// The firing AI: when units turn, approach, acquire targets, fire, retaliate and scatter
// (U section 16; weapon choice U section 4, target acquisition U section 8).
//
// Not here yet (FUTURE_THINGS_TO_UNDERSTAND.md): bridges, the waypoint queue and bridge-repair targets in
// ApproachTarget, gattlings, deploy-to-fire, spawners, prism towers, SAM sites, team members, threat
// nodes (every house uses the Rules "Dumb" threat coefficients) and the global (Hunt) threat scan.

#include "core/fastmath.h"
#include "core/fpu.h"
#include "sim/objects/simulation.h"

#include <algorithm>
#include <cstdlib>

namespace ra::sim {

namespace {

constexpr double kHalfPi = 1.5707963267948966;
constexpr double kAngleToFacing = -10430.060040584269; // 0x7E2818
constexpr double kFacingToRadians = -9.587672516830327e-05; // 0x7E2810

int ground_z(const ra::world::World& w, Coord c)
{
	const Cell cell = ra::cell_of(c);
	const ra::world::MapCell& mc = w.grid.at(cell);
	return ra::world::floor_height(mc.level, mc.slope, c.x - cell.x * kLeptonsPerCell, c.y - cell.y * kLeptonsPerCell);
}

// The 16-bit facing from 'from' toward 'to' (DirStruct from two coordinates).
int facing_between(Coord from, Coord to)
{
	const double a = ra::fastmath::atan2(double(from.y - to.y), double(to.x - from.x));
	return ra::fpu::ftol(ra::fpu::mul(ra::fpu::sub(a, kHalfPi), kAngleToFacing)) & 0xFFFF;
}

int cheb(Cell a, Cell b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

// The ApproachTarget angle offsets (0x8224DC), in 8-bit facing steps.
constexpr int kApproachOffsets[25] = { 0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6, 8, -8, 16, -16, 24, -24, 32, -32, 48, -48, 64, -64 };

// The infantry sequence rates (0x7EAF7C, the fourth byte of each entry).
constexpr int kSequenceRate[42] = { 0, 0, 6, 3, 1, 1, 1, 1, 1, 3, 3, 1, 1, 1, 1, 1, 3, 1, 3, 3, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 3, 1, 3, 1, 3, 4, 6, 3, 1, 1 };

enum Sequence { SeqReady = 0, SeqProne = 2, SeqFireUp = 4, SeqFireProne = 8, SeqDeployedFire = 29, SeqSecondaryFire = 40, SeqSecondaryProne = 41 };

} // namespace

int Simulation::dir_to(const Object& o, Coord target) const { return facing_between(o.location, target); }

void Simulation::set_target(Object& o, const Target& t)
{
	// 0x6FCDB0.
	o.should_lose_target = false;
	if (t == o.target)
		return;
	Target nt = t;
	if (!nt.is_none() && !nt.is_cell()) {
		const Object* to = find(nt.id);
		if (to == &o)
			nt = Target::cell(o.cell().x, o.cell().y);
		else if (!to || !to->alive || to->in_limbo)
			nt = Target::none();
	}
	o.target = nt;
	if (o.target.is_none())
		o.burst_index = 0;
}

int Simulation::weapon_range(const Object& o, int slot) const
{
	const int wi = weapon_of(o, slot);
	return wi >= 0 ? combat_.weapon_info(wi).range : 0;
}

int Simulation::select_weapon(const Object& o, const Target& t) const
{
	// TechnoClass::SelectWeapon (0x6F3330), the parts the port's objects can answer.
	const TypeInfo& ti = info(o);
	if (ti.weapon[0] < 0 || ti.weapon[1] < 0 || t.is_none())
		return 0;
	const WeaponInfo& p = combat_.weapon_info(ti.weapon[0]);
	const WeaponInfo& s = combat_.weapon_info(ti.weapon[1]);
	if (!p.valid() || !s.valid() || s.never_use)
		return 0;
	if (t.is_cell()) {
		if (ti.land_targeting == 2)
			return 1;
		return 0;
	}
	const Object* to = find(t.id);
	if (!to || to->what == What::Bullet)
		return 0;
	const int armor = std::clamp(info(*to).armor, 0, kArmorCount - 1);
	if (combat_.warhead_info(s.warhead).verses[size_t(armor)] != 0.0) {
		if (combat_.warhead_info(p.warhead).verses[size_t(armor)] == 0.0)
			return 1;
		const bool air = in_air(*to);
		if (!air && ti.land_targeting == 2)
			return 1;
		if (combat_.projectile_info(s.projectile).aa && air)
			return 1;
	}
	return 0;
}

// ---------------------------------------------------------------------------------------------
// Moving into range

bool Simulation::is_clear_to_move(const Object& o, Cell c) const
{
	// CellClass::IsClearToMove (0x4834A0) as ApproachTarget asks it: same zone as the unit, no infantry or
	// vehicle in the cell, a wall only for movement zones that cross walls, and land the unit can drive on.
	const TypeInfo& t = info(o);
	if (t.speed_type == SpeedType::Winged)
		return true;
	if (!ra::world::MapGrid::in_grid(c.x, c.y))
		return false;
	const Cell here = o.foot.has_destination ? ra::cell_of(o.foot.destination) : o.cell();
	if (zones_.movement_zone({ c.x, c.y }, t.movement_zone) != zones_.movement_zone({ here.x, here.y }, t.movement_zone))
		return false;
	const CellOccupancy& occ = occupancy_[cell_index(c)];
	for (ObjectId id : { occ.vehicle, occ.spot[0], occ.spot[1], occ.spot[2], occ.spot[3], occ.spot[4] })
		if (id != 0 && id != o.id)
			return false;
	if (building_at_[cell_index(c)] != 0)
		return false;
	const ra::world::MapCell& mc = world_->grid.at(c);
	ra::world::LandType land = mc.land;
	if (mc.overlay >= 0 && size_t(mc.overlay) < world_->overlays.size() && world_->overlays[size_t(mc.overlay)].wall) {
		const int mz = t.movement_zone;
		const bool crushable = world_->overlays[size_t(mc.overlay)].crushable;
		if (!(mz == 2 || mz == 3 || mz == 8 || mz == 12 || ((mz == 1 || mz == 4) && crushable)))
			return false;
		land = ra::world::LandType::Clear;
	}
	return ra::fpu::to_float(land_speeds_.multiplier(land, t.speed_type)) != 0.0f;
}

int Simulation::attempt_path(const Object& o, Cell a, Cell b)
{
	// AStarClass::AttemptPath (0x42D170) without bridges: the hierarchical search, then the larger of the
	// Chebyshev distance and twice the level-0 route minus 2.
	if (!zones_.search({ a.x, a.y }, { b.x, b.y }, info(o).movement_zone))
		return 0x7FFFFFFF;
	const int route = static_cast<int>(zones_.route(0).size());
	return std::max(cheb(a, b), route * 2 - 2);
}

Target Simulation::approach_target(Object& o, bool skip)
{
	const TypeInfo& ti = info(o);
	if (o.target.is_none())
		return Target::none();

	// The Unit wrapper (0x7414E0): AI crushers drive onto crushable targets close by; OmniCrushers onto
	// crushable foot targets.
	if (o.what == What::Unit && !o.target.is_cell()) {
		Object* t = find(o.target.id);
		const bool ai = !houses_[size_t(o.house)].human;
		if (t && ai && !o.foot.has_destination && (ti.crusher || ti.omni_crusher) && info(*t).crushable && !allied(t->house, o.house)
			&& ra::fpu::ftol(dist3d(o.location, t->location)) < (world_->rules.section("General").has("Crush") ? world_->rules.section("General").get_leptons("Crush") : 384)) {
			if (!skip)
				order_move_to(o, t->cell());
			return o.target;
		}
		if (t && ti.omni_crusher && (t->what == What::Unit || t->what == What::Infantry) && info(*t).crushable && !allied(t->house, o.house)) {
			if (!skip)
				order_move_to(o, t->cell());
			return o.target;
		}
	}

	// FootClass::ApproachTarget (0x4D5690).
	const int w = select_weapon(o, o.target);
	int range = weapon_range(o, w);
	if (range >= 0x200)
		range -= 0x80;
	const bool in_range_now = in_range(o, o.target, w);
	const auto give_up = [&]() {
		if (!skip) {
			set_target(o, Target::none());
			o.foot.has_destination = false;
			o.foot.path.fill(-1);
		}
		return Target::none();
	};
	if (!in_range_now && o.mission == Mission::Sticky)
		return give_up();
	const bool can_approach = ti.can_approach_target;
	if (!in_range_now && !can_approach) {
		const bool human = houses_[size_t(o.house)].human;
		const bool keep = o.mission == Mission::Attack || (o.mission == Mission::AreaGuard && human) || (o.mission == Mission::Hunt && !human);
		if (!keep)
			return give_up();
	}
	const bool flying = o.what == What::Aircraft || ti.locomotor == LocomotorKind::Jumpjet;
	const Coord tcoords = target_coords(o.target);

	if (o.foot.has_destination) {
		if (ti.can_recalc_approach_target && !in_range_now && !o.target.is_cell()) {
			const int reset = world_->rules.section("General").has("ApproachTargetResetMultiplier") ? world_->rules.section("General").get_int("ApproachTargetResetMultiplier") : 1;
			if (ra::fpu::ftol(dist3d(tcoords, o.foot.destination)) > reset * range && !skip) {
				o.foot.has_destination = false;
				o.foot.path.fill(-1);
			}
		}
		if (o.foot.has_destination && !in_air(o))
			return Target::none();
	}
	if (in_range_now)
		return Target::none();

	const Object* tobj = o.target.is_cell() ? nullptr : find(o.target.id);
	if (tobj && tobj->what == What::Building)
		range += (info(*tobj).foundation_w + info(*tobj).foundation_h) * 64;
	range = ra::fpu::ftol(ra::fpu::sub(double(range), o.what == What::Infantry ? 281.6 : 179.2));
	if (range <= 0)
		range = 0;
	else if (range <= 256)
		range = 256;

	Coord T = tcoords;
	const Cell tcell = ra::cell_of(T);
	const int facing16 = facing_between(T, o.location); // from the target toward the unit
	const int dir8 = ((facing16 >> 7) + 1) >> 1 & 0xFF;
	if (o.what == What::Infantry && ti.close_range && double(range) < 332.8)
		range = 0x14C;
	if (!tobj || !in_air(*tobj))
		T.z = ground_z(*world_, T);

	Cell chosen { -1, -1 };
	bool found = false;
	for (int d = range; double(d) > 204.8 && !found; d -= 256) {
		for (int k = 0; k < 25 && !found; ++k) {
			const int a8 = (kApproachOffsets[k] + dir8) & 0xFF;
			const int a16 = int(int16_t(uint16_t(a8 << 8))) & ~0xFF;
			const double rad = ra::fpu::mul(double(a16 - 0x3FFF), kFacingToRadians);
			Coord cand;
			cand.y = ra::fpu::ftol(ra::fpu::sub(double(T.y), ra::fpu::mul(ra::fastmath::sin(rad), double(d))));
			cand.x = ra::fpu::ftol(ra::fpu::add(ra::fpu::mul(ra::fastmath::cos(rad), double(d)), double(T.x)));
			Cell cell = ra::cell_of(cand);
			cand = ra::cell_center(cell);
			cand.z = ground_z(*world_, cand);
			const double d2 = dist2d(cand, T);
			if (!(in_range_from(o, cand, o.target, w) || (ti.close_range && d2 < 307.2)))
				continue;
			if (!world_->grid.in_local(cell.x, cell.y, true))
				continue;
			bool clear = is_clear_to_move(o, cell);
			if (!clear) {
				if (o.what == What::Infantry)
					continue;
				for (int dir = 0; dir < 8 && !clear; ++dir) {
					const ra::sim::path::Cell n = ra::sim::path::step({ cell.x, cell.y }, dir);
					Coord nc = ra::cell_center({ int16_t(n.x), int16_t(n.y) });
					const double nd = dist2d(nc, T);
					if ((in_range_from(o, nc, o.target, w) || (ti.close_range && nd < 307.2)) && is_clear_to_move(o, { int16_t(n.x), int16_t(n.y) })) {
						clear = true;
						cell = { int16_t(n.x), int16_t(n.y) };
					}
				}
				if (!clear)
					continue;
			}
			// The path check.
			if (flying || attempt_path(o, cell, tcell) <= cheb(cell, tcell) + 8 || attempt_path(o, o.cell(), cell) <= cheb(o.cell(), cell) + 8) {
				found = true;
				chosen = cell;
			}
		}
	}
	if (found) {
		if (!skip)
			order_move_to(o, chosen);
		return Target::cell(chosen.x, chosen.y);
	}
	// Nothing found: hunter-seekers and vehicle thieves drive onto the target; others give it up.
	if (ti.hunter_seeker || (o.what == What::Infantry && ti.vehicle_thief)) {
		if (!skip)
			order_move_to(o, tcell);
		return o.target;
	}
	if (!skip) {
		set_target(o, Target::none());
		o.foot.has_destination = false;
		o.foot.path.fill(-1);
	}
	return Target::none();
}

// ---------------------------------------------------------------------------------------------
// Target acquisition

double Simulation::threat_value(const Object& o, const Object& target, const Coord* from) const
{
	// TechnoClass::ThreatValue (0x70CD10) with the Rules "Dumb" coefficients.
	const ra::rules::RuleObject g = world_->rules.section("General");
	const auto coef = [&](const char* key) { return g.has(key) ? g.get_double(key) : 0.0; };
	const double my_eff = coef("DumbMyEffectivenessCoefficient"), t_eff = coef("DumbTargetEffectivenessCoefficient");
	const double t_special = coef("DumbTargetSpecialThreatCoefficient"), t_strength = coef("DumbTargetStrengthCoefficient");
	const double t_distance = coef("DumbTargetDistanceCoefficient");
	const int w = select_weapon(o, Target::object(target.id));
	const int wi = weapon_of(o, w);
	double v = 0.0;
	if (target.what != What::Bullet) {
		const int tw = select_weapon(target, Target::object(o.id));
		const int twi = weapon_of(target, tw);
		if (twi >= 0 && combat_.weapon_info(twi).warhead >= 0) {
			const double e = ra::fpu::mul(t_eff, combat_.warhead_info(combat_.weapon_info(twi).warhead).verses[size_t(std::clamp(info(o).armor, 0, kArmorCount - 1))]);
			v = target.target == Target::object(o.id) ? -e : e;
		}
		v = ra::fpu::add(v, ra::fpu::mul(t_special, double(info(target).special_threat_value)));
	}
	if (wi >= 0 && combat_.weapon_info(wi).warhead >= 0)
		v = ra::fpu::add(v, ra::fpu::mul(my_eff, combat_.warhead_info(combat_.weapon_info(wi).warhead).verses[size_t(std::clamp(info(target).armor, 0, kArmorCount - 1))]));
	const double health = info(target).strength > 0 ? ra::fpu::div(double(target.health), double(info(target).strength)) : 0.0;
	v = ra::fpu::add(v, ra::fpu::mul(t_strength, health));
	const int range_cells = (wi >= 0 ? combat_.weapon_info(wi).range : info(o).guard_range) / 256;
	const double d = from ? dist3d(*from, target.location) : ra::fpu::div(dist3d(o.location, target.location), 256.0);
	return ra::fpu::add(ra::fpu::add(ra::fpu::mul(std::max(ra::fpu::sub(d, double(range_cells)), 0.0), t_distance), v), 100000.0);
}

Target Simulation::greatest_threat(Object& o, int flags, Coord from)
{
	// TechnoClass::GreatestThreat (0x6F8DF0), the local scan (Range 1 / Area 2).
	const TypeInfo& ti = info(o);
	if (ti.no_auto_fire && houses_[size_t(o.house)].human)
		return Target::none();
	const bool armed = weapon_of(o, 0) >= 0 || weapon_of(o, 1) >= 0;
	if (!armed)
		return Target::none(); // healers and repairers come with their phases
	const auto guard_range = [&](int mode) {
		if (mode == 0)
			return ti.guard_range;
		int r = ti.guard_range != 0 ? ti.guard_range : std::max(weapon_range(o, 0), weapon_range(o, 1));
		r *= 2;
		return mode == 2 ? std::clamp(r, 0x700, 0x1000) : std::clamp(r, 0, 0x1000);
	};
	int range = (flags & 1) ? guard_range(0) : ((flags & 2) ? guard_range(o.mission == Mission::Patrol ? 2 : 1) : 0);
	int range_cells = range / 256;
	if (range == 0) {
		const int r = std::max(weapon_range(o, 0), weapon_range(o, 1));
		range_cells = r / 256 + 1 + ti.air_range_bonus / 256;
	}
	const int zone = ((flags & 1) == 0 && o.what != What::Building && o.what != What::Aircraft) ? zones_.movement_zone({ o.cell().x, o.cell().y }, ti.movement_zone) : -1;
	const Cell centre = ra::cell_of(from);
	Target best = Target::none();
	int best_threat = 0;
	const auto consider = [&](int cx, int cy) {
		if (!ra::world::MapGrid::in_grid(cx, cy))
			return;
		const size_t index = cell_index({ int16_t(cx), int16_t(cy) });
		const CellOccupancy occ = occupancy_[index];
		for (ObjectId id : { occ.vehicle, occ.spot[0], occ.spot[1], occ.spot[2], occ.spot[3], occ.spot[4], building_at_[index] }) {
			Object* t = find(id);
			if (!t || !t->alive || t->in_limbo || t == &o || allied(t->house, o.house))
				continue;
			// CanAutoTargetObject (0x6F7CA0).
			const Target tt = Target::object(t->id);
			const int w = select_weapon(o, tt);
			if (get_fire_error(o, tt, w, false) == kFireIllegal)
				continue;
			const int wi = weapon_of(o, w);
			if (wi < 0 || combat_.warhead_info(combat_.weapon_info(wi).warhead).verses[size_t(std::clamp(info(*t).armor, 0, kArmorCount - 1))] <= 0.02)
				continue;
			if (zone != -1 && zones_.movement_zone({ t->cell().x, t->cell().y }, ti.movement_zone) != zone && t->what != What::Building)
				continue;
			if (range > 0) {
				if (ra::fpu::ftol(in_air(*t) ? dist2d(o.location, t->location) : dist3d(o.location, t->location)) > range)
					continue;
			} else if (!in_range(o, tt, w)) {
				continue;
			}
			const int threat = std::max(1, ra::fpu::ftol(threat_value(o, *t, &from)));
			if (threat > best_threat) {
				best_threat = threat;
				best = tt;
			}
			return; // the first eligible object of a cell is the one scored
		}
	};
	for (int r = 0; r < range_cells; ++r) {
		for (int x = -r; x <= r; ++x) {
			consider(centre.x + x, centre.y - r);
			if (r > 0)
				consider(centre.x + x, centre.y + r);
		}
		for (int y = -r + 1; y <= r - 1; ++y) {
			consider(centre.x - r, centre.y + y);
			consider(centre.x + r, centre.y + y);
		}
		if (!best.is_none() && (r == range_cells / 4 || r == range_cells / 2))
			break;
	}
	return best;
}

bool Simulation::target_and_estimate_damage(Object& o, Coord from, int flags)
{
	// 0x709820.
	const ra::rules::RuleObject g = world_->rules.section("General");
	const int delay = o.mission == Mission::AreaGuard ? (g.has("GuardAreaTargetingDelay") ? g.get_int("GuardAreaTargetingDelay") : 36)
	                                                  : (g.has("NormalTargetingDelay") ? g.get_int("NormalTargetingDelay") : 27);
	o.targeting_start = int(frame_);
	o.targeting_delay = delay + random_->ranged(0, 2);
	if (!o.target.is_none() && o.should_lose_target) {
		const int e = get_fire_error(o, o.target, select_weapon(o, o.target), true);
		if (e == kFireIllegal || e == kFireRange)
			set_target(o, Target::none());
	}
	if (o.target.is_none()) {
		const Target t = greatest_threat(o, flags & 3, from);
		if (!info(o).distributed_fire && !t.is_none()) {
			set_target(o, t);
			// The damage reservation on the target's EstimatedHealth (0x6FDB80) is not kept yet.
		}
	}
	return !o.target.is_none();
}

bool Simulation::can_passive_acquire(const Object& o) const
{
	// 0x709290 / 0x7091D0.
	const TypeInfo& ti = info(o);
	if (!ti.can_passive_acquire)
		return false;
	const bool armed = weapon_of(o, 0) >= 0 || weapon_of(o, 1) >= 0;
	if (!armed)
		return false;
	if (!ti.opportunity_fire && o.mission != Mission::Guard)
		return false;
	return true;
}

void Simulation::update_targeting(Object& o)
{
	// TechnoClass::Update's targeting timer and the zone check (U section 16.1).
	if (int(frame_) - o.targeting_start >= o.targeting_delay) {
		if ((o.mission == Mission::Move || o.mission == Mission::Harvest || o.mission == Mission::Guard) && can_passive_acquire(o)) {
			const Target old = o.target;
			if (target_and_estimate_damage(o, o.location, 1) && !(o.target == old))
				o.should_lose_target = true;
		}
	}
	if (o.what != What::Aircraft && o.what != What::Building && !o.target.is_none() && !o.target.is_cell()) {
		const Object* t = find(o.target.id);
		const int mz = info(o).movement_zone;
		if (t && t->what != What::Building && zones_.movement_zone({ t->cell().x, t->cell().y }, mz) != zones_.movement_zone({ o.cell().x, o.cell().y }, mz)) {
			approach_target(o, false);
			if (!o.foot.has_destination && !in_range(o, o.target, select_weapon(o, o.target)))
				set_target(o, Target::none());
		}
	}
}

// ---------------------------------------------------------------------------------------------
// The fire steps

void Simulation::unit_fire_step(Object& o)
{
	// 0x736DF0.
	if (o.target.is_none())
		return;
	if (!o.target.is_cell()) {
		const Object* t = find(o.target.id);
		if (!t || !t->alive || t->in_limbo) {
			set_target(o, Target::none());
			return;
		}
	}
	const int w = select_weapon(o, o.target);
	if (weapon_of(o, w) < 0)
		return;
	const int e = get_fire_error(o, o.target, w, true);
	const TypeInfo& ti = info(o);
	switch (e) {
	case kFireOk:
		o.is_firing = false;
		fire(o, o.target, w);
		o.last_fire_frame = int(frame_);
		break;
	case kFireFacing: {
		const uint16_t dir = uint16_t(dir_to(o, target_coords(o.target)));
		if (ti.turret) {
			o.secondary_facing.set_desired(dir, frame_);
		} else if (!o.foot.has_destination && o.foot.track_number == -1 && !o.foot.has_head_to) {
			o.primary_facing.set_desired(dir, frame_);
			o.secondary_facing.set_desired(o.primary_facing.current(frame_), frame_);
		}
		break;
	}
	case kFireIllegal:
		drop_healed_target(o, w, What::Unit);
		break;
	case kFireRange:
	case 0xB:
	case kFireCloaked:
		o.is_firing = false;
		break;
	default:
		break;
	}
}

void Simulation::unit_turret_step(Object& o)
{
	// 0x736990.
	const TypeInfo& ti = info(o);
	if (!o.target.is_none() && !o.turret_busy && ti.turret)
		o.secondary_facing.set_desired(uint16_t(dir_to(o, target_coords(o.target))), frame_);
	if (!ti.turret_spins) {
		o.turret_busy = false;
		if (ti.turret) {
			if (o.secondary_facing.rotating(frame_)) {
				o.turret_busy = true;
			} else if (o.target.is_none()) {
				const ra::rules::RuleObject g = world_->rules.section("General");
				const int delay = g.has("GuardAreaTargetingDelay") ? g.get_int("GuardAreaTargetingDelay") : 36;
				if (int(frame_) - o.last_fire_frame >= delay + 5) {
					const uint16_t facing = o.foot.has_destination ? uint16_t(dir_to(o, o.foot.destination)) : o.primary_facing.current(frame_);
					o.secondary_facing.set_desired(facing, frame_);
				}
			}
		}
	}
}

void Simulation::play_sequence(Object& o, int sequence)
{
	// InfantryClass::PlayAnim (0x51D6F0): the sequence starts at stage 0 with its rate.
	o.sequence = sequence;
	o.stage = 0;
	o.stage_start = int(frame_);
	o.stage_rate = sequence >= 0 && sequence < 42 ? kSequenceRate[sequence] : 0;
}

void Simulation::infantry_stage(Object& o)
{
	// The StageClass: one stage every 'rate' frames; a sequence that runs out stops (rate 0).
	if (o.stage_rate <= 0 || o.sequence < 0)
		return;
	if (int(frame_) - o.stage_start >= o.stage_rate) {
		++o.stage;
		o.stage_start = int(frame_);
		const int count = info(o).sequence_count[size_t(o.sequence)];
		if (o.stage >= std::max(1, count))
			o.stage_rate = 0;
	}
}

void Simulation::infantry_fire_step(Object& o)
{
	// 0x5206B0.
	if (o.target.is_none()) {
		o.is_firing = false;
		return;
	}
	if (!o.target.is_cell()) {
		const Object* t = find(o.target.id);
		if (!t || !t->alive || t->in_limbo) {
			set_target(o, Target::none());
			o.is_firing = false;
			return;
		}
	}
	const TypeInfo& ti = info(o);
	const int w = select_weapon(o, o.target);
	if (!o.is_firing) {
		const int e = get_fire_error(o, o.target, w, true);
		if (e == kFireOk) {
			const bool secondary = w != 0 && ti.sequence_count[SeqSecondaryFire] != 0;
			play_sequence(o, secondary ? SeqSecondaryFire : SeqFireUp);
			o.is_firing = true;
			const uint16_t dir = uint16_t(dir_to(o, target_coords(o.target)));
			o.primary_facing.set_current(dir, frame_);
			o.body.raw = dir;
		} else if (e == kFireIllegal) {
			drop_healed_target(o, w, What::Infantry);
		}
	}
	const int fire_frame = (w != 0 && ti.sequence_count[SeqSecondaryFire] != 0) ? ti.secondary_fire : ti.fire_up;
	// The shot leaves on the fire frame; a second check on the same stage fails on the rearm timer and
	// ends the firing.
	if (!o.is_firing || o.stage != fire_frame)
		return;
	const int w2 = select_weapon(o, o.target);
	if (get_fire_error(o, o.target, w2, true) == kFireOk) {
		fire(o, o.target, w2);
		o.last_fire_frame = int(frame_);
		return;
	}
	o.is_firing = false;
	play_sequence(o, SeqReady);
}

void Simulation::drop_healed_target(Object& o, int slot, What healed_kind)
{
	// An ILLEGAL fire error drops the target only for a healing weapon (CombatDamage below 0, 0x6F3970),
	// unless the target is of the kind it heals and still below ConditionGreen.
	const int wi = weapon_of(o, slot);
	if (wi < 0 || combat_.weapon_info(wi).damage >= 0)
		return;
	const Object* t = o.target.is_cell() ? nullptr : find(o.target.id);
	const ra::rules::RuleObject g = world_->rules.section("General");
	const double green = g.has("ConditionGreen") ? g.get_double("ConditionGreen") : 1.0;
	if (t && t->what == healed_kind && ra::fpu::div(double(t->health), double(std::max(1, info(*t).strength))) < green)
		return;
	set_target(o, Target::none());
}

void Simulation::order_move_to(Object& o, Cell c)
{
	// FootClass::SetDestination (vslot 0x480) with a cell.
	o.foot.destination = ra::cell_center(c);
	o.foot.has_destination = true;
	o.foot.path.fill(-1);
	o.foot.path_length = 0;
	o.foot.path_delay = 0;
}

bool Simulation::mission_flag(Mission m, const char* key, bool fallback) const
{
	static const char* const names[] = { "Sleep", "Attack", "Move", "QMove", "Retreat", "Guard", "Sticky", "Enter", "Capture", "Eaten",
		"Harvest", "Area Guard", "Return", "Stop", "Ambush", "Hunt", "Unload", "Sabotage", "Construction", "Selling", "Repair",
		"Rescue", "Missile", "Harmless", "Open", "Patrol", "Paradrop Approach", "Paradrop Overfly", "Wait", "Attack Move",
		"Spyplane Approach", "Spyplane Overfly" };
	const int i = static_cast<int>(m);
	if (i < 0 || i >= 32)
		return fallback;
	const ra::rules::RuleObject s = world_->rules.section(names[i], "MissionControlClass");
	return s.has(key) ? s.get_bool(key) : fallback;
}

void Simulation::building_fire_step(Object& o)
{
	// BuildingClass::Mission_Attack's firing (U section 9.6) runs from the mission; here a turreted
	// building aims while it has a target.
	if (o.target.is_none())
		return;
	if (info(o).turret)
		o.primary_facing.set_desired(uint16_t(dir_to(o, target_coords(o.target))), frame_);
}

// ---------------------------------------------------------------------------------------------
// Retaliation and scatter

bool Simulation::can_retaliate(const Object& o, const Object& attacker) const
{
	// 0x7087C0, the checks the port's objects can answer.
	const TypeInfo& ti = info(o);
	if (!ti.can_retaliate || &o == &attacker || allied(attacker.house, o.house))
		return false;
	const bool human = houses_[size_t(o.house)].human;
	if (human && !o.target.is_none())
		return false;
	const bool retaliate_flag = mission_flag(o.mission, "Retaliate", true);
	if (!retaliate_flag)
		return false;
	if (std::max(weapon_range(o, 0), weapon_range(o, 1)) <= 0)
		return false;
	const Target t = Target::object(attacker.id);
	const int w = select_weapon(o, t);
	const int e = get_fire_error(o, t, w, false);
	if (e == kFireIllegal || e == kFireCant)
		return false;
	if (human) {
		const bool return_fire = world_->rules.section("General").has("PlayerReturnFire") && world_->rules.section("General").get_bool("PlayerReturnFire");
		if (!return_fire && o.what != What::Building && o.mission != Mission::AreaGuard && o.mission != Mission::Guard && o.mission != Mission::Patrol)
			return false;
	} else if (!o.target.is_none() && !o.target.is_cell()) {
		const Object* cur = find(o.target.id);
		if (cur && threat_value(o, attacker, nullptr) < threat_value(o, *cur, nullptr))
			return false;
	}
	const int wi = weapon_of(o, w);
	if (wi < 0 || combat_.warhead_info(combat_.weapon_info(wi).warhead).verses[size_t(std::clamp(info(attacker).armor, 0, kArmorCount - 1))] <= 0.009999999776482582)
		return false;
	return true;
}

void Simulation::retaliate(Object& victim, ObjectId attacker_id)
{
	// TechnoClass::ReceiveDamage's retaliation and scatter (U section 3.3).
	Object* attacker = find(attacker_id);
	if (!victim.alive || !attacker || !attacker->alive || attacker->what == What::Bullet)
		return;
	if (can_retaliate(victim, *attacker)) {
		const Target t = Target::object(attacker->id);
		const int w = select_weapon(victim, t);
		bool go = true;
		if (!in_range(victim, t, w) && houses_[size_t(victim.house)].human) {
			const double reach = ra::fpu::mul(ra::fpu::add(double(info(victim).sight), 0.5), 256.0);
			go = dist3d(victim.location, attacker->location) <= reach;
		}
		if (go) {
			set_target(victim, t);
			queue_mission(victim, Mission::Attack, true);
			return;
		}
	}
	// A foot unit with no target and no destination scatters after a hit, when allowed.
	if ((victim.what == What::Unit || victim.what == What::Infantry) && victim.target.is_none() && !victim.foot.has_destination) {
		const bool player_scatter = world_->rules.section("General").has("PlayerScatter") && world_->rules.section("General").get_bool("PlayerScatter");
		if ((player_scatter || !houses_[size_t(victim.house)].human) && mission_flag(victim.mission, "Scatter", true))
			scatter(victim, &attacker->location, false, false);
	}
}

void Simulation::scatter(Object& o, const Coord* source, bool forced, bool no_kidding)
{
	// UnitClass::Scatter (0x743A50) / InfantryClass::Scatter (0x51D0D0), without bridges.
	if (o.what != What::Unit && o.what != What::Infantry)
		return;
	const TypeInfo& ti = info(o);
	if (!mission_flag(o.mission, "Scatter", true) && !no_kidding)
		return;
	if (o.what == What::Unit) {
		if (o.primary_facing.rotating(frame_))
			return;
		if (o.foot.has_destination && !forced)
			return;
		if (o.target.is_none() == false && !no_kidding && random_->ranged(1, 4) != 1)
			return;
	} else {
		if (!ti.fraidycat && !o.target.is_none() && !forced)
			return;
	}
	int dir8;
	if (source) {
		const int f = facing_between(*source, o.location);
		dir8 = o.what == What::Unit ? random_->ranged(0, 2) - 1 + (((f >> 12) + 1) >> 1) : random_->ranged(0, 4) - 2 + (((f >> 12) + 1) >> 1 & 7);
	} else {
		const int f = o.primary_facing.current(frame_);
		dir8 = o.what == What::Unit ? random_->ranged(0, 2) - 1 + (((f >> 12) + 1) >> 1 & 7) : random_->ranged(0, 4) + (((f >> 12) + 1) >> 1 & 7);
	}
	const Cell here = o.cell();
	for (int k = 0; k < 8; ++k) {
		const int dir = (k + dir8) & 7;
		const ra::sim::path::Cell n = ra::sim::path::step({ here.x, here.y }, dir);
		const Cell c { int16_t(n.x), int16_t(n.y) };
		if (!world_->grid.in_local(c.x, c.y, true))
			continue;
		if (can_enter(o, c, dir, here) != path::Move::Ok)
			continue;
		order_move_to(o, c);
		queue_mission(o, Mission::Move, false);
		commence_mission(o);
		return;
	}
}

} // namespace ra::sim
