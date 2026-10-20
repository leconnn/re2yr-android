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

// The lepton position of an infantry spot in a cell (PROVISIONAL: the original's spot table is not decoded).
Coord spot_coord(Cell c, int spot)
{
	static const int sx[5] = { 128, 64, 192, 64, 192 };
	static const int sy[5] = { 128, 64, 64, 192, 192 };
	if (spot < 0 || spot > 4)
		spot = 0;
	return { c.x * kLeptonsPerCell + sx[spot], c.y * kLeptonsPerCell + sy[spot], 0 };
}

int ground_z(const ra::world::World& w, Coord c)
{
	const Cell cell = ra::cell_of(c);
	const ra::world::MapCell& mc = w.grid.at(cell);
	return ra::world::floor_height(mc.level, mc.slope, c.x - cell.x * kLeptonsPerCell, c.y - cell.y * kLeptonsPerCell);
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
	o.body = facing;
	o.turret = facing;
	o.sub_cell = sub_cell;
	o.location = kind == ra::rules::TypeKind::Infantry ? spot_coord(cell, sub_cell) : cell_center(cell);
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
	return occupancy_[cell_index(cell)][static_cast<size_t>(spot)];
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
	const size_t spot = o.kind == ra::rules::TypeKind::Infantry ? static_cast<size_t>(std::clamp(o.sub_cell, 0, 4)) : 0;
	occupancy_[cell_index(c)][spot] = o.id;
}

