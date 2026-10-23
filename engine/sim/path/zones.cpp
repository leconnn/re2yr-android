#include "sim/path/zones.h"

#include "core/fpu.h"

#include <algorithm>
#include <cstdlib>

namespace ra::sim::path {

namespace {

// 0x82A594: per movement zone, the group of each class; group 1 can be crossed.
constexpr int kGroups[Zones::kMovementZones][8] = {
	{ 1, 2, 2, 2, 2, 2, 2, 3 }, // Normal
	{ 1, 1, 2, 2, 2, 2, 2, 3 }, // Crusher
	{ 1, 1, 1, 2, 2, 2, 2, 3 }, // Destroyer
	{ 1, 1, 1, 1, 1, 1, 2, 3 }, // AmphibiousDestroyer
	{ 1, 1, 2, 1, 1, 2, 2, 3 }, // AmphibiousCrusher
	{ 1, 2, 2, 1, 1, 2, 2, 3 }, // Amphibious
	{ 1, 1, 1, 2, 2, 2, 1, 3 }, // Subterrannean
	{ 1, 2, 2, 2, 2, 1, 2, 3 }, // Infantry
	{ 1, 1, 1, 2, 2, 1, 2, 3 }, // InfantryDestroyer
	{ 1, 1, 1, 1, 1, 1, 1, 3 }, // Fly
	{ 2, 2, 2, 2, 1, 2, 2, 3 }, // Water
	{ 2, 2, 2, 1, 1, 2, 2, 3 }, // WaterBeach
	{ 1, 1, 1, 2, 2, 2, 2, 3 }, // CrusherAll
};

// 0x7E3794, indexed by class.
constexpr float kClassCost[8] = { 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };

int group(int mz, int cls)
{
	if (mz < 0 || mz >= Zones::kMovementZones || cls < 0 || cls > 7)
		return 3;
	return kGroups[mz][cls];
}

uint32_t edge_key(int nb, int id) { return (uint32_t(nb & 0xFFFF) << 16) | uint32_t(id & 0xFFFF); }
int edge_bucket(int nb, int id) { return ((nb & 0xF) << 4) | (id & 0xF); }

// Looks the key up in its bucket and appends it (with its type) if it isn't there.
void add_once(std::vector<uint64_t>& bucket, uint32_t key, uint8_t type)
{
	for (size_t i = bucket.size(); i-- > 0;)
		if (uint32_t(bucket[i]) == key)
			return;
	bucket.push_back(uint64_t(key) | (uint64_t(type) << 32));
}

} // namespace

bool Zones::passable(int mz, int cls) { return group(mz, cls) == 1; }

float Zones::class_cost(int cls) { return cls >= 0 && cls < 8 ? kClassCost[cls] : 1.0f; }

int Zones::region(Cell c) const
{
	if (!in_square(c.x, c.y))
		return 0;
	return rec_[size_t(index(c.x, c.y))].region;
}

int Zones::movement_zone(Cell c, int mz) const
{
	if (mz < 0 || mz >= kMovementZones || movement_zone_[mz].empty())
		return 0xFFFF;
	return movement_zone_[mz][size_t(region(c))];
}

int Zones::part(Cell c, int level) const
{
	if (!in_square(c.x, c.y))
		return 0;
	return parts_[size_t(index(c.x, c.y))].part[level];
}

// ---------------------------------------------------------------------------------------------
// Regions (0x56C510)

void Zones::build(int width, int height, const std::function<int(int, int)>& cls, const std::function<int(int, int)>& level)
{
	width_ = width;
	height_ = height;
	row_ = width + height + 1;
	// Padding on both sides stands in for the memory around the original's array: the fills read one
	// row beyond the cells they fill, which in the original only ever happens next to class 7 cells.
	pad_ = row_ + 2;
	rec_.assign(size_t(row_) * row_ + 2 * size_t(pad_), Rec {});
	parts_.assign(rec_.size(), PartRec {});
	for (int y = 0; y < row_; ++y)
		for (int x = 0; x < row_; ++x) {
			Rec& r = rec_[size_t(index(x, y))];
			r.cls = uint8_t(cls(x, y));
			r.level = uint8_t(level(x, y));
		}

	region_table_.assign(256, {});
	region_class_.assign(1, 7);
	int id = 1;
	const int end = pad_ + row_ * row_;
	for (int p = pad_; p < end;) {
		if (rec_[size_t(p)].cls == 7 || rec_[size_t(p)].region != 0) {
			++p;
			continue;
		}
		region_cache_ = 0;
		int advance = 0;
		region_fill(p, id, advance);
		region_class_.push_back(rec_[size_t(p)].cls);
		++id;
		p += advance;
	}
	region_count_ = id & 0xFFFF;

	// Neighbour lists, in the order of the edge table's buckets.
	region_edges_.assign(size_t(region_count_), {});
	for (const auto& bucket : region_table_)
		for (uint64_t e : bucket) {
			const int lo = int(e & 0xFFFF), hi = int((e >> 16) & 0xFFFF);
			region_edges_[size_t(lo)].push_back(uint16_t(hi));
			region_edges_[size_t(hi)].push_back(uint16_t(lo));
		}

	// Movement zones: connected regions whose classes the movement zone can cross, numbered from 2.
	for (int mz = 0; mz < kMovementZones; ++mz) {
		std::vector<uint16_t>& z = movement_zone_[mz];
		z.assign(size_t(region_count_), 0);
		for (int i = 0; i < region_count_; ++i)
			z[size_t(i)] = group(mz, region_class_[size_t(i)]) != 1 ? 1 : 0;
		uint16_t next = 2;
		std::vector<int> stack;
		for (int i = 0; i < region_count_; ++i) {
			if (z[size_t(i)] != 0)
				continue;
			const int g = group(mz, region_class_[size_t(i)]);
			z[size_t(i)] = next;
			stack.assign(1, i);
			while (!stack.empty()) {
				const int r = stack.back();
				stack.pop_back();
				const auto& nbs = region_edges_[size_t(r)];
				for (size_t k = nbs.size(); k-- > 0;) {
					const int nb = nbs[k];
					if (group(mz, region_class_[size_t(nb)]) == g && z[size_t(nb)] == 0) {
						z[size_t(nb)] = next;
						stack.push_back(nb);
					}
				}
			}
			++next;
		}
		if (!z.empty())
			z[0] = 0xFFFF;
	}

	for (int l = kLevels - 1; l >= 0; --l)
		build_level(l);

	for (int l = 0; l < kLevels; ++l) {
		marks_[l].assign(nodes_[l].size(), 0);
		visited_[l].assign(nodes_[l].size(), 0);
		cost_[l].assign(nodes_[l].size(), 0.0f);
		route_[l].clear();
	}
	search_id_ = 0;
}

void Zones::add_region_edge(int nb, int id)
{
	region_cache_ = uint16_t(nb);
	add_once(region_table_[size_t(edge_bucket(nb, id))], edge_key(nb, id), 0);
}

int Zones::region_fill(int p, int id, int& advance)
{
	// 0x56CB90. Fill left, then right (that scan allows level steps below 4, continuing from the level
	// the left scan ended on), then the row above and the row below from one cell left of the run to
	// one cell right of it, recursing into unfilled cells of the class.
	const auto lv = [&](int i) { return int(rec_[size_t(i)].level); };
	const int c0 = rec_[size_t(p)].cls;
	const bool class6 = c0 == 6;
	int level = lv(p);
	int q = p;
	do {
		if (std::abs(lv(q) - level) > 1)
			break;
		rec_[size_t(q)].region = uint16_t(id);
		level = lv(q);
		--q;
	} while (rec_[size_t(q)].cls == c0);
	int nb = rec_[size_t(q)].region;
	if (nb != 0 && (std::abs(lv(q) - level) < 2 || class6) && nb != region_cache_ && nb != (id & 0xFFFF))
		add_region_edge(nb, id);

	int r = p;
	while (rec_[size_t(r)].cls == c0 && std::abs(lv(r) - level) < 4) {
		rec_[size_t(r)].region = uint16_t(id);
		level = lv(r);
		++r;
	}
	nb = rec_[size_t(r)].region;
	if (nb != 0 && (std::abs(lv(r) - level) < 2 || class6) && nb != region_cache_ && nb != (id & 0xFFFF))
		add_region_edge(nb, id);

	int total = (r - q) - 1;
	advance = (r - p) - 1;
	const int R = row_;
	// The level each cell of the next row is compared with: the run cell diagonally after it, the
	// one straight across for the last run cell, the one diagonally before for the cell past the run.
	const auto across = [&](int i, int last, int delta) {
		if (i < last)
			return i + delta + 1;
		if (i == last)
			return i + delta;
		return i + delta - 1;
	};
	const int last_above = r - R - 1;
	for (int a = q - R; a <= last_above + 1;) {
		const int adj = across(a, last_above, R);
		nb = rec_[size_t(a)].region;
		if (nb != 0) {
			if (nb != (id & 0xFFFF) && nb != region_cache_ && (std::abs(lv(a) - lv(adj)) < 2 || class6))
				add_region_edge(nb, id);
			++a;
		} else if (rec_[size_t(a)].cls != c0 || std::abs(lv(a) - lv(adj)) > 1) {
			++a;
		} else {
			int step = 0;
			total += region_fill(a, id, step);
			a += step;
		}
	}
	const int last_below = r + R - 1;
	for (int b = q + R; b <= last_below + 1;) {
		const int adj = across(b, last_below, -R);
		nb = rec_[size_t(b)].region;
		if (nb == 0) {
			if (rec_[size_t(b)].cls == c0 && std::abs(lv(b) - lv(adj)) < 2) {
				int step = 0;
				total += region_fill(b, id, step);
				b += step;
			} else {
				++b;
			}
		} else {
			if (nb != (id & 0xFFFF) && nb != region_cache_ && (std::abs(lv(b) - lv(adj)) < 2 || class6))
				add_region_edge(nb, id);
			++b;
		}
	}
	return total;
}

// ---------------------------------------------------------------------------------------------
// Hierarchical levels (0x581F90)

void Zones::add_part_edge(int level, int nb, int id, uint8_t type)
{
	(void)level;
	add_once(part_table_[size_t(edge_bucket(nb, id))], edge_key(nb, id), type);
}

void Zones::build_level(int level)
{
	part_table_.assign(256, {});
	for (PartRec& r : parts_)
		r.part[level] = 0;
	std::vector<Node>& nodes = nodes_[size_t(level)];
	nodes.assign(1, Node {});
	const int size = 1 << (level + 1);
	const int mask = size - 1;
	int id = 1;
	int x = 0, y = 0, x0 = 0, y0 = 0;
	const int end = pad_ + row_ * row_;
	for (int p = pad_; p < end;) {
		const int cls = rec_[size_t(p)].cls;
		const int parent = level + 1 < kLevels ? parts_[size_t(p)].part[level + 1] : 0;
		if (cls == 7 || parts_[size_t(p)].part[level] != 0) {
			++p;
			++x;
		} else {
			const int advance = part_fill(p, level, id, x0, y0, size, x, y);
			Node n;
			n.parent = parent;
			n.cls = cls;
			nodes.push_back(std::move(n));
			id = (id + 1) & 0xFFFF;
			p += advance;
			x += advance;
		}
		if (x == row_) {
			++y;
			x = 0;
			y0 = y - (y & mask);
			x0 = 0;
		} else if ((mask & x) == 0) {
			x0 = x;
		}
	}
	for (const auto& bucket : part_table_)
		for (uint64_t e : bucket) {
			const int lo = int(e & 0xFFFF), hi = int((e >> 16) & 0xFFFF);
			const uint8_t type = uint8_t(e >> 32);
			if (lo < int(nodes.size()) && hi < int(nodes.size())) {
				nodes[size_t(lo)].edges.push_back({ hi, type });
				nodes[size_t(hi)].edges.push_back({ lo, type });
			}
		}
}

int Zones::part_fill(int p, int level, int id, int x0, int y0, int size, int x, int y)
{
	// 0x5824A0: like the region fill, but inside the block [x0, x0 + size) x [y0, y0 + size), over
	// cells of the seed's region, with level steps below 2 in both directions. Neighbour parts are
	// recorded only between cells inside the usable area.
	const auto lv = [&](int i) { return int(rec_[size_t(i)].level); };
	const auto pt = [&](int i) -> int16_t& { return parts_[size_t(i)].part[level]; };
	const int region0 = rec_[size_t(p)].region;
	const int x1 = x0 + size - 1, y1 = y0 + size - 1;
	const int id16 = id & 0xFFFF;
	int cache = -1;

	int q = p, i = x;
	int level_left = lv(p);
	do {
		if (i < x0 || std::abs(lv(q) - level_left) > 1)
			break;
		pt(q) = int16_t(id);
		level_left = lv(q);
		--q;
		--i;
	} while (rec_[size_t(q)].region == region0);
	const int left = i;
	int nb = pt(q);
	if (nb > 0 && std::abs(lv(q) - level_left) < 2 && nb != cache && usable(left, y) && usable(left + 1, y)) {
		cache = nb;
		add_part_edge(level, nb, id, 0);
	}

	int r = p, j = x;
	int level_right = lv(p);
	while (rec_[size_t(r)].region == region0 && j <= x1 && std::abs(lv(r) - level_right) < 2) {
		pt(r) = int16_t(id);
		level_right = lv(r);
		++j;
		++r;
	}
	nb = pt(r);
	if (nb > 0 && std::abs(lv(r) - level_right) < 2 && nb != cache && usable(j, y) && usable(j - 1, y)) {
		cache = nb;
		add_part_edge(level, nb, id, 0);
	}
	const int filled = (r - p) - 1;

	// The cell of the run each cell of the next row is compared with.
	const auto across_x = [&](int k) { return k == left ? k + 1 : (k < j ? k : k - 1); };
	const int R = row_;
	for (int k = left, a = q - R; k <= j;) {
		const int ax = across_x(k);
		const int adj = a + R + (ax - k);
		nb = pt(a);
		if (nb == 0 && y0 < y && x0 <= k && k <= x1) {
			if (rec_[size_t(a)].region != region0 || std::abs(lv(adj) - lv(a)) > 1) {
				++a;
				++k;
			} else {
				part_fill(a, level, id, x0, y0, size, k, y - 1);
			}
			continue;
		}
		if (nb != id16 && nb != cache && nb != 0 && std::abs(lv(adj) - lv(a)) < 2 && usable(ax, y) && usable(k, y - 1)) {
			cache = nb;
			add_part_edge(level, nb, id, (k < x0 || k > x1) ? 1 : 0);
		}
		++a;
		++k;
	}
	for (int k = left, b = q + R; k <= j;) {
		const int ax = across_x(k);
		const int adj = b - R + (ax - k);
		nb = pt(b);
		if (nb == 0 && y < y1 && x0 <= k && k <= x1) {
			if (rec_[size_t(b)].region == region0 && std::abs(lv(b) - lv(adj)) < 2) {
				part_fill(b, level, id, x0, y0, size, k, y + 1);
			} else {
				++b;
				++k;
			}
			continue;
		}
		if (nb != id16 && nb != cache && nb != 0 && std::abs(lv(b) - lv(adj)) < 2 && usable(ax, y) && usable(k, y + 1)) {
			cache = nb;
			add_part_edge(level, nb, id, (k < x0 || k > x1) ? 1 : 0);
		}
		++b;
		++k;
	}
	return filled;
}

// ---------------------------------------------------------------------------------------------
// The hierarchical search (0x42C290)

bool Zones::search(Cell start, Cell goal, int mz)
{
	if (!built())
		return false;
	// A new search id for every search (AStarClass +0x28, advanced by 0x42A5B0).
	if (++search_id_ == 0) {
		for (int l = 0; l < kLevels; ++l) {
			std::fill(marks_[l].begin(), marks_[l].end(), 0u);
			std::fill(visited_[l].begin(), visited_[l].end(), 0u);
		}
		++search_id_;
	}
	const uint32_t id = search_id_;
	struct Record {
		int parent = -1;
		int part = 0;
		float cost = 0.0f;
		int steps = 0;
	};
	std::vector<Record> records;
	std::vector<int> heap(1, -1); // 1-based
	const auto f = [&](int slot) { return records[size_t(heap[size_t(slot)])].cost; };
	const auto push = [&](int r) {
		heap.push_back(r);
		size_t k = heap.size() - 1;
		while (k > 1 && records[size_t(heap[k / 2])].cost > records[size_t(r)].cost) {
			heap[k] = heap[k / 2];
			k /= 2;
		}
		heap[k] = r;
	};
	const auto pop = [&]() -> int {
		if (heap.size() <= 1)
			return -1;
		const int top = heap[1];
		heap[1] = heap.back();
		heap.pop_back();
		const int n = int(heap.size()) - 1;
		int c = (n < 2 || f(1) <= f(2)) ? 1 : 2;
		if (!(n < 3 || f(c) <= f(3)))
			c = 3;
		int cur = 1;
		while (c != cur) {
			std::swap(heap[size_t(cur)], heap[size_t(c)]);
			cur = c;
			if (2 * cur <= n && f(2 * cur) < f(cur))
				c = 2 * cur;
			if (2 * cur + 1 <= n && f(2 * cur + 1) < f(c))
				c = 2 * cur + 1;
		}
		return top;
	};

	for (int l = kLevels - 1; l >= 0; --l) {
		const std::vector<Node>& nodes = nodes_[size_t(l)];
		std::vector<uint32_t>& marks = marks_[size_t(l)];
		std::vector<uint32_t>& visited = visited_[size_t(l)];
		std::vector<float>& cost = cost_[size_t(l)];
		const std::vector<uint32_t>* upper = l + 1 < kLevels ? &marks_[size_t(l + 1)] : nullptr;
		heap.assign(1, -1);
		records.clear();
		route_[size_t(l)].clear();
		const int s = part(start, l), g = part(goal, l);
		marks[size_t(s)] = id;
		marks[size_t(g)] = id;
		if (s == g) {
			route_[size_t(l)].push_back(s);
			continue;
		}
		records.push_back({ -1, s, 0.0f, 0 });
		push(0);
		visited[size_t(s)] = id;
		cost[size_t(s)] = 0.0f;
		int cur = pop();
		while (cur >= 0 && records[size_t(cur)].part != g) {
			const Record here = records[size_t(cur)];
			for (const Edge& e : nodes[size_t(here.part)].edges) {
				const Node& to = nodes[size_t(e.to)];
				// The threat cost (0x585F40) needs the owner's threat map, which isn't here yet.
				const float c = ra::fpu::to_float(ra::fpu::add(ra::fpu::add(double(class_cost(to.cls)), double(here.cost)), e.type ? double(0.001f) : 0.0));
				if (!blocked_[size_t(l)].empty()) {
					const uint32_t key = link_key(here.part, e.to);
					if (std::find(blocked_[size_t(l)].begin(), blocked_[size_t(l)].end(), key) != blocked_[size_t(l)].end())
						continue;
				}
				const bool better = visited[size_t(e.to)] != id || c < cost[size_t(e.to)];
				const bool inside = l == kLevels - 1 || (*upper)[size_t(to.parent)] == id || to.cls == 1;
				if (better && inside && passable(mz, to.cls)) {
					records.push_back({ cur, e.to, c, here.steps + 1 });
					push(int(records.size()) - 1);
					visited[size_t(e.to)] = id;
					cost[size_t(e.to)] = c;
				}
			}
			cur = pop();
		}
		if (cur < 0)
			return false;
		std::vector<int>& route = route_[size_t(l)];
		for (int r = cur; r >= 0; r = records[size_t(r)].parent) {
			if (records[size_t(r)].parent >= 0)
				marks[size_t(records[size_t(r)].part)] = id;
			route.push_back(records[size_t(r)].part);
		}
		std::reverse(route.begin(), route.end());
	}
	return true;
}

uint32_t Zones::link_key(int a, int b)
{
	const uint16_t x = uint16_t(a), y = uint16_t(b);
	return x < y ? (uint32_t(x) << 16) | y : (uint32_t(y) << 16) | x;
}

void Zones::clear_blocked()
{
	for (auto& b : blocked_)
		b.clear();
}

bool Zones::part_is_split(Cell cell, int level, int mz, const std::function<bool(Cell, int)>& enterable, std::vector<int>& unreached)
{
	// 0x5840C0: flood the cell's part (in its block) over the neighbours the unit can enter (or that its
	// movement zone can't cross at all), collecting the other parts the flood touches.
	const int size = 1 << (level + 1);
	const int m = size - 1;
	uint8_t visited[8][8] = {};
	const int u = part(cell, level);
	std::vector<int> touched;
	int last = 0;
	std::vector<Cell> stack(1, cell);
	visited[cell.x & m][cell.y & m] = 1;
	while (!stack.empty()) {
		const Cell here = stack.back();
		stack.pop_back();
		for (int dir = 0; dir < 8; ++dir) {
			const Cell n = step(here, dir);
			const int nu = part(n, level);
			const int cls = in_square(n.x, n.y) ? rec_[size_t(index(n.x, n.y))].cls : 7;
			if (!(enterable(n, dir) || !passable(mz, cls)))
				continue;
			if (nu == u) {
				if (!visited[n.x & m][n.y & m]) {
					visited[n.x & m][n.y & m] = 1;
					stack.push_back(n);
				}
			} else if (nu != last && nu != 0) {
				if (std::find(touched.begin(), touched.end(), nu) == touched.end())
					touched.push_back(nu);
				last = nu;
			}
		}
	}
	// The split test: the original checks the one cell (x + (x & m), y + (y & m)) against every slot of
	// the visited grid instead of each cell of the block.
	const Cell probe { cell.x + (cell.x & m), cell.y + (cell.y & m) };
	if (usable(probe.x, probe.y) && part(probe, level) == u)
		for (int i = 0; i < size; ++i)
			for (int j = 0; j < size; ++j)
				if (!visited[i][j])
					return true;
	// Not split: the part's links to parts the flood never touched can't be used from here.
	const std::vector<Edge>& edges = nodes_[size_t(level)][size_t(u)].edges;
	for (size_t k = edges.size(); k-- > 0;)
		if (std::find(touched.begin(), touched.end(), edges[k].to) == touched.end())
			unreached.push_back(edges[k].to);
	return false;
}

bool Zones::block_route_links(int u, int level)
{
	// 0x42CF80: block the link from the part to the next part of the route (the previous one for the
	// last part), and the links from it to every part both of them touch.
	const std::vector<int>& route = route_[size_t(level)];
	const int n = int(route.size());
	if (n < 2)
		return false;
	int k = 0;
	while (k < n && route[size_t(k)] != u)
		++k;
	if (k == n)
		return false;
	int self, other;
	if (k == n - 1) {
		other = route[size_t(k)];
		self = route[size_t(k - 1)];
	} else {
		self = route[size_t(k)];
		other = route[size_t(k + 1)];
	}
	if (self != other)
		blocked_[size_t(level)].push_back(link_key(self, other));
	const std::vector<Edge>& a = nodes_[size_t(level)][size_t(other)].edges;
	const std::vector<Edge>& b = nodes_[size_t(level)][size_t(self)].edges;
	for (size_t i = a.size(); i-- > 0;) {
		const int w = a[i].to;
		if (w == self)
			continue;
		for (size_t j = b.size(); j-- > 0;)
			if (b[j].to == w && w != self)
				blocked_[size_t(level)].push_back(link_key(self, w));
	}
	return true;
}

bool Zones::block_around(Cell cell, int mz, const std::function<bool(Cell to, int dir)>& enterable)
{
	bool hierarchical = true;
	for (int l = 0; l < kLevels; ++l) {
		const int u = part(cell, l);
		std::vector<int> unreached;
		if (!part_is_split(cell, l, mz, enterable, unreached)) {
			for (size_t k = unreached.size(); k-- > 0;)
				if (unreached[k] != u)
					blocked_[size_t(l)].push_back(link_key(u, unreached[k]));
		} else if (!block_route_links(u, l)) {
			hierarchical = false;
		}
	}
	return hierarchical;
}

bool Zones::in_corridor(Cell c) const
{
	const int p = part(c, 0);
	return p > 0 && size_t(p) < marks_[0].size() && marks_[0][size_t(p)] == search_id_;
}

} // namespace ra::sim::path
