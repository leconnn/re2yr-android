// Hover, Jumpjet and Fly locomotors, and crushing (U sections 10.4 to 10.6 and 12.4).
//
// What the reports give exactly is used exactly (speed fractions, the hover spring, the jumpjet state
// table and physics, the flight speed rule). What they leave out is filled in with the simplest
// behaviour that fits and is listed in FUTURE_THINGS_TO_UNDERSTAND.md: how fast aircraft climb, the
// jumpjet wobble amplitude, the hover bob's effect on the height, and how the vertical position
// joins the spring.

#include "core/fastmath.h"
#include "core/fpu.h"
#include "sim/objects/simulation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ra::sim {

namespace {

constexpr double kTwoPi = 6.283185307179586;

Coord centre_of(Cell c) { return ra::cell_center(c); }

int ground_z(const ra::world::World& w, Coord c)
{
	const Cell cell = ra::cell_of(c);
	const ra::world::MapCell& mc = w.grid.at(cell);
	return ra::world::floor_height(mc.level, mc.slope, c.x - cell.x * kLeptonsPerCell, c.y - cell.y * kLeptonsPerCell);
}

double distance(Coord a, Coord b)
{
	const double dx = double(b.x - a.x), dy = double(b.y - a.y);
	return ra::fastmath::sqrt(ra::fpu::add(ra::fpu::mul(dx, dx), ra::fpu::mul(dy, dy)));
}

} // namespace

void Simulation::face_toward(Object& o, Coord target)
{
	const double dx = double(target.x - o.location.x), dy = double(target.y - o.location.y);
	if (dx == 0.0 && dy == 0.0)
		return;
	const double angle = ra::fastmath::atan2(-dy, dx);
	const int facing = ra::fpu::ftol(ra::fpu::mul(ra::fpu::sub(angle, 1.5707963267948966), -10430.378350470453));
	o.primary_facing.set_desired(static_cast<uint16_t>(facing), frame_);
}

void Simulation::destroy(Object& o)
{
	if (o.what == What::Building) {
		const TypeInfo& t = info(o);
		for (int y = 0; y < t.foundation_h; ++y)
			for (int x = 0; x < t.foundation_w; ++x) {
				const Cell c { int16_t(o.cell().x + x), int16_t(o.cell().y + y) };
				if (ra::world::MapGrid::in_grid(c.x, c.y) && building_at_[cell_index(c)] == o.id) {
					building_at_[cell_index(c)] = 0;
					blocked_[cell_index(c)] = 0;
				}
			}
	}
	vacate(o, o.foot.current_cell);
	vacate(o, o.cell());
	o.alive = false;
	o.health = 0;
	o.foot.has_destination = false;
	o.foot.has_head_to = false;
}

// ---------------------------------------------------------------------------------------------
// Crushing

void Simulation::try_crush(Object& o)
{
	const TypeInfo& t = info(o);
	if (!t.crusher && !t.omni_crusher)
		return;
	const Cell c = o.cell();
	if (!ra::world::MapGrid::in_grid(c.x, c.y))
		return;
	const CellOccupancy occ = occupancy_[cell_index(c)];
	std::array<ObjectId, 6> ids { occ.vehicle, occ.spot[0], occ.spot[1], occ.spot[2], occ.spot[3], occ.spot[4] };
	for (ObjectId id : ids) {
		if (id == 0 || id == o.id)
			continue;
		Object* v = find(id);
		if (!v || !v->alive || v->what == What::Building)
			continue;
		const TypeInfo& vt = info(*v);
		const bool crushable = vt.crushable || t.omni_crusher; // (OmniCrushResistant isn't read yet)
		// Allied objects are spared unless the crusher is a train.
		if (!crushable || (allied(v->house, o.house) && !t.train))
			continue;
		// The victim's centre must be within 128 leptons (squared distance below 0x4000).
		const int64_t dx = v->location.x - o.location.x, dy = v->location.y - o.location.y;
		if (dx * dx + dy * dy >= 0x4000)
			continue;
		destroy(*v);
	}
}

// ---------------------------------------------------------------------------------------------
// Hover