void Simulation::vacate(const Object& o, Cell c)
{
	if (!ra::world::MapGrid::in_grid(c.x, c.y))
		return;
	auto& slots = occupancy_[cell_index(c)];
	for (ObjectId& s : slots)
		if (s == o.id)
			s = 0;
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
// Pathfinding and CanEnterCell

Move Simulation::can_enter(const Object& o, Cell to, int, Cell from) const
{
	// PROVISIONAL: the original's FootClass::CanEnterCell (vslot 0x1AC) is much longer. This covers
	// terrain speed, buildings and trees, cliffs, and other objects standing in the cell.
	const ra::world::MapCell& c = world_->grid.at(to);
	if (!c.exists || !ra::world::MapGrid::in_grid(to.x, to.y))
		return Move::No;
	if (blocked_terrain(to))
		return Move::No;
	const TypeInfo& t = info(o);
	if (land_speeds_.multiplier(c.land, t.speed_type) <= 0.0)
		return Move::No;
	const ra::world::MapCell& f = world_->grid.at(from);
	if (std::abs(int(c.level) - int(f.level)) >= 2 && c.slope == 0 && f.slope == 0)
		return Move::No;
	const auto& slots = occupancy_[cell_index(to)];
	if (o.kind == ra::rules::TypeKind::Infantry) {
		const size_t spot = static_cast<size_t>(std::clamp(o.sub_cell, 0, 4));
		(void)spot;
		int free_spots = 0;
		for (size_t i = 1; i <= 4; ++i)
			if (slots[i] == 0)
				++free_spots;
		if (slots[0] != 0 && slots[0] != o.id)
			return Move::Temp;
		return free_spots > 0 ? Move::Ok : Move::Temp;
	}
	for (ObjectId id : slots) {
		if (id == 0 || id == o.id)
			continue;
		const Object* other = find(id);
		if (!other)
			continue;
		const bool moving = other->foot.track_number != -1;
		return moving ? Move::MovingBlock : Move::Temp;
	}
	return Move::Ok;
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

bool Simulation::turn_towards(Object& o, Facing desired)
{
	// PROVISIONAL: a fixed rate of ROT << 8 binary-angle units per frame (FacingClass is not decoded).
	const int rate = std::max(1, info(o).rot) << 8;
	int diff = int(uint16_t(desired.raw - o.body.raw));
	if (diff > 0x8000)
		diff -= 0x10000;
	if (std::abs(diff) <= rate) {
		o.body = desired;
		return true;
	}
	o.body.raw = static_cast<uint16_t>(o.body.raw + (diff > 0 ? rate : -rate));
	return false;
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
		// The search may end next to a blocked goal; if it found no step the unit is already as near as it gets.
		if (!f.has_path()) {
			f.has_destination = false;
			return;
		}
	}
	start_of_move(o);
}

void Simulation::start_of_move(Object& o)
{
	FootState& f = o.foot;
	const int next = f.path[0];
	if (next < 0 || next > 7) {
		f.path.fill(-1);
		return;
	}
	// PROVISIONAL: only straight tracks. The body turns in place until it faces the next step.
	const Facing want { static_cast<uint16_t>(next << 13) };
	if (o.body.dir8() != next) {
		f.rotating = !turn_towards(o, want);
		if (f.rotating)
			return;
	}
	f.rotating = false;
	const Cell from = o.cell();
	const Cell to { int16_t(from.x + kDx[next]), int16_t(from.y + kDy[next]) };
	const Move m = can_enter(o, to, next, from);
	if (m == Move::No) {
		f.path.fill(-1); // repath next frame
		f.path_delay = 0;
		return;
	}
	if (m != Move::Ok) {
		// Something is in the way: wait a few frames, then look for another way round.
		if (++f.path_delay > 30) {
			f.path.fill(-1);
			f.path_delay = 0;
		}
		return;
	}
	f.path_delay = 0;
	// Pop the step and start the straight track for this direction (TurnTrack[cur * 9]).
	for (int i = 0; i + 1 < kMaxPath; ++i)
		f.path[static_cast<size_t>(i)] = f.path[static_cast<size_t>(i) + 1];
	f.path[kMaxPath - 1] = -1;
	f.head_to = cell_center(to);
	f.has_head_to = true;
	f.track_number = next * 9;
	f.track_index = 0;
	f.on_short_track = false;
	o.body = Facing { static_cast<uint16_t>(drive_tracks::turn_track(f.track_number).facing << 8) };
	occupy(o, to); // the cell is reserved from the start of the move
	// Target speed: the destination land's multiplier.
	const ra::world::MapCell& mc = world_->grid.at(to);
	f.target_speed = std::max(0.0, land_speeds_.multiplier(mc.land, info(o).speed_type));
}

void Simulation::while_moving(Object& o)
{
	FootState& f = o.foot;
	const TypeInfo& t = info(o);

	// Acceleration (types with Accelerates=yes): U section 10.2.
	if (t.accelerates) {
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
	} else {
		f.speed_percentage = f.target_speed;
	}

	int accum = current_speed(o) + f.speed_accum;
	const drive_tracks::TurnTrack& turn = drive_tracks::turn_track(f.track_number);
	const drive_tracks::RawTrack& track = drive_tracks::raw_track(f.on_short_track ? turn.start_track : turn.track);
	while (accum > 7) {
		accum -= 7;
		const drive_tracks::Point p = track.points[static_cast<size_t>(f.track_index)];
		if (p.x == 0 && p.y == 0 && f.track_index != 0) {
			// The end of the track: re-synchronise on the cell centre.
			accum += ra::fpu::ftol(double(std::abs(f.head_to.x - o.location.x) + std::abs(f.head_to.y - o.location.y)));
			o.location.x = f.head_to.x;
			o.location.y = f.head_to.y;
			o.location.z = ground_z(*world_, o.location);
			f.track_number = -1;
			f.track_index = 0;
			f.has_head_to = false;
			enter_cell(o, ra::cell_of(o.location));
			// Chain the next track within the same frame.
			const int before = f.track_number;
			if (f.has_destination && ra::cell_of(f.destination) != o.cell() && f.has_path())
				start_of_move(o);
			if (f.track_number == before)
				break;
			continue;
		}
		const drive_tracks::Point q = drive_tracks::transform(p, turn.flag);
		o.location.x = f.head_to.x + q.x;
		o.location.y = f.head_to.y + q.y;
		o.location.z = ground_z(*world_, o.location);
		++f.track_index;
	}
	f.speed_accum = accum;
}

void Simulation::walk_process(Object& o)
{
	// PROVISIONAL: infantry walk to the next cell's spot at Speed leptons per frame (Walk, U section 10.3).
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
				f.path_delay = 15;
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
		f.path_delay = 0;
		for (int i = 0; i + 1 < kMaxPath; ++i)
			f.path[static_cast<size_t>(i)] = f.path[static_cast<size_t>(i) + 1];
		f.path[kMaxPath - 1] = -1;
		f.head_to = spot_coord(to, o.sub_cell);
		f.has_head_to = true;
		o.body = Facing { static_cast<uint16_t>(next << 13) };
		f.speed_percentage = 1.0;
		occupy(o, to);
	}
	// Move along the straight line to head_to; snap within 16 leptons.
	const double dx = double(f.head_to.x - o.location.x), dy = double(f.head_to.y - o.location.y);
	const double d = ra::fastmath::sqrt(dx * dx + dy * dy);
	const int speed = std::max(1, current_speed(o));
	if (d <= 16.0 || d <= double(speed)) {
		o.location.x = f.head_to.x;
		o.location.y = f.head_to.y;
		o.location.z = ground_z(*world_, o.location);
		f.has_head_to = false;
		enter_cell(o, ra::cell_of(o.location));
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
	// The layers and the logic list (PROVISIONAL: every object is in the ground layer and the logic list).
	for (const Object& o : objects_)
		if (o.alive && !o.in_limbo)
			crc.add_position(o.location.x, o.location.y, static_cast<uint32_t>(o.what));
	for (const Object& o : objects_)
		if (o.alive && !o.in_limbo)
			crc.add_position(o.location.x, o.location.y, static_cast<uint32_t>(o.what));
}

} // namespace ra::sim
