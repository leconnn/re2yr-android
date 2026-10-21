#include "sim/objects/simulation.h"

#include "core/fastmath.h"
#include "core/fpu.h"
#include "ini/ini.h"
#include "sim/objects/drive_tracks.h"

#include <algorithm>
#include <cstdlib>

namespace ra::sim {

namespace {

using ra::sim::path::Move;

constexpr int kDx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
constexpr int kDy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

// The infantry spots inside a cell, from the cell's corner (table at 0x89E9F0, set by 0x48E480).
// Spot 1 exists in the table but is never used: the spot of a point is 0, 2, 3 or 4 (0x4810A0).
constexpr int kSpotX[5] = { 128, 64, 192, 64, 192 };
constexpr int kSpotY[5] = { 128, 64, 64, 192, 192 };

// The order in which spots are tried (0x81CC84 by starting spot; 0x81CC98, four rotations picked at
// random, when starting from the centre). Entries 0 and 1 are skipped.
constexpr int kSpotOrder[5][4] = { { 1, 2, 3, 4 }, { 0, 2, 3, 4 }, { 0, 1, 4, 3 }, { 0, 1, 4, 2 }, { 0, 2, 3, 1 } };
constexpr int kSpotRotations[4][4] = { { 1, 2, 3, 4 }, { 2, 3, 4, 1 }, { 3, 4, 1, 2 }, { 4, 1, 2, 3 } };

Coord spot_coord(Cell c, int spot)
{
	if (spot < 0 || spot > 4)
		spot = 0;
	return { c.x * kLeptonsPerCell + kSpotX[spot], c.y * kLeptonsPerCell + kSpotY[spot], 0 };
}

int ground_z(const ra::world::World& w, Coord c)
{
	const Cell cell = ra::cell_of(c);
	const ra::world::MapCell& mc = w.grid.at(cell);
	return ra::world::floor_height(mc.level, mc.slope, c.x - cell.x * kLeptonsPerCell, c.y - cell.y * kLeptonsPerCell);
}

// Track point facings follow the turn track's flags like the points (RA1 DriveClass): transposing mirrors
// the facing to W - f, negating x gives -f, negating y gives S - f (in 256 steps).
int transform_facing(int facing, int flag)
{
	if (flag & 1)
		facing = 192 - facing;
	if (flag & 2)
		facing = -facing;
	if (flag & 4)
		facing = 128 - facing;
	return facing & 0xFF;
}

} // namespace

// The search map of one unit: the world as that unit's pathfinding sees it.
class UnitSearchMap final : public ra::sim::path::SearchMap {
public:
	UnitSearchMap(const Simulation& sim, const Object& unit) : sim_(sim), unit_(unit) {}
	bool exists(ra::sim::path::Cell c) const override { return sim_.world_->grid.at(c.x, c.y).exists; }
	int level(ra::sim::path::Cell c) const override { return sim_.world_->grid.at(c.x, c.y).level; }
	bool bridge(ra::sim::path::Cell) const override { return false; }
	Move can_enter(ra::sim::path::Cell to, int dir, int, ra::sim::path::Cell from) const override
	{
		return sim_.can_enter(unit_, Cell { int16_t(to.x), int16_t(to.y) }, dir, Cell { int16_t(from.x), int16_t(from.y) });
	}

private:
	const Simulation& sim_;
	const Object& unit_;
};

Simulation::Simulation(const ra::world::World& world, ra::Random2& random)
    : world_(&world), random_(&random), occupancy_(size_t(ra::world::MapGrid::kSize) * ra::world::MapGrid::kSize), blocked_(size_t(ra::world::MapGrid::kSize) * ra::world::MapGrid::kSize, 0)
{
	land_speeds_.load(world.rules);
	// Trees and rocks block the cell they stand in.
	for (const auto& t : world.terrain_objects)
		if (ra::world::MapGrid::in_grid(t.x, t.y))
			blocked_[cell_index({ int16_t(t.x), int16_t(t.y) })] = 1;
}

int Simulation::house_index(const std::string& name)
{
	const std::string key = ra::ini::to_lower(name);
	for (size_t i = 0; i < houses_.size(); ++i)
		if (ra::ini::to_lower(houses_[i].name) == key)
			return static_cast<int>(i);
	houses_.push_back({ name, true });
	return static_cast<int>(houses_.size()) - 1;
}

ObjectId Simulation::spawn(ra::rules::TypeKind kind, int type, int house, Cell cell, Facing facing, int health, int sub_cell)
{
	Object o;
	o.id = static_cast<ObjectId>(objects_.size()) + 1;
	o.kind = kind;
	o.type = type;
	o.house = house;
	switch (kind) {
	case ra::rules::TypeKind::Infantry: o.what = What::Infantry; break;
	case ra::rules::TypeKind::Aircraft: o.what = What::Aircraft; break;
	case ra::rules::TypeKind::Building: o.what = What::Building; break;
	default: o.what = What::Unit; break;
	}
	// The type's data, read once.
	int found = -1;
	for (size_t i = 0; i < types_.size(); ++i)
		if (types_[i].kind == kind && types_[i].index == type)
			found = static_cast<int>(i);
	if (found < 0) {
		types_.push_back(TypeInfo::read(world_->rules, kind, type));
		found = static_cast<int>(types_.size()) - 1;
	}
	o.type_info = found;
	const TypeInfo& t = types_[static_cast<size_t>(found)];
	o.health = std::max(1, t.strength * std::clamp(health, 0, 256) / 256);
	// Both facings turn at the type's ROT (TechnoType +0x71C).
	o.primary_facing.set_rot(t.rot);
	o.secondary_facing.set_rot(t.rot);
	o.primary_facing.desired = o.primary_facing.start = facing.raw;
	o.secondary_facing.desired = o.secondary_facing.start = facing.raw;
	o.primary_facing.timer_start = o.secondary_facing.timer_start = static_cast<int32_t>(frame_);
	o.body = facing;
	o.turret = facing;
	o.sub_cell = (sub_cell == 1) ? 0 : std::clamp(sub_cell, 0, 4);
	o.location = kind == ra::rules::TypeKind::Infantry ? spot_coord(cell, o.sub_cell) : cell_center(cell);
	o.location.z = ground_z(*world_, o.location);
	o.foot.path.fill(-1);
	o.foot.current_cell = cell;
	objects_.push_back(o);

	if (kind == ra::rules::TypeKind::Building) {
		// A building blocks the cells of its foundation.
		int fw = 1, fh = 1;
		const std::string name = world_->rules.image_name(world_->rules.object(kind, type));
		const std::string foundation = world_->rules.art().get_string(name, "Foundation");
		if (!foundation.empty())
			std::sscanf(foundation.c_str(), "%dx%d", &fw, &fh);
		for (int y = 0; y < fh; ++y)
			for (int x = 0; x < fw; ++x)
				if (ra::world::MapGrid::in_grid(cell.x + x, cell.y + y))
					blocked_[cell_index({ int16_t(cell.x + x), int16_t(cell.y + y) })] = 1;
	} else {
		occupy(objects_.back(), cell);
	}
	return o.id;
}

void Simulation::spawn_placed_objects()
{
	for (const auto& p : world_->placed_objects) {
		const int h = house_index(p.owner);
		spawn(p.kind, p.type, h, { int16_t(p.x), int16_t(p.y) }, Facing { uint16_t(p.facing << 8) }, p.health, p.sub_cell);
	}
}

Object* Simulation::find(ObjectId id)
{
	if (id < 1 || id > static_cast<ObjectId>(objects_.size()))
		return nullptr;
	return &objects_[static_cast<size_t>(id) - 1];
}

const Object* Simulation::find(ObjectId id) const
{
	if (id < 1 || id > static_cast<ObjectId>(objects_.size()))
		return nullptr;
	return &objects_[static_cast<size_t>(id) - 1];
}

ObjectId Simulation::occupant(Cell cell, int spot) const
{
	if (!ra::world::MapGrid::in_grid(cell.x, cell.y) || spot < 0 || spot > 4)
		return 0;
	const CellOccupancy& c = occupancy_[cell_index(cell)];
	if (spot == 0 && c.vehicle)
		return c.vehicle;
	return c.spot[static_cast<size_t>(spot)];
}

std::vector<ObjectId> Simulation::objects_in_rect(Coord min, Coord max) const
{
	std::vector<ObjectId> out;
	for (const Object& o : objects_)
		if (o.alive && !o.in_limbo && o.location.x >= min.x && o.location.x <= max.x && o.location.y >= min.y && o.location.y <= max.y)
			out.push_back(o.id);
	return out;
}

void Simulation::occupy(const Object& o, Cell c)
{
	if (!ra::world::MapGrid::in_grid(c.x, c.y))
		return;
	CellOccupancy& occ = occupancy_[cell_index(c)];
	if (o.kind == ra::rules::TypeKind::Infantry)
		occ.spot[static_cast<size_t>(std::clamp(o.sub_cell, 0, 4))] = o.id;
	else
		occ.vehicle = o.id;
}

void Simulation::vacate(const Object& o, Cell c)
{
	if (!ra::world::MapGrid::in_grid(c.x, c.y))
		return;
	CellOccupancy& occ = occupancy_[cell_index(c)];
	if (occ.vehicle == o.id)
		occ.vehicle = 0;
	for (ObjectId& s : occ.spot)
		if (s == o.id)
			s = 0;
}

bool Simulation::cell_has_vehicle(Cell c, ObjectId except) const
{
	const ObjectId v = occupancy_[cell_index(c)].vehicle;
	return v != 0 && v != except;
}

int Simulation::spot_of(Coord c)
{
	const int x = c.x & 0xFF, y = c.y & 0xFF;
	const double dx = double(x - 0x80), dy = double(y - 0x80);
	if (ra::fpu::ftol(ra::fastmath::sqrt(dy * dy + dx * dx)) < 0x3C)
		return 0;
	int s = (x > 0x80 ? 1 : 0) | (y > 0x80 ? 2 : 0);
	return s == 0 ? 0 : s + 1;
}

bool Simulation::find_free_spot(Cell cell, Coord near, Coord& out)
{
	if (!ra::world::MapGrid::in_grid(cell.x, cell.y))
		return false;
	const CellOccupancy& occ = occupancy_[cell_index(cell)];
	if (occ.vehicle != 0)
		return false; // a unit holds the cell
	auto free = [&](int s) { return occ.spot[static_cast<size_t>(s)] == 0; };
	const int first = spot_of(near);
	const int* order;
	if (first == 0) {
		order = kSpotRotations[random_->ranged(0, 3)];
	} else {
		if (first != 1 && free(first)) {
			out = spot_coord(cell, first);
			out.z = ground_z(*world_, out);
			return true;
		}
		order = kSpotOrder[first];
	}
	for (int i = 0; i < 4; ++i) {
		const int s = order[i];
		if (s == 0 || s == 1 || !free(s))
			continue;
		out = spot_coord(cell, s);
		out.z = ground_z(*world_, out);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Orders

void Simulation::order_move(Object& o, Cell destination)
{
	FootState& f = o.foot;
	o.mission = Mission::Move;
	f.destination = cell_center(destination);
	f.has_destination = true;
	f.path.fill(-1);
	f.path_length = 0;
	f.path_delay = 0;
}

void Simulation::order_stop(Object& o)
{
	FootState& f = o.foot;
	f.has_destination = false;
	f.path.fill(-1);
	f.path_length = 0;
	// A unit on a track finishes the track: it can only stop on a cell centre.
	o.mission = Mission::Guard;
}

void Simulation::order_guard(Object& o)
{
	order_stop(o);
	o.mission = Mission::Guard;
}

Event Simulation::move_event(int house, ObjectId unit, Cell destination)
{
	MegaMission m;
	m.whom = Target::object(unit);
	m.mission = static_cast<uint8_t>(Mission::Move);
	m.destination = Target::cell(destination.x, destination.y);
	return m.to_event(house);
}

Event Simulation::stop_event(int house, ObjectId unit)
{
	MegaMission m;
	m.whom = Target::object(unit);
	m.mission = static_cast<uint8_t>(Mission::Stop);
	return m.to_event(house);
}

Event Simulation::guard_event(int house, ObjectId unit)
{
	MegaMission m;
	m.whom = Target::object(unit);
	m.mission = static_cast<uint8_t>(Mission::Guard);
	return m.to_event(house);
}

void Simulation::execute(const Event& event)
{
	switch (event.type) {
	case EventType::MegaMission:
	case EventType::MegaMissionF: {
		const MegaMission m = MegaMission::from_event(event);
		Object* o = find(m.whom.id);
		if (!o || !o->alive || o->house != event.house)
			return;
		switch (static_cast<Mission>(m.mission)) {
		case Mission::Move:
			if (m.destination.is_cell())
				order_move(*o, { int16_t(m.destination.cell_x()), int16_t(m.destination.cell_y()) });
			break;
		case Mission::Stop: order_stop(*o); break;
		case Mission::Guard:
		case Mission::AreaGuard: order_guard(*o); break;
		default: break; // the other missions come with their phases
		}
		break;
	}
	default:
		break; // the other event types come with their phases
	}
}

// ---------------------------------------------------------------------------------------------
// CanEnterCell and pathfinding

bool Simulation::level_rule(Cell from, Cell to, int unit_level) const
{
	// vslot 0x1B0 (0x4D9C60), without bridge decks (not loaded yet): equal levels pass; a step of one
	// level needs a ramp in the lower cell (the cell left when climbing, the cell entered when
	// descending); any other difference (cliffs) fails.
	const ra::world::MapCell& t = world_->grid.at(to);
	const ra::world::MapCell& f = world_->grid.at(from);
	const int d = unit_level - int(t.level);
	if (d == 0)
		return true;
	if (std::abs(d) == 1)
		return d < 1 ? f.slope != 0 : t.slope != 0;
	return false;
}

Move Simulation::can_enter(const Object& o, Cell to, int dir, Cell from) const
{
	// Unit 0x73F0A0 / Infantry 0x51BF90 (U section 10, 4B-fix). Implemented: tubes (none yet), the
	// height rule, the playable edge, buildings and trees, other objects, infantry spots, the land speed.
	const ra::world::MapCell& c = world_->grid.at(to);
	if (!c.exists || !ra::world::MapGrid::in_grid(to.x, to.y))
		return Move::No;
	if (dir == 8)
		return Move::No; // no tubes or tunnels yet
	const bool infantry = o.kind == ra::rules::TypeKind::Infantry;
	const int unit_level = world_->grid.at(from).level;
	if (!level_rule(from, to, unit_level))
		return Move::No;
	// An object inside the playable area may not leave it.
	if (world_->grid.in_local(from.x, from.y) && !world_->grid.in_local(to.x, to.y))
		return Move::No;
	if (blocked_terrain(to))
		return Move::No; // a building or a tree
	if (c.overlay >= 0 && static_cast<size_t>(c.overlay) < world_->overlays.size() && world_->overlays[static_cast<size_t>(c.overlay)].wall)
		return Move::No; // walls (crushing walls and attacking them come with combat)

	int result = 0;
	const CellOccupancy& occ = occupancy_[cell_index(to)];
	// A unit in the cell.
	if (occ.vehicle != 0 && occ.vehicle != o.id) {
		const Object* v = find(occ.vehicle);
		if (v) {
			const bool moving = v->foot.track_number != -1 || v->foot.has_destination;
			if (allied(v->house, o.house)) {
				if (moving) {
					// Two units heading straight at each other within 512 leptons can't pass.
					if (!infantry) {
						const int mine = o.body.dir8();
						const int theirs = v->body.dir8();
						const int64_t dx = v->location.x - o.location.x, dy = v->location.y - o.location.y;
						if (((theirs + 4) & 7) == mine && ra::fpu::ftol(ra::fastmath::sqrt(double(dx * dx + dy * dy))) < 0x200)
							return Move::No;
					}
					result = std::max(result, 2); // MovingBlock
				} else {
					result = std::max(result, 6); // Temp
				}
			} else {
				result = std::max(result, 5); // Destroyable
			}
		}
	}
	// Infantry in the cell.
	int infantry_here = 0, stationary_allies = 0;
	int owner = -1;
	for (int s : { 2, 3, 4, 0 }) {
		const ObjectId id = occ.spot[static_cast<size_t>(s)];
		if (id == 0 || id == o.id)
			continue;
		const Object* i = find(id);
		if (!i)
			continue;
		++infantry_here;
		owner = i->house;
		if (!i->foot.has_destination && !i->foot.has_head_to)
			++stationary_allies;
	}
	if (owner >= 0) {
		if (allied(owner, o.house)) {
			if (infantry) {
				const bool full = occ.spot[2] && occ.spot[3] && occ.spot[4];
				if (full && result < 2)
					return stationary_allies == 3 ? Move::Temp : Move::MovingBlock;
			} else {
				result = std::max(result, 6); // a unit waits for friendly infantry to move away
			}
		} else {
			const TypeInfo& t = info(o);
			if (!infantry && t.crusher) {
				// Crushable enemy infantry: the cell can be entered (crushing comes with combat).
			} else {
				result = std::max(result, 5);
			}
		}
	}
	(void)infantry_here;
	if (land_speeds_.multiplier(c.land, info(o).speed_type) == 0.0)
		return Move::No;
	if (infantry && result == 0 && occ.spot[2] && occ.spot[3] && occ.spot[4])
		return Move::No;
	return static_cast<Move>(result);
}

bool Simulation::find_path(Object& o, Cell goal)
{
	UnitSearchMap map(*this, o);
	ra::sim::path::Query q;
	const Cell from = o.cell();
	q.start = { from.x, from.y };
	q.goal = { goal.x, goal.y };
	q.start_level = world_->grid.at(from).level;
	q.goal_level = world_->grid.at(goal).level;
	const ra::sim::path::Result r = ra::sim::path::find_path(map, q);
	FootState& f = o.foot;
	f.path.fill(-1);
	f.path_length = 0;
	if (!r.found)
		return false;
	const int n = std::min<int>(static_cast<int>(r.directions.size()), kMaxPath - 1);
	for (int i = 0; i < n; ++i)
		f.path[static_cast<size_t>(i)] = r.directions[static_cast<size_t>(i)];
	f.path_length = n;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Movement

int Simulation::current_speed(const Object& o) const
{
	const TypeInfo& t = info(o);
	// ftol(Type.Speed * HouseType.SpeedXMult * this.SpeedMultiplier), both multipliers 1.0 here, then * SpeedPercentage.
	const int base = ra::fpu::ftol(ra::fpu::mul(ra::fpu::mul(double(t.speed), 1.0), 1.0));
	return ra::fpu::ftol(ra::fpu::mul(double(base), o.foot.speed_percentage));
}

double Simulation::speed_target(const Object& o, Cell head) const
{
	// StartOfMove (0x4B2630): the land speed of the cell being entered (capped at 1.0), times the slope
	// factors for units going up or down hill, 0.5 when that comes out 0, and 75% when damaged.
	const TypeInfo& t = info(o);
	const ra::world::MapCell& hc = world_->grid.at(head);
	double target = std::min(1.0, land_speeds_.multiplier(hc.land, t.speed_type));
	if (o.what == What::Unit) {
		const int here = o.location.z;
		const int there = ground_z(*world_, cell_center(head));
		const ra::rules::RuleObject general = world_->rules.section("General");
		const bool tracked = t.speed_type == SpeedType::Track;
		if (here < there)
			target = ra::fpu::mul(target, general.get_double(tracked ? "TrackedUphill" : "WheeledUphill"));
		else if (there < here)
			target = ra::fpu::mul(target, general.get_double(tracked ? "TrackedDownhill" : "WheeledDownhill"));
	}
	if (target == 0.0)
		target = 0.5;
	const double health_ratio = t.strength > 0 ? double(o.health) / double(t.strength) : 1.0;
	if (health_ratio <= world_->rules.section("AudioVisual").get_double("ConditionYellow"))
		target = ra::fpu::mul(target, 0.75);
	return target;
}

void Simulation::enter_cell(Object& o, Cell c)
{
	const Cell old = o.foot.current_cell;
	if (old != c) {
		vacate(o, old);
		occupy(o, c);
		o.foot.current_cell = c;
	}
	if (o.house == local_house_)
		reveal_around(o);
}

void Simulation::reveal_around(const Object& o)
{
	const TypeInfo& t = info(o);
	if (t.sight > 0)
		shroud_.reveal_area(o.cell().x, o.cell().y, t.sight);
}

void Simulation::update_objects()
{
	for (Object& o : objects_) {
		if (!o.alive || o.in_limbo)
			continue;
		if (o.what == What::Unit || o.what == What::Infantry)
			update_foot(o);
		sync_facing(o);
	}
	++frame_;
}

void Simulation::update_foot(Object& o)
{
	switch (info(o).locomotor) {
	case LocomotorKind::Drive:
	case LocomotorKind::Ship:
		drive_process(o);
		break;
	case LocomotorKind::Walk:
	case LocomotorKind::Mech:
		walk_process(o);
		break;
	default:
		break; // the other locomotors come later
	}
}

void Simulation::drive_process(Object& o)
{
	FootState& f = o.foot;
	if (f.track_number != -1) {
		while_moving(o);
		return;
	}
	if (!f.has_destination) {
		f.speed_accum = 0;
		return;
	}
	const Cell here = o.cell();
	const Cell dest = ra::cell_of(f.destination);
	if (here == dest) {
		// Arrived: Destination == Location.
		f.has_destination = false;
		f.path.fill(-1);
		if (o.mission == Mission::Move)
			o.mission = Mission::Guard;
		return;
	}
	// Turning in place: wait while the body rotates.
	if (o.primary_facing.rotating(frame_)) {
		f.rotating = true;
		return;
	}
	f.rotating = false;
	if (!f.has_path()) {
		if (f.path_delay > 0) {
			--f.path_delay;
			return;
		}
		if (!find_path(o, dest)) {
			// PathDelay = ftol(Rules.PathDelay * 900) frames before the next try.
			f.path_delay = std::max(1, ra::fpu::ftol(ra::fpu::mul(world_->rules.section("General").get_double("PathDelay"), 900.0)));
			return;
		}
		// The search may end next to a blocked goal; with no step left the unit is as near as it gets.
		if (!f.has_path()) {
			f.has_destination = false;
			return;
		}
	}
	start_of_move(o);
}

bool Simulation::begin_track(Object& o, int track, Cell head, int steps_used)
{
	FootState& f = o.foot;
	for (int k = 0; k < steps_used; ++k) {
		for (int i = 0; i + 1 < kMaxPath; ++i)
			f.path[static_cast<size_t>(i)] = f.path[static_cast<size_t>(i) + 1];
		f.path[kMaxPath - 1] = -1;
	}
	f.head_to = cell_center(head);
	f.has_head_to = true;
	f.track_number = track;
	f.track_index = 0;
	f.on_short_track = false;
	occupy(o, head); // the cell driven to is reserved from the start of the move
	if (track < 0x40)
		f.target_speed = speed_target(o, head);
	return true;
}

void Simulation::start_of_move(Object& o)
{
	// Drive StartOfMove (0x4B2630).
	FootState& f = o.foot;
	const int next = f.path[0];
	if (next < 0 || next > 7) {
		f.path.fill(-1); // tube steps (8) are not supported yet
		return;
	}
	const Cell from = o.cell();
	const Cell first { int16_t(from.x + kDx[next]), int16_t(from.y + kDy[next]) };

	// The body must face the first step exactly; otherwise turn toward it and wait (Do_Turn).
	const uint16_t want = static_cast<uint16_t>(next << 13);
	if (o.primary_facing.current(frame_) != want) {
		o.primary_facing.set_desired(want, frame_);
		f.rotating = true;
		return;
	}

	Move m = can_enter(o, first, next, from);
	const TypeInfo& t = info(o);
	if (m == Move::FriendlyDestroyable || m == Move::Destroyable)
		if (t.crusher && world_->grid.at(first).overlay < 0)
			m = Move::Ok;
	if (m != Move::Ok) {
		if (m == Move::No) {
			f.path.fill(-1); // find another way next frame
			f.path_delay = 0;
		} else if (++f.path_delay > 30) {
			// Blocked by something that should clear: after a while look for another way.
			f.path.fill(-1);
			f.path_delay = 0;
		}
		return;
	}
	f.path_delay = 0;

	// The track: from the facing of the first step toward the second step (TurnTrack[second + first * 8]),
	// or straight ahead when there is no second step or no turn track for that pair.
	int second = f.path[1];
	if (second < 0 || second > 7)
		second = next;
	int track = second + next * 8;
	if (drive_tracks::turn_track(track).track == 0)
		track = next * 9;
	if ((drive_tracks::turn_track(track).flag & 8) == 0) {
		begin_track(o, track, first, 1);
		return;
	}
	// A curved track ends in the cell after the next one: check it first.
	const Cell after { int16_t(first.x + kDx[second]), int16_t(first.y + kDy[second]) };
	Move m2 = can_enter(o, after, second, first);
	if (m2 == Move::FriendlyDestroyable || m2 == Move::Destroyable)
		if (t.crusher && world_->grid.at(after).overlay < 0)
			m2 = Move::Ok;
	if (m2 == Move::Ok) {
		occupy(o, first);
		begin_track(o, track, after, 2);
		return;
	}
	// Blocked further on: drive the straight track into the first cell instead.
	begin_track(o, next * 9, first, 1);
}

void Simulation::while_moving(Object& o)
{
	FootState& f = o.foot;
	const TypeInfo& t = info(o);

	// Acceleration (types with Accelerates=yes): U section 10.2.
	if (t.accelerates && f.track_number < 0x40) {
		// The distance to the order's destination (Foot+0x34), not to the cell being driven to.
		const Coord goal = f.has_destination ? f.destination : f.head_to;
		const double dx = double(goal.x - o.location.x), dy = double(goal.y - o.location.y);
		const double d = ra::fastmath::sqrt(dx * dx + dy * dy);
		double sp = f.speed_percentage;
		const double default_speed = double(t.speed);
		if (d < double(t.slowdown_distance)) {
			sp = ra::fpu::sub(sp, ra::fpu::mul(default_speed, t.deceleration_factor));
			sp = std::max(sp, 0.3);
		} else {
			if (sp < f.target_speed)
				sp = std::min(ra::fpu::add(sp, t.acceleration_factor), f.target_speed);
			if (sp > f.target_speed)
				sp = std::max(ra::fpu::sub(sp, ra::fpu::mul(default_speed, t.deceleration_factor)), f.target_speed);
		}
		f.speed_percentage = sp;
	} else if (!t.accelerates) {
		f.speed_percentage = f.target_speed;
	}

	int accum = current_speed(o) + f.speed_accum;
	while (accum > 7) {
		const drive_tracks::TurnTrack& turn = drive_tracks::turn_track(f.track_number);
		const drive_tracks::RawTrack& track = drive_tracks::raw_track(f.on_short_track ? turn.start_track : turn.track);
		accum -= 7;
		const drive_tracks::Point p = track.points[static_cast<size_t>(std::min(f.track_index, track.count - 1))];
		if (p.x == 0 && p.y == 0 && f.track_index != 0) {
			// The end of the track: re-synchronise on the cell centre.
			accum += ra::fpu::ftol(double(std::abs(f.head_to.x - o.location.x) + std::abs(f.head_to.y - o.location.y)));
			o.location.x = f.head_to.x;
			o.location.y = f.head_to.y;
			o.location.z = ground_z(*world_, o.location);
			const Cell before_cell = f.current_cell;
			f.track_number = -1;
			f.track_index = 0;
			f.has_head_to = false;
			// A curved track passed through a middle cell: free it.
			const Cell here = ra::cell_of(o.location);
			for (int dir = 0; dir < 8; ++dir) {
				const Cell n { int16_t(here.x + kDx[dir]), int16_t(here.y + kDy[dir]) };
				if (n != before_cell && ra::world::MapGrid::in_grid(n.x, n.y) && occupancy_[cell_index(n)].vehicle == o.id)
					occupancy_[cell_index(n)].vehicle = 0;
			}
			enter_cell(o, here);
			// Chain the next track within the same frame.
			if (f.has_destination && ra::cell_of(f.destination) != o.cell() && f.has_path())
				start_of_move(o);
			if (f.track_number == -1)
				break;
			continue;
		}
		const drive_tracks::Point q = drive_tracks::transform(p, turn.flag);
		o.location.x = f.head_to.x + q.x;
		o.location.y = f.head_to.y + q.y;
		o.location.z = ground_z(*world_, o.location);
		o.primary_facing.set_current(static_cast<uint16_t>(transform_facing(p.facing, turn.flag) << 8), frame_);
		// The occupation moves to the next cell at the track's Cell index (straight tracks switch at the end).
		if (f.track_index == track.cell) {
			const Cell now = ra::cell_of(o.location);
			if (now != f.current_cell) {
				if (occupancy_[cell_index(f.current_cell)].vehicle == o.id)
					occupancy_[cell_index(f.current_cell)].vehicle = 0;
				f.current_cell = now;
				occupy(o, now);
			}
		}
		++f.track_index;
	}
	f.speed_accum = accum;
}

void Simulation::walk_process(Object& o)
{
	// Walk (U section 10.3).
	FootState& f = o.foot;
	if (!f.has_head_to) {
		if (!f.has_destination)
			return;
		const Cell here = o.cell();
		const Cell dest = ra::cell_of(f.destination);
		if (here == dest) {
			f.has_destination = false;
			f.path.fill(-1);
			if (o.mission == Mission::Move)
				o.mission = Mission::Guard;
			return;
		}
		if (!f.has_path()) {
			if (f.path_delay > 0) {
				--f.path_delay;
				return;
			}
			if (!find_path(o, dest) || !f.has_path()) {
				f.path_delay = std::max(1, ra::fpu::ftol(ra::fpu::mul(world_->rules.section("General").get_double("PathDelay"), 900.0)));
				return;
			}
		}
		const int next = f.path[0];
		if (next < 0 || next > 7) {
			f.path.fill(-1);
			return;
		}
		const Cell to { int16_t(here.x + kDx[next]), int16_t(here.y + kDy[next]) };
		const Move m = can_enter(o, to, next, here);
		if (m == Move::No) {
			f.path.fill(-1);
			return;
		}
		if (m != Move::Ok) {
			if (++f.path_delay > 30) {
				f.path.fill(-1);
				f.path_delay = 0;
			}
			return;
		}
		// The free spot in the next cell, starting from the same spot the infantry stands in now.
		Coord spot;
		const Coord near { to.x * kLeptonsPerCell + (o.location.x & 0xFF), to.y * kLeptonsPerCell + (o.location.y & 0xFF), 0 };
		if (!find_free_spot(to, near, spot)) {
			if (++f.path_delay > 30) {
				f.path.fill(-1);
				f.path_delay = 0;
			}
			return;
		}
		f.path_delay = 0;
		for (int i = 0; i + 1 < kMaxPath; ++i)
			f.path[static_cast<size_t>(i)] = f.path[static_cast<size_t>(i) + 1];
		f.path[kMaxPath - 1] = -1;
		f.head_to = spot;
		f.has_head_to = true;
		vacate(o, f.current_cell);
		o.sub_cell = spot_of(spot);
		occupy(o, to);
		f.current_cell = to;
		// Face the spot (Do_Turn) and walk at full speed.
		const double dx = double(spot.x - o.location.x), dy = double(spot.y - o.location.y);
		const double angle = ra::fastmath::atan2(-dy, dx);
		const int facing = ra::fpu::ftol(ra::fpu::mul(ra::fpu::sub(angle, 1.5707963267948966), -10430.378350470453));
		o.primary_facing.set_desired(static_cast<uint16_t>(facing), frame_);
		f.speed_percentage = 1.0;
	}
	// Walk along the line to the spot; snap to it within 16 leptons.
	const double dx = double(f.head_to.x - o.location.x), dy = double(f.head_to.y - o.location.y);
	const double d = ra::fastmath::sqrt(dx * dx + dy * dy);
	const int speed = std::max(1, current_speed(o));
	if (d <= 16.0 || d <= double(speed)) {
		o.location.x = f.head_to.x;
		o.location.y = f.head_to.y;
		o.location.z = ground_z(*world_, o.location);
		f.has_head_to = false;
		if (o.house == local_house_)
			reveal_around(o);
		return;
	}
	o.location.x += ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(dx, double(speed)), d));
	o.location.y += ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(dy, double(speed)), d));
	o.location.z = ground_z(*world_, o.location);
}

