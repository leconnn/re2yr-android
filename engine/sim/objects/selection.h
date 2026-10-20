#pragma once

// The player's selection (P3 section 15): a set of own objects that orders go to.
//
// Selection is local to a player and is not part of the synchronised game state; only the orders it
// produces are (as events). Clicks and boxes arrive as world positions in leptons, so any input
// method (mouse, touch) can drive it.

#include "sim/objects/simulation.h"

#include <vector>

namespace ra::sim {

class Selection {
public:
	const std::vector<ObjectId>& ids() const { return ids_; }
	bool empty() const { return ids_.empty(); }
	bool contains(ObjectId id) const;
	void clear() { ids_.clear(); }

	// Selects the single own, selectable object nearest to a point within 'radius' leptons (or clears
	// the selection when there is none). 'add' keeps the current selection (shift-click toggles).
	void click(const Simulation& sim, int house, Coord at, int radius = 192, bool add = false);
	// Box select: every own selectable unit or infantry inside the rectangle. Buildings are only picked
	// by a click, as in the original.
	void box(const Simulation& sim, int house, Coord corner_a, Coord corner_b, bool add = false);

	// The events for a move order to a cell for every selected, own, movable object, in selection order.
	std::vector<Event> move_orders(const Simulation& sim, int house, Cell destination) const;
	std::vector<Event> stop_orders(const Simulation& sim, int house) const;
	std::vector<Event> guard_orders(const Simulation& sim, int house) const;

private:
	static bool selectable(const Simulation& sim, const Object& o, int house);
	void add_id(ObjectId id);

	std::vector<ObjectId> ids_;
};

} // namespace ra::sim