void Simulation::hover_process(Object& o)
{
	FootState& f = o.foot;
	const TypeInfo& t = info(o);
	const ra::rules::RuleObject rules = world_->rules.section("General");
	const double accel = rules.has("HoverAcceleration") ? rules.get_double("HoverAcceleration") : 0.03;
	const double brake = rules.has("HoverBrake") ? rules.get_double("HoverBrake") : 0.03;
	const double boost = rules.has("HoverBoost") ? rules.get_double("HoverBoost") : 1.3;

	bool moving = false;
	double target = 0.0;
	if (!f.has_head_to && f.has_destination) {
		const Cell here = o.cell();
		const Cell dest = ra::cell_of(f.destination);
		if (here == dest) {
			f.has_destination = false;
			f.path.fill(-1);
		} else if (!f.has_path() && f.path_delay > 0) {
			--f.path_delay;
		} else if (!f.has_path() && (!find_path(o, dest) || !f.has_path())) {
			f.path_delay = std::max(1, ra::fpu::ftol(ra::fpu::mul(world_->rules.section("General").get_double("PathDelay"), 900.0)));
		} else {
			const int next = f.path[0];
			if (next < 0 || next > 7) {
				f.path.fill(-1);
			} else {
				const Cell to { int16_t(here.x + (next == 1 || next == 2 || next == 3 ? 1 : (next >= 5 ? -1 : 0))),
					int16_t(here.y + (next == 3 || next == 4 || next == 5 ? 1 : (next == 7 || next == 0 || next == 1 ? -1 : 0))) };
				const path::Move m = can_enter(o, to, next, here);
				if (m == path::Move::No) {
					f.path.fill(-1);
				} else if (m == path::Move::Ok || m == path::Move::Destroyable || m == path::Move::FriendlyDestroyable) {
					for (int i = 0; i + 1 < kMaxPath; ++i)
						f.path[size_t(i)] = f.path[size_t(i) + 1];
					f.path[kMaxPath - 1] = -1;
					f.head_to = ra::cell_center(to);
					f.has_head_to = true;
					occupy(o, to);
				}
			}
		}
	}

	if (f.has_head_to) {
		moving = true;
		face_toward(o, f.head_to);
		// Target speed: 0 unless facing the way within 0x2000, 0.5 near the goal, otherwise 1, boosted on straights.
		const int16_t off = static_cast<int16_t>(o.primary_facing.desired - o.primary_facing.current(frame_));
		const double to_head = distance(o.location, f.head_to);
		const double to_dest = f.has_destination ? distance(o.location, f.destination) : to_head;
		if (std::abs(int(off)) > 0x2000)
			target = 0.0;
		else if (to_head < 256.0 || to_dest < 256.0)
			target = 0.5;
		else
			target = 1.0;
		if (target > 0.0 && f.path[0] != -1 && f.path[0] == f.path[1])
			target = ra::fpu::mul(target, boost);
	}
	if (f.loco_speed < target)
		f.loco_speed = std::min(target, ra::fpu::add(f.loco_speed, ra::fpu::div(1.0, ra::fpu::mul(accel, 900.0))));
	else if (f.loco_speed > target)
		f.loco_speed = std::max(target, ra::fpu::sub(f.loco_speed, ra::fpu::div(1.0, ra::fpu::mul(brake, 900.0))));
	f.speed_percentage = f.loco_speed;

	if (f.has_head_to) {
		const double d = distance(o.location, f.head_to);
		const int speed = current_speed(o);
		if (d <= 16.0 || (speed > 0 && d <= double(speed))) {
			o.location.x = f.head_to.x;
			o.location.y = f.head_to.y;
			f.has_head_to = false;
			enter_cell(o, ra::cell_of(f.head_to));
		} else if (speed > 0) {
			const double dx = double(f.head_to.x - o.location.x), dy = double(f.head_to.y - o.location.y);
			o.location.x += ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(dx, double(speed)), d));
			o.location.y += ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(dy, double(speed)), d));
		}
	}

	// Height: the damped spring (0x513D20) with the bob added.
	const double H = rules.has("HoverHeight") ? double(rules.get_int("HoverHeight")) : 120.0;
	const double G = rules.has("Gravity") ? double(rules.get_int("Gravity")) : 3.0;
	const double damp = rules.has("HoverDampen") ? rules.get_double("HoverDampen") : 0.8;
	const double bob_period = rules.has("HoverBob") ? rules.get_double("HoverBob") : 30.0;
	const int ground = ground_z(*world_, o.location);
	const double h = double(o.location.z - ground);
	double v = f.hover_vz;
	if (h < H)
		v = ra::fpu::add(v, ra::fpu::mul(ra::fpu::div(ra::fpu::sub(ra::fpu::mul(2.0, H), h), H), G));
	if (h < H / 4.0)
		v = ra::fpu::add(v, ra::fpu::div(G, 3.0));
	v = ra::fpu::mul(ra::fpu::sub(v, G), damp);
	f.hover_vz = v;
	const int period = std::max(1, ra::fpu::ftol(ra::fpu::mul(ra::fpu::mul(bob_period, 900.0), moving ? 1.0 : 1.1)));
	const int phase = int((frame_ + (moving ? 2u : 0u)) % uint32_t(period));
	const double bob = ra::fpu::mul(2.0, ra::fastmath::sin(ra::fpu::div(ra::fpu::mul(kTwoPi, double(phase)), double(period))));
	const double bob_step = ra::fpu::sub(bob, f.wobble);
	f.wobble = bob;
	o.location.z = std::max(ground, o.location.z + ra::fpu::ftol(ra::fpu::add(v, bob_step)));
	(void)t;
}