// ---------------------------------------------------------------------------------------------
// Game loop hookup

void Simulation::attach(GameLoop& loop)
{
	loop.logic().set_step(LogicStep::LogicObjects, [this] { update_objects(); });
	loop.crc_terms = [this](FrameCrc& crc) { add_crc_terms(crc); };
}

void Simulation::add_crc_terms(FrameCrc& crc) const
{
	// P3 section 13.3: infantry, units, buildings, houses, the display layers, then the logic list.
	for (const Object& o : objects_)
		if (o.what == What::Infantry && o.alive)
			crc.add_position(o.location.x, o.location.y, o.body.crc_byte());
	for (const Object& o : objects_)
		if (o.what == What::Unit && o.alive)
			crc.add_position(o.location.x, o.location.y, o.body.crc_byte() + o.turret.crc_byte());
	for (const Object& o : objects_)
		if (o.what == What::Building && o.alive)
			crc.add_position(o.location.x, o.location.y, o.body.crc_byte());
	for (size_t i = 0; i < houses_.size(); ++i)
		crc.add(0); // MapIsClear
	// The layers and the logic list (every object is in the ground layer and the logic list for now).
	for (const Object& o : objects_)
		if (o.alive && !o.in_limbo)
			crc.add_position(o.location.x, o.location.y, static_cast<uint32_t>(o.what));
	for (const Object& o : objects_)
		if (o.alive && !o.in_limbo)
			crc.add_position(o.location.x, o.location.y, static_cast<uint32_t>(o.what));
}

} // namespace ra::sim
