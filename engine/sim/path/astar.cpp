#include "sim/path/astar.h"

#include "core/fastmath.h"
#include "core/fpu.h"

#include <algorithm>
#include <cstdlib>

namespace ra::sim::path {

namespace {

constexpr int kGrid = 512;
constexpr int kDefaultMaxSteps = 0xFFF7;

// MoveCost (0x81870C), indexed by the Move result.
constexpr float kMoveCost[8] = { 1.0f, 1000.0f, 1.0f, 1.0f, 60.0f, 20.0f, 8.0f, 10000.0f };
// Per-direction tie-breaks (0x81872C): equal-cost paths prefer N, E, S, W, then the diagonals.
constexpr float kTieBreak[8] = { 0.001f, 0.005f, 0.002f, 0.006f, 0.003f, 0.007f, 0.004f, 0.008f };
constexpr int kDx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
constexpr int kDy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

// The direction of a one-cell move (0x818760, indexed by dy * 3 + dx).
int direction_of(int dx, int dy)
{
	static const int table[9] = { 7, 0, 1, 6, -1, 2, 5, 4, 3 };
	return table[(dy + 1) * 3 + (dx + 1)];
}

float f32(double v) { return ra::fpu::to_float(v); }

struct Node {
	Cell cell;
	int level = 0;
	int parent = -1; // record index
};
struct Record {
	int node = 0;
	float g = 0.0f;
	float f = 0.0f;
	int steps = 0;
};

// The visited marks of one level (ground or bridge deck): the search that last opened a cell and its g.
struct Marks {
	std::vector<uint32_t> stamp = std::vector<uint32_t>(size_t(kGrid) * kGrid, 0);
	std::vector<float> g = std::vector<float>(size_t(kGrid) * kGrid, 0.0f);
};

class Search {
public:
	Result run(const SearchMap& map, const Query& q);

private:
	int create(const SearchMap& map, const Query& q, int parent, Cell cell, float cost, int start_level);
	float f(int r) const { return records_[size_t(r)].f; }
	void heap_insert(int r);
	int heap_pop();
	void sift_down();