// ---------------------------------------------------------------------------------------------
// Jumpjet

void Simulation::jumpjet_process(Object& o)
{
	FootState& f = o.foot;
	const TypeInfo& t = info(o);
	const Cell here = o.cell();
	const Cell dest = ra::cell_of(f.destination);
	const int ground = ground_z(*world_, o.location);
	const int height = t.jj_height;
	const int gap = o.location.z - ground;
	const Coord goal = centre_of(dest);

	switch (f.jj_state) {
	case 0: // Grounded: a new destination lifts off.
		f.loco_speed = 0.0;
		if (f.has_destination && here != dest) {
			vacate(o, f.current_cell);
			f.jj_state = 1;
		} else if (f.has_destination) {
			f.has_destination = false;
		}
		break;
	case 1: // Ascending
		if (gap > (t.balloon_hover ? height / 2 : height / 4)) {
			f.loco_max = double(t.jj_speed);
			if (f.has_destination)
				face_toward(o, goal);
		} else {
			f.loco_max = 0.0; // rise first
		}
		if (gap >= height)
			f.jj_state = f.has_destination && here != dest ? 3 : 2;
		break;
	case 2: // Hovering
		f.loco_max = 0.0;
		if (f.has_destination && here != dest)
			f.jj_state = 3;
		else if (!t.balloon_hover)
			f.jj_state = 4;
		break;
	case 3: // Cruising toward the destination
		f.loco_max = double(t.jj_speed);
		if (!f.has_destination) {
			f.jj_state = 2;
		} else {
			face_toward(o, goal);
			if (distance(o.location, goal) <= std::max(1.0, f.loco_speed)) {
				o.location.x = goal.x;
				o.location.y = goal.y;
				f.has_destination = false;
				f.loco_max = 0.0;
				f.loco_speed = 0.0;
				f.jj_state = 2;
			}
		}
		break;
	case 4: // Descending
		f.loco_max = 0.0;
		break;
	default:
		break;
	}

	// Physics (0x54D0F0).
	if (f.loco_speed < f.loco_max)
		f.loco_speed = std::min(ra::fpu::add(f.loco_speed, double(t.jj_accel)), double(t.jj_speed));
	else if (f.loco_speed > f.loco_max)
		f.loco_speed = std::max(ra::fpu::sub(f.loco_speed, ra::fpu::mul(1.5, double(t.jj_accel))), 0.0);
	f.speed_percentage = t.jj_speed > 0 ? ra::fpu::div(f.loco_speed, double(t.jj_speed)) : 0.0;
	if (!t.jj_no_wobbles && (f.jj_state == 2 || f.jj_state == 3))
		f.wobble = ra::fpu::add(f.wobble, ra::fpu::div(kTwoPi, ra::fpu::div(15.0, std::max(0.01, t.jj_wobbles))));

	if (f.jj_state == 1 || f.jj_state == 3) {
		if (f.has_destination && f.loco_speed >= 1.0) {
			const double d = distance(o.location, goal);
			const int step = ra::fpu::ftol(f.loco_speed);
			if (d > 0.0 && step > 0) {
				const double dx = double(goal.x - o.location.x), dy = double(goal.y - o.location.y);
				if (double(step) >= d) {
					o.location.x = goal.x;
					o.location.y = goal.y;
				} else {
					o.location.x += ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(dx, double(step)), d));
					o.location.y += ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(dy, double(step)), d));
				}
			}
		}
	}

	const int floor_now = ground_z(*world_, o.location);
	if (f.jj_state == 4) {
		o.location.z -= std::max(1, ra::fpu::ftol(t.jj_crash));
		if (o.location.z <= floor_now) {
			o.location.z = floor_now;
			f.jj_state = 0;
			const Cell land = o.cell();
			if (o.kind == ra::rules::TypeKind::Infantry)
				o.sub_cell = 0;
			occupy(o, land);
			f.current_cell = land;
		}
	} else if (f.jj_state != 0) {
		const int gap_now = o.location.z - floor_now;
		if (gap_now < height)
			o.location.z = std::min(floor_now + height, o.location.z + t.jj_climb);
		else if (gap_now > height)
			o.location.z = std::max(floor_now, o.location.z - std::max(1, ra::fpu::ftol(t.jj_crash)));
	} else {
		o.location.z = floor_now;
	}

	const Cell cell_now = o.cell();
	if (cell_now != f.current_cell && f.jj_state != 0) {
		f.current_cell = cell_now;
		if (o.house == local_house_)
			reveal_around(o);
	}
}

