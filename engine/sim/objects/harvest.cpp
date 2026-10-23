// Tiberium on the map and the harvester's work cycle (P3 section 8.4, U sections 9.7 and 9.9).
//
// PROVISIONAL (U section 15.4 gives the dock handshake only as message names): a harvester returns to the
// nearest refinery, stops on the cell south of the refinery's foundation, faces 0x4000 and dumps there.
// Ore growth and spread (P3 section 8.4) and veins are not here yet.

#include "core/fastmath.h"
#include "core/fpu.h"
#include "sim/objects/simulation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ra::sim {

namespace {

constexpr int kStages = 12;

// The Tiberium type an overlay belongs to, from its name: TIB01..20 Riparius, GEM Cruentus, TIB2_ Vinifera, TIB3_ Aboreus.
int tiberium_type_from_name(const std::string& name)
{
	if (name.rfind("GEM", 0) == 0)
		return 1;
	if (name.rfind("TIB2_", 0) == 0)
		return 2;
	if (name.rfind("TIB3_", 0) == 0)
		return 3;
	if (name.rfind("TIB", 0) == 0)
		return 0;
	return -1;
}

} // namespace

int Simulation::ore_type(Cell c) const
{
	if (!ra::world::MapGrid::in_grid(c.x, c.y))
		return -1;
	const int ov = overlay_[cell_index(c)];
	if (ov < 0 || size_t(ov) >= world_->overlays.size() || !world_->overlays[size_t(ov)].tiberium)
		return -1;
	return tiberium_type_from_name(world_->overlays[size_t(ov)].name);
}

int Simulation::ore_stage(Cell c) const { return ore_type(c) < 0 ? 0 : overlay_data_[cell_index(c)]; }

int Simulation::ore_value(Cell c) const
{
	const int t = ore_type(c);
	if (t < 0)
		return 0;
	const int value = world_->rules.object(ra::rules::TypeKind::Tiberium, t).get_int("Value");
	return value * (overlay_data_[cell_index(c)] + 1);
}

int Simulation::reduce_tiberium(Cell c, int n)
{
	if (ore_type(c) < 0)
		return 0;
	uint8_t& stage = overlay_data_[cell_index(c)];
	if (n < stage + 1) {
		stage = uint8_t(stage - n);
		return n;
	}
	const int had = stage + 1;
	overlay_[cell_index(c)] = -1;
	stage = 0;
	return had;
}

void Simulation::give_tiberium(int house, double amount, int type)
{
	House& h = houses_[size_t(house)];
	h.point_total += ra::fpu::ftol(ra::fpu::mul(amount, 5.0));
	const int value = world_->rules.object(ra::rules::TypeKind::Tiberium, std::max(0, type)).get_int("Value");
	h.balance += ra::fpu::ftol(ra::fpu::mul(ra::fpu::mul(double(value), h.econ.income_mult), amount));
}

double Simulation::storage_total(const Object& o) const
{
	double total = 0.0;
	for (double v : o.storage)
		total = ra::fpu::add(total, v);
	return total;
}

Cell Simulation::scan_for_tiberium(const Object& o, int radius) const
{
	// 0x4DD0A0: its own cell if it has ore; else square rings from 1 to radius - 1; the first ring with a
	// reachable ore cell wins, and in it the cell of highest value.
	const Cell here = o.cell();
	if (ore_type(here) >= 0)
		return here;
	for (int r = 1; r < radius; ++r) {
		Cell best { -1, -1 };
		int best_value = 0;
		for (int dy = -r; dy <= r; ++dy)
			for (int dx = -r; dx <= r; ++dx) {
				if (std::max(std::abs(dx), std::abs(dy)) != r)
					continue;
				const Cell c { int16_t(here.x + dx), int16_t(here.y + dy) };
				if (ore_type(c) < 0 || !world_->grid.in_local(c.x, c.y, true))
					continue;
				// 0x4DCE80: reachable (same zone) and free of other units.
				if (zones_.movement_zone({ c.x, c.y }, info(o).movement_zone) != zones_.movement_zone({ here.x, here.y }, info(o).movement_zone))
					continue;
				if (cell_has_vehicle(c, o.id) || blocked_terrain(c))
					continue;
				const int v = ore_value(c);
				if (v > best_value) {
					best_value = v;
					best = c;
				}
			}
		if (best.x >= 0)
			return best;
	}
	return { -1, -1 };
}

