#include "sim/objects/selection.h"

#include <algorithm>
#include <cstdlib>

namespace ra::sim {

bool Selection::contains(ObjectId id) const
{
	return std::find(ids_.begin(), ids_.end(), id) != ids_.end();
}

void Selection::add_id(ObjectId id)
{
	if (!contains(id))
		ids_.push_back(id);
}

bool Selection::selectable(const Simulation& sim, const Object& o, int house)
{
	(void)sim;
	return o.alive && !o.in_limbo && o.house == house;
}

void Selection::click(const Simulation& sim, int house, Coord at, int radius, bool add)
{
	const Object* best = nullptr;
	int64_t best_d = int64_t(radius) * radius;
	for (const Object& o : sim.objects()) {
		if (!selectable(sim, o, house))
			continue;
		const int64_t dx = o.location.x - at.x, dy = o.location.y - at.y;
		const int64_t d = dx * dx + dy * dy;
		if (d <= best_d) {
			best_d = d;
			best = &o; // a later object wins a tie: it is drawn on top
		}
	}
	if (!add)
		ids_.clear();
	if (!best)
		return;
	if (add && contains(best->id))
		ids_.erase(std::find(ids_.begin(), ids_.end(), best->id));
	else
		add_id(best->id);
}

void Selection::box(const Simulation& sim, int house, Coord a, Coord b, bool add)
{
	const Coord lo { std::min(a.x, b.x), std::min(a.y, b.y), 0 }, hi { std::max(a.x, b.x), std::max(a.y, b.y), 0 };
	if (!add)
		ids_.clear();
	for (ObjectId id : sim.objects_in_rect(lo, hi)) {
		const Object* o = sim.find(id);
		if (o && selectable(sim, *o, house) && o->what != What::Building)
			add_id(id);
	}
}

std::vector<Event> Selection::move_orders(const Simulation& sim, int house, Cell destination) const
{
	std::vector<Event> out;
	for (ObjectId id : ids_) {
		const Object* o = sim.find(id);
		if (o && selectable(sim, *o, house) && o->what != What::Building)
			out.push_back(Simulation::move_event(house, id, destination));
	}
	return out;
}

std::vector<Event> Selection::stop_orders(const Simulation& sim, int house) const
{
	std::vector<Event> out;
	for (ObjectId id : ids_) {
		const Object* o = sim.find(id);
		if (o && selectable(sim, *o, house))
			out.push_back(Simulation::stop_event(house, id));
	}
	return out;
}

std::vector<Event> Selection::guard_orders(const Simulation& sim, int house) const
{
	std::vector<Event> out;
	for (ObjectId id : ids_) {
		const Object* o = sim.find(id);
		if (o && selectable(sim, *o, house))
			out.push_back(Simulation::guard_event(house, id));
	}
	return out;
}

} // namespace ra::sim
