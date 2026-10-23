#pragma once

// The map's zones, as MapClass keeps them for pathfinding (U section 11.7).
//
// Every cell has a passability class (0 clear, 1 crushable wall, 2 wall or tree, 3 beach, 4 water,
// 5 partly blocked, 6 impassable, 7 outside the usable area; CellClass +0x4C, 0x483C80) and a level.
//  - Regions (0x56C510, flood fill 0x56CB90): connected cells of one class whose levels differ by
//    less than 2. Each of the 13 movement zones groups connected regions whose classes it can cross
//    (table 0x82A594) into zones; two cells in different zones can't reach each other.
//  - Three hierarchical levels (0x581F90, flood fill 0x5824A0): level L cuts the map into blocks of
//    2^(L+1) cells and splits each region inside a block into connected parts. Each part is a node of
//    the level's graph, linked to the parts it touches.
//  - The hierarchical search (0x42C290) finds the route through the level 2 graph, then level 1
//    within it, then level 0; the cell A* may only expand into level-0 parts on that route.
//
// The data is laid out like the original's: one record per cell of the (W + H + 1)^2 square, in rows
// of W + H + 1, because the flood fills walk that array and their results depend on its order.
// When the cell search fails inside the corridor, the links around the furthest cell it reached are
// blocked and the hierarchical search runs again (0x42CCD0, up to five attempts in FindPath).
// Not here yet: bridges and tubes (the link list at Map +0x54), the incremental update when a cell
// changes (0x584550) and threat costs.

#include "sim/path/astar.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace ra::sim::path {

class Zones {
public:
	static constexpr int kMovementZones = 13;
	static constexpr int kLevels = 3;

	// Whether movement zone 'mz' can cross cells of class 'cls' (0x82A594: 1 = passable).
	static bool passable(int mz, int cls);
	// The hierarchical search's cost of entering a part of class 'cls' (0x7E3794).
	static float class_cost(int cls);

	// 'width' / 'height': the map's Size. 'cls' and 'level' are asked for every cell of the square.
	void build(int width, int height, const std::function<int(int x, int y)>& cls, const std::function<int(int x, int y)>& level);

	bool built() const { return !rec_.empty(); }
	int region(Cell c) const;
	int region_count() const { return region_count_; }
	// MapClass::GetMovementZoneType (0x56D230) without bridges: the zone of the cell's region; 0xFFFF
	// for region 0 (cells of class 7), 1 for regions the movement zone can't cross.
	int movement_zone(Cell c, int mz) const;
	// The part of 'c' at a hierarchical level; 0 for none.
	int part(Cell c, int level) const;

	struct Edge {
		int to = 0;
		uint8_t type = 0; // 1: found across the block's left or right side diagonally (costs 0.001 more)
	};
	struct Node {
		int parent = 0;   // the part one level up that holds this one
		int cls = 7;      // the class of the cell the part was filled from
		std::vector<Edge> edges;
	};
	const std::vector<Node>& nodes(int level) const { return nodes_[size_t(level)]; }

	// The hierarchical search for movement zone 'mz' from 'start' to 'goal' (same movement zone).
	// On success the parts on the route at each level are marked; the cell search's corridor is the
	// level-0 marks.
	bool search(Cell start, Cell goal, int mz);
	bool in_corridor(Cell c) const;
	// FindPath clears the blocked links at its start.
	void clear_blocked();
	// 0x42CCD0, after a failed cell search that reached 'cell': at each level, block the links from the
	// cell's part that can't be used from where the search got to. 'enterable' is CanEnterCell (check
	// only, at the cell's own level) == Ok. Returns false when the hierarchical search must be turned
	// off for the rest of this FindPath.
	bool block_around(Cell cell, int mz, const std::function<bool(Cell to, int dir)>& enterable);
	// The route found at each level, from the start's part to the goal's.
	const std::vector<int>& route(int level) const { return route_[size_t(level)]; }

private:
	struct Rec {
		uint8_t cls = 7;
		uint8_t level = 0;
		uint16_t region = 0;
	};
	struct PartRec {
		int16_t part[kLevels] = { 0, 0, 0 };
	};
	using Table = std::vector<std::vector<uint64_t>>; // 256 buckets of (key, type) entries

	int index(int x, int y) const { return pad_ + y * row_ + x; }
	int x_of(int i) const { return (i - pad_) % row_; }
	bool in_square(int x, int y) const { return x >= 0 && y >= 0 && x < row_ && y < row_; }
	int region_fill(int p, int id, int& advance);
	void add_region_edge(int nb, int id);
	void build_level(int level);
	int part_fill(int p, int level, int id, int x0, int y0, int size, int x, int y);
	// IsWithinUsableArea with the level (0x578460): exactly the cells whose class isn't 7.
	bool usable(int x, int y) const { return in_square(x, y) && rec_[size_t(index(x, y))].cls != 7; }
	void add_part_edge(int level, int nb, int id, uint8_t type);
	bool part_is_split(Cell cell, int level, int mz, const std::function<bool(Cell, int)>& enterable, std::vector<int>& unreached);
	bool block_route_links(int part, int level);
	static uint32_t link_key(int a, int b);

	int width_ = 0, height_ = 0, row_ = 0, pad_ = 0;
	std::vector<Rec> rec_;
	std::vector<PartRec> parts_;
	int region_count_ = 0;
	std::vector<uint8_t> region_class_;
	std::vector<std::vector<uint16_t>> region_edges_;
	std::vector<uint16_t> movement_zone_[kMovementZones];
	Table region_table_;
	uint16_t region_cache_ = 0;        // DAT_00ABDE8C: the last neighbour region an edge was looked up for
	Table part_table_;
	std::vector<Node> nodes_[kLevels];

	// The hierarchical search's state (AStarClass): marks and costs per part, stamped with the search id.
	uint32_t search_id_ = 0;
	std::vector<uint32_t> marks_[kLevels], visited_[kLevels];
	std::vector<float> cost_[kLevels];
	std::vector<int> route_[kLevels];
	std::vector<uint32_t> blocked_[kLevels]; // links the search may not use (AStarClass +0x74 per level)
};

} // namespace ra::sim::path