	std::vector<Node> nodes_;
	std::vector<Record> records_;
	std::vector<int> heap_; // 1-based
	int count_ = 0;
	Marks ground_, deck_;
	uint32_t id_ = 0;
};

int Search::create(const SearchMap& map, const Query& q, int parent, Cell cell, float cost, int start_level)
{
	// FUN_0042a460: the node's level (a bridge deck is the cell level + 4), g, steps and f.
	Node n;
	n.cell = cell;
	n.parent = parent;
	if (parent < 0) {
		n.level = start_level;
	} else {
		const Node& pn = nodes_[size_t(records_[size_t(parent)].node)];
		n.level = map.level(cell);
		if (map.bridge(cell)) {
			const bool parent_bridge = map.bridge(pn.cell);
			if (parent_bridge && pn.level == map.level(pn.cell) + 4)
				n.level += 4;
			else if (!parent_bridge && std::abs(map.level(cell) - pn.level + 3) <= 1)
				n.level += 4;
		}
	}
	nodes_.push_back(n);
	Record r;
	r.node = static_cast<int>(nodes_.size()) - 1;
	if (parent < 0) {
		r.g = 0.0f;
		r.steps = 1;
	} else {
		r.g = f32(ra::fpu::add(cost, records_[size_t(parent)].g));
		r.steps = records_[size_t(parent)].steps + 1;
	}
	const int dx = std::abs(cell.x - q.goal.x), dy = std::abs(cell.y - q.goal.y);
	r.f = f32(ra::fpu::add(ra::fastmath::sqrt(double(dx * dx + dy * dy)), r.g));
	records_.push_back(r);
	return static_cast<int>(records_.size()) - 1;
}

void Search::heap_insert(int r)
{
	int i = ++count_;
	if (static_cast<int>(heap_.size()) <= i)
		heap_.resize(size_t(i) + 1);
	while (i > 1 && f(heap_[size_t(i / 2)]) > f(r)) {
		heap_[size_t(i)] = heap_[size_t(i / 2)];
		i /= 2;
	}
	heap_[size_t(i)] = r;
}

// The original's sift-down from the root: the first step picks the smallest of 1, 2, 3 (ties to the
// lower index); later steps move only when a child is strictly smaller.
void Search::sift_down()
{
	int c = (count_ < 2 || f(heap_[1]) <= f(heap_[2])) ? 1 : 2;
	if (count_ >= 3 && !(f(heap_[size_t(c)]) <= f(heap_[3])))
		c = 3;
	int i = 1;
	while (c != i) {
		std::swap(heap_[size_t(i)], heap_[size_t(c)]);
		i = c;
		int next = i;
		if (2 * i <= count_ && f(heap_[size_t(2 * i)]) < f(heap_[size_t(i)]))
			next = 2 * i;
		if (2 * i + 1 <= count_ && f(heap_[size_t(2 * i + 1)]) < f(heap_[size_t(next)]))
			next = 2 * i + 1;
		c = next;
	}
}

int Search::heap_pop()
{
	if (count_ == 0)
		return -1;
	const int top = heap_[1];
	heap_[1] = heap_[size_t(count_)];
	--count_;
	if (count_ > 0)
		sift_down();
	return top;
}

Result Search::run(const SearchMap& map, const Query& q)
{
	Result result;
	nodes_.clear();
	records_.clear();
	count_ = 0;
	heap_.assign(2, 0);
	++id_;
	if (id_ == 0) { // the stamps wrapped: start again from clean marks
		ground_ = Marks();
		deck_ = Marks();
		id_ = 1;
	}
	if (!map.exists(q.start) || !map.exists(q.goal))
		return result;
	if (q.start == q.goal && q.start_level == q.goal_level)
		return result; // already there: no path

	result.progress = q.start;
	size_t route_index = 0;
	int cur = create(map, q, -1, q.start, 0.0f, q.start_level);
	int cur_level = q.start_level;
	{
		Marks& m = q.start_level > map.level(q.start) ? deck_ : ground_;
		const size_t i = size_t(q.start.y) * kGrid + size_t(q.start.x);
		m.stamp[i] = id_;
		m.g[i] = 0.0f;
	}

	const int max_steps = q.max_steps < 0 ? kDefaultMaxSteps : q.max_steps;
	int expanded = 0;
	bool next_to_goal = false;
	while (cur >= 0) {
		if (max_steps <= expanded)
			break;
		const Node cur_node = nodes_[size_t(records_[size_t(cur)].node)];
		if (cur_node.cell == q.goal && cur_node.level == q.goal_level)
			break;
		const float cur_g = records_[size_t(cur)].g;
		int held = -1;
		for (int dir = 0; dir < 9 && !next_to_goal; ++dir) {
			Cell nc;
			if (dir == 8) {
				if (!map.tube_exit(cur_node.cell, nc))
					continue;
			} else {
				nc = { cur_node.cell.x + kDx[dir], cur_node.cell.y + kDy[dir] };
			}
			if (!map.exists(nc))
				continue;
			const bool ground = !map.bridge(nc) || std::abs(cur_level - map.level(nc)) < 2;
			if (ground && !map.in_corridor(nc))
				continue;
			Marks& marks = ground ? ground_ : deck_;
			const size_t idx = size_t(nc.y) * kGrid + size_t(nc.x);
			if (marks.stamp[idx] == id_ && marks.g[idx] < f32(ra::fpu::add(cur_g, 1.009)))
				continue;

			int m = static_cast<int>(map.can_enter(nc, dir, cur_level, cur_node.cell));
			if (q.train && m < 7)
				m = 0;
			float step_cost;
			if (dir == 8) {
				const int cheb = std::max(std::abs(cur_node.cell.y - nc.y), std::abs(cur_node.cell.x - nc.x));
				step_cost = static_cast<float>(cheb);
			} else {
				float base = kMoveCost[m < 0 ? 0 : (m > 7 ? 7 : m)];
				if (m == static_cast<int>(Move::MovingBlock))
					base = map.moving_block_cost(nc, !ground);
				if (map.slow(nc))
					base = f32(ra::fpu::mul(base, 4.0));
				step_cost = f32(ra::fpu::add(ra::fpu::mul(base, q.path_cost_factor), kTieBreak[dir]));
			}
			if (m < 7) {
				if (marks.stamp[idx] != id_) {
					const int r = create(map, q, cur, nc, step_cost, cur_level);
					if (held >= 0)
						heap_insert(held);
					held = r;
					marks.stamp[idx] = id_;
					marks.g[idx] = records_[size_t(r)].g;
					if (q.route && route_index + 1 < q.route->size() && map.part(nc) == (*q.route)[route_index + 1]) {
						++route_index;
						result.progress = nc;
					}
				}
			} else if (nc == q.goal && !q.train && std::abs(cur_level - q.goal_level) < 2) {
				next_to_goal = true; // the goal is blocked: the path ends next to it
			}
		}
		if (next_to_goal)
			break;

		if (held < 0) {
			cur = heap_pop();
		} else if (count_ > 0 && f(heap_[1]) <= f(held)) {
			cur = heap_[1];
			heap_[1] = held;
			sift_down();
		} else {
			cur = held;
		}
		if (cur >= 0)
			cur_level = nodes_[size_t(records_[size_t(cur)].node)].level;
		++expanded;
	}

	result.expanded = expanded;
	if (expanded == 10000 || cur < 0 || expanded == max_steps || records_[size_t(cur)].steps <= 1)
		return result;

	// Unwind from the last node to the start (FUN_0042aa90).
	std::vector<int> chain;
	for (int r = cur; r >= 0; r = nodes_[size_t(records_[size_t(r)].node)].parent)
		chain.push_back(r);
	result.found = true;
	result.cost = records_[size_t(cur)].g;
	result.start = nodes_[size_t(records_[size_t(chain.back())].node)].cell;
	for (size_t i = chain.size() - 1; i > 0; --i) {
		const Node& from = nodes_[size_t(records_[size_t(chain[i])].node)];
		const Node& to = nodes_[size_t(records_[size_t(chain[i - 1])].node)];
		const int dx = to.cell.x - from.cell.x, dy = to.cell.y - from.cell.y;
		result.directions.push_back(std::abs(dx) < 2 && std::abs(dy) < 2 ? direction_of(dx, dy) : 8);
		result.levels.push_back(from.level);
	}
	if (q.clean_up) {
		straighten_zigzags(map, q, result.start, result.directions, result.levels);
		shortcut_loops(map, q, result.start, result.directions, result.levels);
	}
	return result;
}

} // namespace

Cell step(Cell c, int dir)
{
	if (dir < 0 || dir > 7)
		return c;
	return { c.x + kDx[dir], c.y + kDy[dir] };
}

// ---------------------------------------------------------------------------------------------
// The clean-up passes (U section 11.6)

namespace {

// One step along a path: a tube jump goes to the tube's exit, or to cell (0, 0) when there is none
// (0x42D490).
Cell advance(const SearchMap& map, Cell c, int dir)
{
	if (dir != 8)
		return step(c, dir & 7);
	Cell exit;
	if (map.tube_exit(c, exit))
		return exit;
	return { 0, 0 };
}

// CanEnterCell with the "check only" arguments. There is no from cell, so the original takes the cell
// behind 'to' (0x4D9C60).
Move check_enter(const SearchMap& map, Cell to, int dir, int level)
{
	if (!map.exists(to))
		return Move::No;
	return map.can_enter(to, dir, level, step(to, (dir + 4) & 7));
}

// The level carried along a straight line: the cell's level, or its deck level when the line is on
// the deck.
int line_level(const SearchMap& map, Cell c, int level)
{
	if (!map.exists(c))
		return 0;
	const int lv = map.level(c);
	if (level - lv == 4 && map.bridge(c))
		return lv + 4;
	return lv;
}

int threat_at(const SearchMap& map, Cell c) { return map.exists(c) ? map.threat(c) : 0; }

// 0x42B420: a run of 'a' steps in direction d1 followed by 'b' steps in d2, 90 degrees apart, starting
// at 'cell'. Replaces the last k steps of the first run and the first k of the second with 2k steps in
// the direction between them, for the largest k (up to min(a, b)) whose straight line is free. Returns
// how many steps it moved past and sets 'cell' to the cell there.
int straighten(const SearchMap& map, const Query& q, std::vector<int>& d, const std::vector<int>& lv, int s, int a, int b, Cell& cell)
{
	const int d1 = d[size_t(s)], d2 = d[size_t(s + a)];
	int mid = (d1 + d2) >> 1;
	if (mid + 1 != d2 && mid + 1 != d1)
		mid = 0;
	if (d1 == 8 || d2 == 8) {
		for (int j = 0; j < a + b; ++j)
			cell = advance(map, cell, d[size_t(s + j)]);
		return a + b;
	}
	Cell c = cell;
	int k = b;
	if (a < b) {
		k = a;
	} else if (b < a) {
		for (int j = 0; j < a - b; ++j)
			c = advance(map, c, d[size_t(s + j)]);
	}
	for (; k > 0; --k) {
		Cell t = step(c, mid & 7);
		int level = lv[size_t(s + a - k)];
		bool blocked = false;
		for (int left = 2 * k; left > 0 && !blocked; --left) {
			// The threat is read at the line's first cell, not at the cell checked.
			blocked = check_enter(map, t, mid, level) != Move::Ok || map.slow(t)
				|| ra::fpu::mul(double(threat_at(map, c)), q.threat_factor) >= 1.0;
			t = step(t, mid & 7);
			// The level for the next check is that cell's own, so a cliff inside the line goes unseen.
			level = line_level(map, t, level);
		}
		if (!blocked) {
			for (int j = a - k; j < a + k; ++j)
				d[size_t(s + j)] = mid;
			for (int j = 0; j < a - k; ++j)
				cell = advance(map, cell, d[size_t(s + j)]);
			return a - k;
		}
		c = step(c, d1 & 7);
	}
	for (int j = 0; j < a; ++j)
		cell = advance(map, cell, d[size_t(s + j)]);
	return a;
}

// 0x42BCA0: walk back from step 'last' to step 'first' (skipping removed steps). Returns the step just
// after the first place, going back, where the distance from 'cell' grows again after having stalled,
// and sets 'cell' to the cell before that step. Without such a place: 'first' and its cell.
int loop_start(const std::vector<int>& d, int last, int first, Cell& cell)
{
	Cell c = cell;
	int ox = 0, oy = 0, best = 0;
	bool stalled = false;
	for (int j = last; j >= first; --j) {
		if (d[size_t(j)] == -2)
			continue;
		const int back = (d[size_t(j)] - 4) & 7;
		ox += kDx[back];
		oy += kDy[back];
		c = step(c, back);
		const int dist = std::max(std::abs(ox), std::abs(oy));
		if (best < dist) {
			best = dist;
			if (stalled) {
				cell = step(c, (back - 4) & 7);
				return j + 1;
			}
		} else {
			stalled = true;
		}
	}
	cell = c;
	return first;
}

// 0x42BE20: replace 'count' steps from 'cell' to cell + (dx, dy) with a diagonal run and a straight
// run (diagonal first; then the other order). The steps left over become -2.
bool rewrite_leg(const SearchMap& map, const Query& q, std::vector<int>& d, int s, int count, Cell cell, int dx, int dy, int level0, bool lenient)
{
	int diag = dx < 0 ? (dy >= 0 ? 5 : 7) : (dy >= 0 ? 3 : 1);
	int straight = dx <= dy ? (dx + dy < 1 ? 6 : 4) : (dx + dy < 1 ? 0 : 2);
	int n1 = std::min(std::abs(dx), std::abs(dy));
	int n2 = std::max(std::abs(dx), std::abs(dy)) - n1;
	const bool threats = q.threat_factor > 1e-5;
	for (int attempt = 0; attempt < 2; ++attempt) {
		if (n1 != 0) {
			int hot = 0; // cells on the route with threat * factor >= 0.01
			int level = level0;
			Cell c = cell;
			bool blocked = false;
			auto check = [&](int dir) {
				c = step(c, dir);
				if (threats && ra::fpu::mul(double(threat_at(map, c)), q.threat_factor) >= 0.01)
					++hot;
				blocked = !(check_enter(map, c, dir, level) == Move::Ok && !map.slow(c) && hot < 4 && (lenient || hot < 1));
				level = line_level(map, c, level);
			};
			for (int j = 0; j < n1 && !blocked; ++j)
				check(diag);
			for (int j = 0; j < n2 && !blocked; ++j)
				check(straight);
			if (!blocked) {
				int i = s;
				for (int j = 0; j < n1; ++j)
					d[size_t(i++)] = diag;
				for (int j = 0; j < n2; ++j)
					d[size_t(i++)] = straight;
				for (; i < s + count; ++i)
					d[size_t(i)] = -2;
				return true;
			}
		}
		std::swap(diag, straight);
		std::swap(n1, n2);
	}
	return false;
}

} // namespace

void straighten_zigzags(const SearchMap& map, const Query& q, Cell start, std::vector<int>& d, const std::vector<int>& lv)
{
	// 0x42B210. A candidate is a run of diagonal steps followed by a run at 90 degrees to it (a straight
	// run resets the scan), except right after a replacement, whose direction is kept even when straight.
	const int n = static_cast<int>(d.size());
	Cell cur = start, run_cell = start;
	int prev = -1, run_len = 0, run_start = 0;
	int second = -1, second_start = 0, second_len = 0;
	bool zig = false;
	while (run_start + run_len < n) {
		if (second_start + second_len >= n)
			break;
		if (zig) {
			if (d[size_t(second_start + second_len)] == second) {
				++second_len;
			} else {
				run_start += straighten(map, q, d, lv, run_start, run_len, second_len, run_cell);
				run_len = 1;
				zig = false;
				cur = step(run_cell, d[size_t(run_start)] & 7);
				prev = d[size_t(run_start)];
			}
			continue;
		}
		const int i = run_start + run_len;
		const int dir = d[size_t(i)];
		const int turn = (dir - prev) & 7;
		if (dir == prev) {
			++run_len;
		} else if ((turn == 2 || turn == 6) && prev != -1 && prev != 8 && dir != 8) {
			zig = true;
			second_len = 1;
			second_start = i;
			second = dir;
		} else {
			run_len = 1;
			prev = (dir & 1) ? dir : -1;
			run_cell = cur;
			run_start = i;
		}
		cur = advance(map, cur, dir);
	}
	if (zig)
		straighten(map, q, d, lv, run_start, run_len, second_len, run_cell);
}

void shortcut_loops(const SearchMap& map, const Query& q, Cell start, std::vector<int>& d, const std::vector<int>& lv)
{
	// 0x42B7F0. Over the first 20 steps the path is split into stretches that are monotonic on both
	// axes (A: the offset within the stretch). A second offset B should grow in Chebyshev length with
	// every step; when it doesn't, the steps from the place loop_start finds are rewritten as the
	// shortest route. When a new stretch starts, B becomes (stretch start - here), as in the original.
	const int n = static_cast<int>(d.size());
	Cell cur = start;
	int ax = 0, ay = 0, prev_ax = 0, prev_ay = 0; // A, and its last absolute values
	int bx = 0, by = 0, b_max = 0;                 // B, and the largest Chebyshev length it reached
	Cell ref { 0, 0 };                             // where the current stretch started; (0, 0) = none yet
	int stretch = 0, prev_stretch = 0;             // the steps where the last two stretches started
	int i = 0;
	while (i < n) {
		if (i > 19)
			break;
		const int dir = d[size_t(i)];
		if (dir == 8) {
			// A tube jump: the original moves the cell one step north here, then starts over.
			cur = step(cur, 0);
			++i;
			ax = ay = bx = by = 0;
			prev_ax = prev_ay = 0;
			b_max = 0;
			ref = { 0, 0 };
			stretch = prev_stretch = i;
			continue;
		}
		if (dir == -2) {
			++i;
			continue;
		}
		const int nax = ax + kDx[dir & 7], nay = ay + kDy[dir & 7];
		const int nbx = bx + kDx[dir & 7], nby = by + kDy[dir & 7];
		if (std::abs(nax) < prev_ax || std::abs(nay) < prev_ay) {
			// A turns back on an axis: a new stretch starts here, and this step is looked at again.
			if (ref.x != 0 || ref.y != 0) {
				prev_stretch = stretch;
				bx = int16_t(ref.x - cur.x);
				by = int16_t(ref.y - cur.y);
				b_max = std::max(std::abs(bx), std::abs(by));
			}
			prev_ax = prev_ay = 0;
			ax = ay = 0;
			ref = cur;
			stretch = i;
			continue;
		}
		ax = nax;
		ay = nay;
		prev_ax = std::abs(nax);
		prev_ay = std::abs(nay);
		cur = step(cur, dir & 7);
		const int dist = std::max(std::abs(nbx), std::abs(nby));
		if (b_max < dist) {
			b_max = dist;
		} else {
			Cell from = cur;
			const int first = loop_start(d, i, prev_stretch, from);
			rewrite_leg(map, q, d, first, i - first + 1, from, cur.x - from.x, cur.y - from.y, lv[size_t(first)], false);
		}
		bx = nbx;
		by = nby;
		++i;
	}
	if (ref.x != 0 || ref.y != 0) {
		const int dist = std::max(std::abs(int(int16_t(cur.x - ref.x))), std::abs(int(int16_t(cur.y - ref.y))));
		if (i - stretch - 1 > dist) {
			Cell from = cur;
			const int first = loop_start(d, i - 1, stretch, from);
			rewrite_leg(map, q, d, first, (i - 1) - first + 1, from, cur.x - from.x, cur.y - from.y, lv[size_t(first)], true);
		}
	}
	// Drop the removed steps.
	std::vector<int> kept;
	kept.reserve(d.size());
	for (int j = 0; j < n && d[size_t(j)] != -1; ++j)
		if (d[size_t(j)] != -2)
			kept.push_back(d[size_t(j)]);
	d = std::move(kept);
}

Result find_path(const SearchMap& map, const Query& query)
{
	static Search search; // the simulation is single-threaded; the marks are reused between searches
	return search.run(map, query);
}

} // namespace ra::sim::path
