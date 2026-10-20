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
	return result;
}

} // namespace

Cell step(Cell c, int dir)
{
	if (dir < 0 || dir > 7)
		return c;
	return { c.x + kDx[dir], c.y + kDy[dir] };
}

Result find_path(const SearchMap& map, const Query& query)
{
	static Search search; // the simulation is single-threaded; the marks are reused between searches
	return search.run(map, query);
}

} // namespace ra::sim::path
