#pragma once

// The cell A* search (AStarClass, U section 11.4, cell search 0x429A90).
//
// The search is written against a small interface, so it can run on the real map and objects or on
// a test grid. Everything that decides which path wins follows the original:
//  - costs are 32-bit floats, stored with the original's truncation, and every step adds a tiny
//    per-direction tie-break;
//  - h is the Euclidean cell distance (the original's table square root);
//  - a cell is opened at most once per search (per level: ground and bridge deck are separate);
//  - the open list is the original's binary heap, and the last neighbour created while expanding a
//    node is held back and compared with the heap top instead of being pushed (ties go to the heap);
//  - a goal cell that can't be entered ends the search at the cell next to it.
// Not here yet: the hierarchical corridor (pass a corridor callback to restrict the search), the bridge
// direction costs (FindBridgeDir) and the two path post-processing passes (0x42B210, 0x42B7F0).

#include <cstdint>
#include <functional>
#include <vector>

namespace ra::sim::path {

// The result of CanEnterCell (the Move enum). 7 and above can't be entered.
enum class Move : int {
	Ok = 0, Cloak = 1, MovingBlock = 2, ClosedGate = 3, FriendlyDestroyable = 4, Destroyable = 5, Temp = 6, No = 7,
};

struct Cell {
	int x = 0, y = 0;
	friend bool operator==(const Cell&, const Cell&) = default;
};

// The 8 directions: 0 N (0,-1), 1 NE (1,-1), 2 E (1,0), 3 SE (1,1), 4 S (0,1), 5 SW (-1,1), 6 W (-1,0), 7 NW (-1,-1).
// Direction 8 is a tube/tunnel jump.
Cell step(Cell c, int dir);

struct SearchMap {
	virtual ~SearchMap() = default;
	// False for cells outside the map (no cell there).
	virtual bool exists(Cell c) const = 0;
	virtual int level(Cell c) const = 0;           // CellClass::Level
	virtual bool bridge(Cell c) const = 0;         // cell flag 0x100: has a bridge deck
	virtual bool slow(Cell c) const { (void)c; return false; } // cell flag 0x40000: costs four times as much
	// The exit of a tube or tunnel that starts here, if any (cell +0x116).
	virtual bool tube_exit(Cell c, Cell& exit) const { (void)c; (void)exit; return false; }
	// FootClass::CanEnterCell for the unit searching (vslot 0x1AC): moving into 'to' in direction 'dir'
	// at height level 'level', from 'from'.
	virtual Move can_enter(Cell to, int dir, int level, Cell from) const = 0;
	// The cost of a MovingBlock cell: 1 normally; 4 when the chain of blocking units (up to 10, along their
	// facings) ends in a unit that isn't moving; 1000 in FindMode 2. The default treats it as moving.
	virtual float moving_block_cost(Cell to, bool bridge_level) const { (void)to; (void)bridge_level; return 1.0f; }
	// The hierarchical corridor: may the search expand into this cell? Default: everywhere.
	virtual bool in_corridor(Cell c) const { (void)c; return true; }
};

struct Query {
	Cell start, goal;
	int start_level = 0, goal_level = 0; // cell levels, +4 on a bridge deck (the caller decides)
	int max_steps = -1;                  // nodes to expand; -1 = the default 0xFFF7
	float path_cost_factor = 1.0f;       // AStarClass +0x4
	bool train = false;                  // IsTrain: anything below Move::No costs as Ok
	bool find_mode_2 = false;            // FindMode 2 (MovingBlock costs 1000), passed to the map
};

struct Result {
	bool found = false;
	Cell start;
	std::vector<int> directions; // from the start, one per step; 8 = tube jump
	std::vector<int> levels;     // the level of the cell reached by each step
	float cost = 0.0f;           // g of the last node
	int expanded = 0;            // nodes expanded
};

Result find_path(const SearchMap& map, const Query& query);

} // namespace ra::sim::path