bool Simulation::unit_harvesting(Object& o)
{
	// UnitClass::Harvesting (0x73D450): one bale per step.
	const TypeInfo& t = info(o);
	if (o.foot.has_destination)
		return true; // still walking
	const double capacity = double(t.storage);
	if (!t.harvester || storage_total(o) >= capacity || ore_type(o.cell()) < 0) {
		o.load_start = int(frame_);
		o.load_delay = 0;
		return false;
	}
	const int type = ore_type(o.cell());
	const int amount = ra::fpu::ftol(std::min(1.0, ra::fpu::sub(capacity, storage_total(o))));
	const int got = reduce_tiberium(o.cell(), std::max(amount, 1));
	if (got > 0) {
		o.storage[size_t(type)] = ra::fpu::add(o.storage[size_t(type)], double(got));
		const ra::rules::RuleObject g = world_->rules.section("General");
		o.load_start = int(frame_);
		o.load_delay = g.has("HarvesterLoadRate") ? g.get_int("HarvesterLoadRate") : 2;
		return true;
	}
	return false;
}

ObjectId Simulation::nearest_dock(const Object& o) const
{
	// TryNearestDockBuilding: the nearest own building of a type in the unit's Dock list.
	const TypeInfo& t = info(o);
	ObjectId best = 0;
	double best_distance = 1e18;
	for (const Object& b : objects_) {
		if (!b.alive || b.what != What::Building || b.house != o.house || b.in_limbo)
			continue;
		bool listed = false;
		for (const std::string& name : t.docks)
			listed |= ra::ini::to_lower(name) == ra::ini::to_lower(info(b).name);
		if (!listed)
			continue;
		const double d = dist2d(o.location, target_coords(Target::object(b.id)));
		if (d < best_distance) {
			best_distance = d;
			best = b.id;
		}
	}
	return best;
}