// ---------------------------------------------------------------------------------------------
// Fly

void Simulation::fly_process(Object& o)
{
	FootState& f = o.foot;
	const TypeInfo& t = info(o);
	const ra::rules::RuleObject rules = world_->rules.section("General");
	const int level = t.flight_level >= 0 ? t.flight_level : (rules.has("FlightLevel") ? rules.get_int("FlightLevel") : 500);
	const int ground = ground_z(*world_, o.location);

	double target = 0.0;
	double dist = 0.0;
	if (f.has_destination) {
		// An aircraft leaving its pad frees the cell.
		if (o.location.z <= ground)
			vacate(o, f.current_cell);
		dist = distance(o.location, f.destination);
		face_toward(o, f.destination);
		const double slow = double(std::max(1, t.slowdown_distance));
		target = std::clamp(ra::fpu::div(dist, slow), 0.0, 1.0);
		if (target < 0.1) {
			if (dist < 86.0) {
				target = 0.0;
				f.loco_speed = ra::fpu::mul(f.loco_speed, 0.5);
			} else {
				target = 0.1;
			}
		}
	}
	// Speed moves toward the target by at most 0.1 a frame.
	if (f.loco_speed < target)
		f.loco_speed = std::min(target, ra::fpu::add(f.loco_speed, 0.1));
	else if (f.loco_speed > target)
		f.loco_speed = std::max(target, ra::fpu::sub(f.loco_speed, 0.1));
	f.speed_percentage = f.loco_speed;

	if (f.has_destination) {
		const int speed = current_speed(o);
		if (dist < 86.0 && target == 0.0) {
			o.location.x = f.destination.x;
			o.location.y = f.destination.y;
			f.has_destination = false;
			f.loco_speed = 0.0;
			f.landing = t.landable;
		} else if (speed > 0 && dist > 0.0) {
			const double dx = double(f.destination.x - o.location.x), dy = double(f.destination.y - o.location.y);
			if (double(speed) >= dist) {
				o.location.x = f.destination.x;
				o.location.y = f.destination.y;
			} else {
				o.location.x += ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(dx, double(speed)), dist));
				o.location.y += ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(dy, double(speed)), dist));
			}
		}
	}

	// Altitude (0x4CD600): below the flight level it climbs; landing descends onto the ground.
	const int floor_now = ground_z(*world_, o.location);
	const int gap = o.location.z - floor_now;
	if (f.has_destination || !f.landing) {
		if (gap < level)
			o.location.z += std::min(level - gap, 20);
		else if (gap > level)
			o.location.z -= std::min(gap - level, 20);
	} else if (gap > 0) {
		o.location.z -= std::min(gap, 6);
	} else {
		o.location.z = floor_now;
		occupy(o, o.cell());
		f.current_cell = o.cell();
		f.landing = false;
	}
	const Cell cell_now = o.cell();
	if (cell_now != f.current_cell) {
		f.current_cell = cell_now;
		if (o.house == local_house_)
			reveal_around(o);
	}
}

} // namespace ra::sim