int Simulation::harvest_mission(Object& o)
{
	// UnitClass::Mission_Harvest (0x73E5E0).
	const TypeInfo& t = info(o);
	const ra::rules::RuleObject g = world_->rules.section("General");
	const auto delay_harvest = [&] { return mission_delay(Mission::Harvest); };
	if (!t.harvester && !t.weeder)
		return 450;
	if (t.docks.empty()) {
		queue_mission(o, Mission::Guard, true);
		return 1;
	}
	const int long_scan = (g.has("TiberiumLongScan") ? g.get_leptons("TiberiumLongScan") : 8192) / 256;
	const int short_scan = (g.has("TiberiumShortScan") ? g.get_leptons("TiberiumShortScan") : 1536) / 256;
	const double capacity = double(t.storage);
	switch (o.mission_status) {
	case 0: { // find ore
		if (storage_total(o) >= capacity) {
			o.mission_status = 2;
			return delay_harvest();
		}
		if (o.has_archive) {
			order_move_to(o, ra::cell_of(o.archive));
			o.has_archive = false;
		}
		const Cell ore = scan_for_tiberium(o, long_scan);
		if (ore.x >= 0) {
			if (ore != o.cell())
				order_move_to(o, ore);
			o.is_harvesting = true;
			o.load_start = int(frame_);
			o.load_delay = 2;
			o.mission_status = 1;
			return delay_harvest();
		}
		if (!o.foot.has_destination) {
			// No ore: useless; the house is short of Tiberium.
			o.mission_status = 4;
			return 105;
		}
		return delay_harvest();
	}
	case 1: { // harvest
		if (int(frame_) - o.load_start < 9 && o.foot.has_destination)
			return delay_harvest();
		if (o.foot.has_destination)
			return delay_harvest();
		if (!unit_harvesting(o)) {
			if (storage_total(o) >= capacity) {
				const Cell nearby = scan_for_tiberium(o, short_scan);
				if (nearby.x >= 0) {
					o.archive = ra::cell_center(nearby);
					o.has_archive = true;
				}
				o.mission_status = 2;
				return delay_harvest();
			}
			const Cell ore = scan_for_tiberium(o, short_scan);
			if (ore.x >= 0) {
				if (ore != o.cell())
					order_move_to(o, ore);
				return delay_harvest();
			}
			if (!o.foot.has_destination)
				o.mission_status = 2;
		}
		return delay_harvest();
	}
	case 2: { // return to the refinery
		const ObjectId dock = nearest_dock(o);
		if (dock == 0) {
			o.mission_status = 4;
			return delay_harvest();
		}
		const Object* b = find(dock);
		const TypeInfo& bt = info(*b);
		// PROVISIONAL dock cell: just south of the foundation.
		const Cell cell { int16_t(b->cell().x + bt.foundation_w / 2), int16_t(b->cell().y + bt.foundation_h) };
		o.dock_building = dock;
		if (o.cell() != cell)
			order_move_to(o, cell);
		o.mission_status = 3;
		return delay_harvest();
	}
	case 3: { // dock and dump
		const Object* b = find(o.dock_building);
		if (!b || !b->alive) {
			o.mission_status = 2;
			return delay_harvest();
		}
		const TypeInfo& bt = info(*b);
		const Cell cell { int16_t(b->cell().x + bt.foundation_w / 2), int16_t(b->cell().y + bt.foundation_h) };
		if (o.foot.has_destination || o.foot.has_head_to || o.foot.track_number != -1)
			return delay_harvest();
		if (o.cell() != cell) {
			order_move_to(o, cell);
			return delay_harvest();
		}
		// Face 0x4000 and wait for the turn to finish.
		if (o.primary_facing.desired != 0x4000 || o.primary_facing.rotating(frame_)) {
			o.primary_facing.set_desired(0x4000, frame_);
			return 5;
		}
		o.mission_status = 5;
		o.load_start = int(frame_);
		return 1;
	}
	case 5: { // dumping: every HarvesterDumpRate * 900 frames (U section 9.9 state 3)
		const double rate = g.has("HarvesterDumpRate") ? g.get_double("HarvesterDumpRate") : 0.016;
		const int period = std::max(1, ra::fpu::ftol(ra::fpu::mul(rate, 900.0)));
		int type = -1;
		for (int i = 0; i < 4; ++i)
			if (o.storage[size_t(i)] > 0.0) {
				type = i;
				break;
			}
		if (type < 0) {
			o.mission_status = 0;
			o.dock_building = 0;
			return 1;
		}
		int purifiers = 0;
		for (const Object& b : objects_)
			if (b.alive && b.what == What::Building && b.house == o.house && !b.in_limbo && info(b).ore_purifier)
				++purifiers;
		const double bonus_rate = g.has("PurifierBonus") ? g.get_double("PurifierBonus") : 0.25;
		const double amount = o.storage[size_t(type)];
		const double bonus = ra::fpu::mul(ra::fpu::mul(double(purifiers), bonus_rate), amount);
		o.storage[size_t(type)] = 0.0;
		give_tiberium(o.house, amount, type);
		if (bonus > 0.0)
			give_tiberium(o.house, bonus, type);
		return period;
	}
	default: { // useless
		if (!o.foot.has_destination) {
			queue_mission(o, Mission::Guard, true);
			return 1;
		}
		return delay_harvest();
	}
	}
}

} // namespace ra::sim
