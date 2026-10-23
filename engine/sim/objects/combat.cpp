// Combat: damage, weapons, firing and projectiles (U sections 3 to 7).
//
// What is exact: GetTotalDamage, ReceiveDamage's damage states, DamageArea's distance rules (ground
// objects), GetROF with its RNG draws, the Fire sequence's damage, speed, scatter and burst/rearm
// bookkeeping, and BulletClass::Update's non-homing flight with its hit tests. What is approximate and
// listed in FUTURE_THINGS_TO_UNDERSTAND.md: the muzzle position (FLH), the homing steering (0x5B20F0),
// the 3D velocity rotation, the line-of-fire and obstacle traces, and every visual effect (animations,
// debris pieces, shake, sounds), whose random draws are kept where the reports say they happen.

#include "core/fastmath.h"
#include "core/fpu.h"
#include "sim/objects/simulation.h"
#include "sim/world/shroud.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ra::sim {

namespace {

constexpr double kPi = 3.141592653589793;

int ground_z(const ra::world::World& w, Coord c)
{
	const Cell cell = ra::cell_of(c);
	const ra::world::MapCell& mc = w.grid.at(cell);
	return ra::world::floor_height(mc.level, mc.slope, c.x - cell.x * kLeptonsPerCell, c.y - cell.y * kLeptonsPerCell);
}

// A 16-bit binary angle from a random draw, as Fire and MoveTo do it (the quantisation matters for sync).
double scatter_angle(ra::Random2& rng)
{
	const int32_t r = rng.ranged(0, 0x7FFFFFFE);
	const double turns = ra::fpu::mul(double(r), 4.656612875245797e-10); // 2^-31
	const int a16 = ra::fpu::ftol(ra::fpu::mul(ra::fpu::sub(ra::fpu::mul(turns, 2.0 * kPi), kPi / 2.0), -10430.06));
	return ra::fpu::mul(double(int16_t(a16) - 0x3FFF), -9.5877e-5);
}

} // namespace

double Simulation::dist3d(Coord a, Coord b) const
{
	const double dx = double(b.x - a.x), dy = double(b.y - a.y), dz = double(b.z - a.z);
	return ra::fastmath::sqrt(ra::fpu::add(ra::fpu::add(ra::fpu::mul(dx, dx), ra::fpu::mul(dy, dy)), ra::fpu::mul(dz, dz)));
}

double Simulation::dist2d(Coord a, Coord b) const
{
	const double dx = double(b.x - a.x), dy = double(b.y - a.y);
	return ra::fastmath::sqrt(ra::fpu::add(ra::fpu::mul(dx, dx), ra::fpu::mul(dy, dy)));
}

bool Simulation::in_air(const Object& o) const
{
	if (o.what == What::Aircraft || info(o).locomotor == LocomotorKind::Jumpjet)
		return o.location.z - ground_z(*world_, o.location) > 0 && o.foot.jj_state != 0;
	return false;
}

Coord Simulation::target_coords(const Target& t) const
{
	if (t.is_cell())
		return ra::cell_center({ int16_t(t.cell_x()), int16_t(t.cell_y()) });
	if (const Object* o = find(t.id)) {
		Coord c = o->location;
		if (o->what == What::Building) {
			const TypeInfo& ti = info(*o);
			c.x += (ti.foundation_w - 1) * kLeptonsPerCell / 2;
			c.y += (ti.foundation_h - 1) * kLeptonsPerCell / 2;
		}
		return c;
	}
	return {};
}

// ---------------------------------------------------------------------------------------------
// Damage

int Simulation::get_total_damage(int damage, int warhead, int armor, int distance) const
{
	if (damage == 0 || warhead < 0)
		return 0;
	if (damage < 0)
		return distance < 8 ? damage : 0;
	const WarheadInfo& w = combat_.warhead_info(warhead);
	const double minimum = ra::fpu::mul(double(damage), w.percent_at_max);
	const int spread = ra::fpu::ftol(ra::fpu::mul(w.cell_spread, 256.0));
	if (minimum != double(damage) && spread != 0) {
		const double scaled = ra::fpu::div(ra::fpu::mul(ra::fpu::sub(double(damage), minimum), double(spread - distance)), double(spread));
		damage = ra::fpu::ftol(ra::fpu::add(scaled, minimum));
	}
	if (damage <= 0)
		damage = 0;
	damage = ra::fpu::ftol(ra::fpu::mul(double(damage), w.verses[size_t(std::clamp(armor, 0, kArmorCount - 1))]));
	return std::min(damage, combat_.rules().max_damage);
}

int Simulation::receive_damage(Object& victim, int damage, int distance, int warhead, ObjectId source, int source_house, bool ignore_defenses)
{
	if (!victim.alive || victim.what == What::Bullet)
		return kNoEffect;
	const TypeInfo& type = info(victim);
	const WarheadInfo* w = warhead >= 0 ? &combat_.warhead_info(warhead) : nullptr;

	// TechnoClass::ReceiveDamage's pre-processing (0x701900). The house and object armor multipliers
	// are 1.0 until houses carry them; veterancy is not here yet.
	if (!ignore_defenses && damage > 0) {
		damage = ra::fpu::ftol(ra::fpu::div(double(damage), 1.0));
		if (has_ability(victim, 1)) // STRONGER
			damage = ra::fpu::ftol(ra::fpu::div(double(damage), combat_.rules().veteran_armor));
		damage = std::max(damage, 1);
		if (type.type_immune && source != 0) {
			const Object* s = find(source);
			if (s && s->type_info == victim.type_info && s->house == victim.house)
				return kNoEffect;
		}
		if (w && !w->affects_allies && source_house >= 0 && allied(source_house, victim.house))
			return kNoEffect;
	}

	// ObjectClass::ReceiveDamage (0x5F5390).
	if (damage == 0)
		return kNoEffect;
	if (!ignore_defenses)
		damage = get_total_damage(damage, warhead, type.armor, distance);
	if (victim.what == What::Building && !type.can_c4 && damage == 0)
		damage = 1;
	if (damage < 0) {
		victim.health = std::min(victim.health - damage, type.strength);
		return kNoEffect;
	}
	if (damage == 0)
		return kUnchanged;

	const int old_health = victim.health;
	const int yellow = type.strength / 2;
	const int red = ra::fpu::ftol(ra::fpu::mul(double(type.strength), combat_.rules().condition_red));
	if (damage >= old_health) {
		victim.health = 0;
		victim.last_attacker = source;
		// Debris pieces are drawn here; the pieces themselves are visual (U section 3.3).
		(void)random_->ranged(type.min_debris, type.max_debris - 1);
		if (Object* killer = find(source))
			register_destruction(*killer, victim);
		destroy(victim);
		return kDead;
	}
	victim.health = old_health - damage;
	victim.last_attacker = source;
	if (source != 0)
		retaliate(victim, source);
	// Crossing the red threshold wins over crossing the yellow one.
	if (old_health > red && victim.health <= red)
		return kRed;
	if (old_health > yellow && victim.health <= yellow)
		return kYellow;
	return kUnchanged;
}

void Simulation::damage_area(Coord at, int damage, ObjectId source, int warhead, int source_house)
{
	if (damage == 0 || warhead < 0)
		return;
	const WarheadInfo& w = combat_.warhead_info(warhead);
	const int radius = ra::fpu::ftol(ra::fpu::mul(w.cell_spread, 256.0));
	const Cell centre = ra::cell_of(at);
	const int ring = std::clamp(ra::fpu::ftol(w.cell_spread), 0, 11);
	const int level_height = ra::world::kLevelHeight;

	// Collect the victims cell by cell in the circle table's order, so the damage order is stable.
	std::vector<ObjectId> victims;
	const int count = ra::world::reveal_cell_count(ring);
	for (int i = 0; i < count; ++i) {
		int dx = 0, dy = 0;
		ra::world::reveal_cell_offset(i, dx, dy);
		const int cx = centre.x + dx, cy = centre.y + dy;
		if (!ra::world::MapGrid::in_grid(cx, cy))
			continue;
		const size_t index = cell_index({ int16_t(cx), int16_t(cy) });
		const CellOccupancy occ = occupancy_[index];
		const ObjectId building = building_at_[index];
		for (ObjectId id : { occ.vehicle, occ.spot[0], occ.spot[1], occ.spot[2], occ.spot[3], occ.spot[4], building })
			if (id != 0 && std::find(victims.begin(), victims.end(), id) == victims.end())
				victims.push_back(id);
	}
	// Aircraft and jumpjets in the air are not in the occupancy: they are found by distance.
	for (const Object& o : objects_)
		if (o.alive && o.what != What::Bullet && in_air(o) && std::find(victims.begin(), victims.end(), o.id) == victims.end())
			victims.push_back(o.id);

	const Object* src = find(source);
	for (ObjectId id : victims) {
		Object* v = find(id);
		if (!v || !v->alive || v->in_limbo)
			continue;
		const TypeInfo& vt = info(*v);
		if (src && v == src && !vt.damage_self)
			continue;
		int distance;
		if (v->what == What::Building) {
			const Coord c = target_coords(Target::object(v->id));
			if (std::abs(at.z - ground_z(*world_, c)) < 2 * level_height && ra::cell_of(at) == v->cell())
				distance = 0;
			else
				distance = std::max(0, ra::fpu::ftol(dist3d(at, c)) - 2 * level_height);
		} else {
			distance = ra::fpu::ftol(dist3d(at, v->location));
		}
		if (distance > radius)
			continue;
		receive_damage(*v, damage, distance, warhead, source, source_house);
	}
}

// ---------------------------------------------------------------------------------------------
// Firing

int Simulation::get_rof(Object& o, int slot)
{
	const int wi = weapon_of(o, slot);
	if (wi < 0)
		return 1;
	const WeaponInfo& w = combat_.weapon_info(wi);
	if (o.burst_index < w.burst)
		return random_->ranged(3, 5); // between the shots of a burst (no FiringSyncFrame yet)
	int rof = ra::fpu::ftol(ra::fpu::add(ra::fpu::mul(double(w.rof), 1.0), double(random_->ranged(0, 2))));
	if (has_ability(o, 4)) // ROF
		rof = ra::fpu::ftol(ra::fpu::mul(double(rof), combat_.rules().veteran_rof));
	return rof;
}

bool Simulation::in_range(const Object& o, const Target& target, int slot) const
{
	return in_range_from(o, o.location, target, slot);
}

bool Simulation::in_range_from(const Object& o, Coord from, const Target& target, int slot) const
{
	const int wi = weapon_of(o, slot);
	if (wi < 0)
		return false;
	const WeaponInfo& w = combat_.weapon_info(wi);
	if (!w.valid())
		return false;
	if (w.range == -512)
		return true; // unlimited
	const ProjectileInfo& p = combat_.projectile_info(w.projectile);
	if (w.cell_rangefinding)
		from = ra::cell_center(ra::cell_of(from), from.z);
	const Object* to = find(target.id);
	int range = w.range;
	if (to && in_air(*to))
		range += info(o).air_range_bonus;
	Coord tc = target_coords(target);
	if (!target.is_cell() && to && !in_air(*to))
		tc.z = ground_z(*world_, tc);
	if (w.minimum_range != 0 && dist3d(from, tc) < double(w.minimum_range))
		return false;
	if (!p.arcing) {
		if (to && to->what == What::Building) {
			const TypeInfo& bt = info(*to);
			range += (bt.foundation_w + bt.foundation_h) * 64;
		}
		return dist3d(from, tc) <= double(range);
	}
	// Arcing: the horizontal distance decides, then the shot must have a ballistic solution (checked at firing).
	return dist2d(from, tc) <= double(range);
}

int Simulation::get_fire_error(const Object& o, const Target& target, int slot, bool check_range) const
{
	// The checks of TechnoClass::GetFireError (0x6FC0B0) that apply to the objects the port has.
	if (target.is_none())
		return kFireIllegal;
	const Object* tt = target.is_cell() ? nullptr : find(target.id);
	if (!target.is_cell() && !tt)
		return kFireIllegal;
	if (tt && (tt->in_limbo || !tt->alive))
		return kFireIllegal;
	const int wi = weapon_of(o, slot);
	if (wi < 0)
		return kFireCant;
	const WeaponInfo& w = combat_.weapon_info(wi);
	if (!w.valid())
		return kFireCant;
	const ProjectileInfo& p = combat_.projectile_info(w.projectile);
	const WarheadInfo& h = combat_.warhead_info(w.warhead);
	const bool air = tt && in_air(*tt);
	if (air && !p.aa)
		return kFireIllegal;
	if (!tt && !p.ag)
		return kFireIllegal;
	if (tt && !air && !p.ag)
		return kFireIllegal;
	if (!rearm_ready(o))
		return kFireRearm;
	if (o.ammo == 0)
		return kFireAmmo;
	if (tt && h.verses[size_t(std::clamp(info(*tt).armor, 0, kArmorCount - 1))] == 0.0)
		return kFireIllegal;
	if (check_range && !in_range(o, target, slot))
		return kFireRange;
	// The class checks after the base (U section 14.2), for an OK result.
	if (o.what == What::Unit) {
		const TypeInfo& ti = info(o);
		const bool moving = o.foot.track_number != -1 || o.foot.has_head_to;
		if (!ti.mobile_fire && moving)
			return kFireMoving;
		if (!w.fire_while_moving && o.foot.has_destination)
			return kFireMoving;
		if (!o.is_firing && o.turret_busy && p.rot == 0)
			return kFireRotating;
		if (!w.omni_fire) {
			const uint16_t facing = ti.turret ? o.secondary_facing.current(frame_) : o.primary_facing.current(frame_);
			const int off = std::abs(int(int16_t(facing - uint16_t(dir_to(o, target_coords(target))))));
			if (off > (p.rot > 0 ? 0x1000 : 0x800))
				return kFireFacing;
		}
	} else if (o.what == What::Infantry) {
		if (o.foot.has_head_to)
			return kFireMoving;
	} else if (o.what == What::Building) {
		const TypeInfo& ti = info(o);
		if (ti.turret) {
			const int off = std::abs(int(int16_t(o.primary_facing.current(frame_) - uint16_t(dir_to(o, target_coords(target))))));
			if (off > 0x800)
				return kFireFacing;
		}
	}
	return kFireOk;
}

ObjectId Simulation::spawn_bullet(const BulletState& state, Coord at)
{
	Object b;
	b.id = static_cast<ObjectId>(objects_.size()) + 1;
	b.what = What::Bullet;
	b.type_info = -1;
	b.house = state.owner_house;
	b.location = at;
	b.health = 1;
	b.bullet = state;
	objects_.push_back(b);
	return b.id;
}

Coord Simulation::random_coords_near(Coord c, int radius)
{
	// MapClass::GetRandomCoordsNear: a random offset up to 'radius' leptons in a random direction.
	const double angle = ra::fpu::mul(double(random_->ranged(0, 0x7FFFFFFE)), 2.9260721e-9); // 2*pi / 2^31
	const double dist = double(random_->ranged(0, radius));
	Coord r = c;
	r.x += ra::fpu::ftol(ra::fpu::mul(dist, ra::fastmath::cos(angle)));
	r.y -= ra::fpu::ftol(ra::fpu::mul(dist, ra::fastmath::sin(angle)));
	return r;
}

ObjectId Simulation::fire(Object& o, const Target& target, int slot)
{
	const int wi = weapon_of(o, slot);
	if (wi < 0 || target.is_none())
		return 0;
	const WeaponInfo w = combat_.weapon_info(wi);
	if (!w.valid())
		return 0;
	const Object* tt = target.is_cell() ? nullptr : find(target.id);
	if (!target.is_cell() && (!tt || tt->in_limbo))
		return 0;
	const ProjectileInfo& p = combat_.projectile_info(w.projectile);

	if (w.suicide) {
		receive_damage(o, info(o).strength, 0, combat_.warhead(world_->rules.section("General").get_string("C4Warhead")), o.id, o.house);
		return 0;
	}

	// The aim point (6.2 step 6) and the muzzle. The muzzle should come from the art's FLH and the
	// facing; the firer's own position stands in for it.
	Coord aim = w.area_fire ? ra::cell_center(o.cell(), o.location.z) : target_coords(target);
	if (tt)
		aim.z = tt->location.z;
	else
		aim.z = ground_z(*world_, aim);
	const Coord muzzle = o.location;

	// Damage (step 7).
	int dmg = w.damage;
	if (dmg > 0) {
		dmg = ra::fpu::ftol(ra::fpu::mul(ra::fpu::mul(1.0, 1.0), double(dmg)));
		if (has_ability(o, 2)) // FIREPOWER
			dmg = ra::fpu::ftol(ra::fpu::mul(double(dmg), combat_.rules().veteran_combat));
	}

	// Bullet speed (step 9, 6.3).
	const double reach = dist2d(muzzle, aim);
	int speed;
	if (p.rot == 0) {
		const double g = double(combat_.rules().gravity);
		speed = ra::fpu::ftol(ra::fastmath::sqrt(ra::fpu::mul(ra::fpu::mul(reach, g), 1.2)));
	} else {
		speed = w.speed;
	}

	// Scatter (step 10).
	if (p.inaccurate && p.arcing) {
		const int bs = combat_.rules().ballistic_scatter;
		int r;
		if (p.flak_scatter && !p.inviso)
			r = ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(double(random_->ranged(0, bs)), dist3d(muzzle, aim)), double(std::max(1, w.range))));
		else
			r = random_->ranged(bs / 2, bs);
		const double theta = scatter_angle(*random_);
		aim.x += ra::fpu::ftol(ra::fpu::mul(double(r), ra::fastmath::cos(theta)));
		aim.y -= ra::fpu::ftol(ra::fpu::mul(double(r), ra::fastmath::sin(theta)));
	}

	// Launch velocity (step 11): along the line to the aim point; arcing shots take the ballistic angle.
	const double dx = double(aim.x - muzzle.x), dy = double(aim.y - muzzle.y), dz = double(aim.z - muzzle.z);
	const double horiz = std::max(reach, 0.05);
	double pitch;
	if (p.arcing) {
		// 0x48A8D0: the low arc (the high arc for lobbers).
		const double v = double(std::max(speed, 1)), g = double(combat_.rules().gravity);
		const double h = dz;
		const double A = ra::fpu::sub(ra::fpu::mul(v, v), ra::fpu::mul(h, g));
		const double D = ra::fpu::sub(ra::fpu::sub(ra::fpu::mul(ra::fpu::mul(v, v), ra::fpu::mul(v, v)), ra::fpu::mul(ra::fpu::mul(2.0 * v * v, h), g)), ra::fpu::mul(ra::fpu::mul(g, g), ra::fpu::mul(horiz, horiz)));
		const double B = ra::fpu::mul(2.0, ra::fpu::add(ra::fpu::div(ra::fpu::mul(h, h), ra::fpu::mul(horiz, horiz)), 1.0));
		if (D < 0.0 || B == 0.0)
			return 0;
		const double root = ra::fastmath::sqrt(D);
		const double x = ra::fpu::div(w.lobber ? ra::fpu::sub(A, root) : ra::fpu::add(A, root), B);
		if (x < 0.0)
			return 0;
		pitch = ra::fastmath::acos(std::min(1.0, ra::fpu::div(ra::fastmath::sqrt(x), v)));
	} else if (p.vertical) {
		pitch = dz > 0 ? kPi / 2 : -kPi / 2;
	} else if (horiz > 200.0) {
		const double adj = (tt && tt->what == What::Building) ? 0.0 : 20.0;
		pitch = ra::fastmath::atan2(dz < 0 ? -(std::abs(dz) - adj) : (std::abs(dz) - adj), horiz);
	} else {
		pitch = 0.0;
	}
	const double heading = ra::fastmath::atan2(-dy, dx);
	const double sp = double(speed);
	BulletState bs;
	bs.owner = o.id;
	bs.owner_house = o.house;
	bs.target = target;
	bs.weapon = wi;
	bs.projectile = w.projectile;
	bs.warhead = w.warhead;
	bs.damage = dmg;
	bs.speed = speed;
	bs.vx = ra::fpu::mul(sp, ra::fpu::mul(ra::fastmath::cos(pitch), ra::fastmath::cos(heading)));
	bs.vy = -ra::fpu::mul(sp, ra::fpu::mul(ra::fastmath::cos(pitch), ra::fastmath::sin(heading)));
	bs.vz = ra::fpu::mul(sp, ra::fastmath::sin(pitch));
	if (p.rot > 0) {
		// Homing: the velocity is a unit vector that Update scales every frame.
		const double len = ra::fastmath::sqrt(bs.vx * bs.vx + bs.vy * bs.vy + bs.vz * bs.vz);
		if (len > 0.0) {
			bs.vx /= len;
			bs.vy /= len;
			bs.vz /= len;
		}
	}
	bs.target_coords = aim;
	bs.source = muzzle;
	bs.arm_start = int(frame_);
	bs.arm_delay = (tt && tt->what == What::Aircraft) ? 0 : p.arm;
	bs.last_distance = dist3d(muzzle, aim);
	if (p.inviso) {
		// An instant-hit projectile warps to the target; the next update detonates it.
		bs.vx = bs.vy = bs.vz = 0.0;
		bs.speed = 0;
	}
	const ObjectId id = spawn_bullet(bs, p.inviso ? (tt ? target_coords(target) : aim) : muzzle);

	// After the launch (step 12): burst and rearm.
	o.burst_index++;
	const int rof = get_rof(o, slot);
	o.rearm_start = int(frame_);
	o.rearm_delay = rof;
	o.burst_index %= w.burst;
	if (o.ammo > 0)
		--o.ammo;
	return id;
}

// ---------------------------------------------------------------------------------------------
// Projectiles

void Simulation::update_bullet(Object& b)
{
	BulletState& s = b.bullet;
	const ProjectileInfo& p = combat_.projectile_info(s.projectile);
	++s.age;
	Coord& pos = b.location;
	const Object* owner = find(s.owner);
	const Object* target_obj = s.target.is_cell() ? nullptr : find(s.target.id);
	bool hit = false;
	bool direct_hit = false;

	if (p.inviso) {
		explode_bullet(b, true);
		b.alive = false;
		return;
	}

	if (p.rot < 1) {
		const double speed2 = s.vx * s.vx + s.vy * s.vy + s.vz * s.vz;
		const bool near_zero = speed2 < 64.0;
		if (!p.vertical) {
			// Ballistic flight: gravity pulls, then the position moves by the truncated velocity.
			const double g = p.floater ? double(combat_.rules().gravity) / 2.0 : double(combat_.rules().gravity);
			s.vz = ra::fpu::sub(s.vz, g);
			pos.x += ra::fpu::ftol(s.vx);
			pos.y += ra::fpu::ftol(s.vy);
			pos.z += ra::fpu::ftol(s.vz);
			const int floor = ground_z(*world_, pos);
			const Cell cell = ra::cell_of(pos);
			if (!ra::world::MapGrid::in_grid(cell.x, cell.y) || !world_->grid.at(cell).exists) {
				b.alive = false; // off the map: removed without exploding
				return;
			}
			if (pos.z < floor && floor - pos.z <= 150)
				hit = true;
			const ObjectId bld = building_at_[cell_index(cell)];
			if (bld != 0 && bld != s.owner) {
				const Object* bo = find(bld);
				if (bo && bo->alive && !allied(bo->house, s.owner_house))
					hit = true;
			}
			if (pos.z < floor - 150)
				hit = true; // went through the ground
		} else {
			s.speed = std::min(s.speed + p.acceleration, combat_.weapon_info(s.weapon).speed);
			pos.z += ra::fpu::ftol(s.vz);
			if (pos.z > p.detonation_altitude && s.vz > 0)
				hit = true;
			if (pos.z < ground_z(*world_, pos))
				hit = true;
		}
		// Arrival: the target's cell (not for vertical shots) when low enough.
		if (!hit && !p.vertical) {
			const Cell tc = ra::cell_of(s.target_coords);
			if (ra::cell_of(pos) == tc && pos.z - ground_z(*world_, pos) < 2 * ra::world::kLevelHeight)
				hit = true;
		}
		// A technos' body in the new cell: within 127 leptons of the bullet, not the owner or an ally.
		if (!hit) {
			const Cell cell = ra::cell_of(pos);
			if (ra::world::MapGrid::in_grid(cell.x, cell.y)) {
				const CellOccupancy occ = occupancy_[cell_index(cell)];
				for (ObjectId id : { occ.vehicle, occ.spot[0], occ.spot[1], occ.spot[2], occ.spot[3], occ.spot[4] }) {
					const Object* t = find(id);
					if (!t || !t->alive || t->id == s.owner || allied(t->house, s.owner_house))
						continue;
					if (dist3d(pos, t->location) <= 127.0) {
						hit = true;
						direct_hit = true;
						if (!p.inaccurate)
							pos = t->location;
						break;
					}
				}
			}
		}
		if (!hit && near_zero && pos.z - ground_z(*world_, pos) <= 9)
			hit = true;
	} else {
		// Homing missile (the model of 7.2 step 5; the steering function 0x5B20F0 is approximated).
		const Coord aim = target_obj ? target_coords(s.target) : s.target_coords;
		s.target_coords = aim;
		double speed = double(s.speed);
		if (s.course_lock && s.course_lock_frames > 0)
			--s.course_lock_frames;
		if (s.course_lock && (s.speed > 39))
			s.course_lock = false;
		const int accel = p.acceleration;
		const int max_speed = combat_.weapon_info(s.weapon).speed;
		if (s.speed < max_speed)
			s.speed = std::min(max_speed, s.speed + accel);
		else if (s.speed > max_speed)
			s.speed = std::max(max_speed, s.speed - accel / 2);
		speed = double(s.speed);
		// Turn toward the aim point by at most ROT per frame.
		const double wobble = ra::fastmath::sin(ra::fpu::mul(ra::fpu::div(double((b.id + int(frame_)) % 15), 15.0), 2.0 * kPi));
		int turn = ra::fpu::ftol(ra::fpu::mul(ra::fpu::add(ra::fpu::mul(wobble, combat_.rules().missile_rot_var), 1.0), double(p.rot)));
		if (dist3d(pos, aim) < 256.0)
			turn = ra::fpu::ftol(ra::fpu::mul(double(turn), 1.5));
		if (s.course_lock)
			turn = 0;
		double tx = double(aim.x - pos.x), ty = double(aim.y - pos.y), tz = double(aim.z - pos.z);
		const double tl = ra::fastmath::sqrt(tx * tx + ty * ty + tz * tz);
		if (tl > 0.0) {
			tx /= tl;
			ty /= tl;
			tz /= tl;
			const double max_angle = double(turn) * 256.0 * 2.0 * kPi / 65536.0;
			const double dot = std::clamp(s.vx * tx + s.vy * ty + s.vz * tz, -1.0, 1.0);
			const double angle = ra::fastmath::acos(dot);
			if (angle <= max_angle || angle < 1e-6) {
				s.vx = tx;
				s.vy = ty;
				s.vz = tz;
			} else {
				const double k = max_angle / angle;
				double nx = s.vx + (tx - s.vx) * k, ny = s.vy + (ty - s.vy) * k, nz = s.vz + (tz - s.vz) * k;
				const double nl = ra::fastmath::sqrt(nx * nx + ny * ny + nz * nz);
				if (nl > 0.0) {
					s.vx = nx / nl;
					s.vy = ny / nl;
					s.vz = nz / nl;
				}
			}
		}
		pos.x += ra::fpu::ftol(ra::fpu::mul(s.vx, speed));
		pos.y += ra::fpu::ftol(ra::fpu::mul(s.vy, speed));
		pos.z += ra::fpu::ftol(ra::fpu::mul(s.vz, speed));
		const Cell cell = ra::cell_of(pos);
		if (!ra::world::MapGrid::in_grid(cell.x, cell.y) || !world_->grid.at(cell).exists) {
			b.alive = false;
			return;
		}
		const double remaining = dist3d(pos, aim);
		if (remaining <= speed / 2.0 || pos.z - ground_z(*world_, pos) < 1) {
			hit = true;
			direct_hit = target_obj != nullptr;
			if (!p.airburst)
				pos = aim;
		} else if (!target_obj && !s.target.is_cell() && pos.z >= combat_.rules().missile_safety_altitude) {
			hit = true;
		}
		// The miss detector: the closing distance per frame is summed; a missile that stops closing orbits.
		const double closing = s.last_distance - remaining;
		if (s.age <= 60)
			s.near_sum += closing;
		else
			s.near_sum = ra::fpu::add(ra::fpu::mul(s.near_sum, 59.0 / 60.0), closing);
		if (s.age > 60 && s.near_sum >= 0.0 && s.near_sum < 60.0 && !p.airburst && !p.very_high)
			hit = true;
		// Proximity (7.2 step 7).
		if (!hit && (p.rot > 0 || p.ranged) && int(frame_) - s.arm_start >= s.arm_delay) {
			if (remaining / 2.0 < 32.0) {
				hit = true;
				direct_hit = true;
			} else if (remaining < 256.0 && remaining > s.last_distance) {
				hit = true;
			}
		}
		s.last_distance = remaining;
	}

	if (p.degenerates && s.damage > 5)
		--s.damage;
	(void)owner;
	if (hit) {
		explode_bullet(b, direct_hit);
		b.alive = false;
	}
}

void Simulation::explode_bullet(Object& b, bool direct_hit)
{
	BulletState& s = b.bullet;
	const ProjectileInfo& p = combat_.projectile_info(s.projectile);
	const Object* target_obj = s.target.is_cell() ? nullptr : find(s.target.id);
	Coord at = b.location;
	if (!p.inaccurate && target_obj && !p.airburst) {
		const Coord tc = target_coords(s.target);
		if (dist3d(at, tc) <= 32.0)
			at = tc;
	}
	(void)direct_hit;
	if (p.airburst) {
		detonate(b, at);
		return;
	}
	const int cluster = std::max(1, p.cluster);
	for (int i = 0; i < cluster; ++i) {
		detonate(b, at);
		if (i + 1 < cluster)
			at = random_coords_near(at, random_->ranged(256, 512));
	}
}

void Simulation::detonate(Object& b, Coord at)
{
	BulletState& s = b.bullet;
	const WarheadInfo& w = combat_.warhead_info(s.warhead);
	// Screen shake: the amounts are drawn from the synchronised generator.
	(void)random_->ranged(w.shake_x_lo, w.shake_x_hi);
	(void)random_->ranged(w.shake_y_lo, w.shake_y_hi);
	damage_area(at, s.damage, s.owner, s.warhead, s.owner_house);
	// Debris: the count is drawn; the pieces and the explosion animation are visual.
	(void)random_->ranged(w.min_debris, w.max_debris - 1);
}

} // namespace ra::sim


namespace ra::sim {

bool Simulation::has_ability(const Object& o, int ability) const
{
	if (o.what == What::Bullet || o.what == What::Building)
		return false;
	const TypeInfo& t = info(o);
	const uint32_t bit = 1u << ability;
	if (is_veteran(o))
		return (t.veteran_abilities & bit) != 0;
	if (is_elite(o))
		return ((t.veteran_abilities | t.elite_abilities) & bit) != 0;
	return false;
}

bool Simulation::should_self_heal(const Object& o) const
{
	// 0x70BE80: SelfHealing=yes, or the SELF_HEAL ability; every ftol(RepairRate * 900) frames; hurt but alive.
	if (!o.alive || o.what == What::Bullet)
		return false;
	if (!info(o).self_healing && !has_ability(o, 9))
		return false;
	const ra::rules::RuleObject g = world_->rules.section("General");
	const int period = ra::fpu::ftol(ra::fpu::mul(g.has("RepairRate") ? g.get_double("RepairRate") : 0.016, 900.0));
	if (period <= 0 || frame_ % uint32_t(period) != 0)
		return false;
	return o.health > 0 && o.health < info(o).strength;
}

void Simulation::register_destruction(Object& killer, const Object& victim)
{
	// TechnoClass::RegisterDestruction (0x702D40): veterancy for the killer and the scores.
	const TypeInfo& vt = info(victim);
	if (vt.dont_score)
		return;
	int value = vt.cost;
	if (is_veteran(victim))
		value *= 2;
	else if (is_elite(victim))
		value *= 3;
	if (allied(killer.house, victim.house))
		value = 0;
	houses_[size_t(victim.house)].last_hurt_by = killer.house;
	houses_[size_t(killer.house)].point_total += value;
	if (victim.what == What::Building)
		++houses_[size_t(killer.house)].killed_buildings;
	else
		++houses_[size_t(killer.house)].killed_units;
	// The recipient: the killer if it is trainable (open-topped transports, spawners and garrisons come later).
	const TypeInfo& kt = info(killer);
	if (kt.trainable && kt.cost > 0 && killer.what != What::Building) {
		const double gain = ra::fpu::div(double(value), ra::fpu::mul(double(kt.cost), combat_.rules().veteran_ratio));
		killer.veterancy = std::min(ra::fpu::add(killer.veterancy, gain), combat_.rules().veteran_cap);
	}
}

} // namespace ra::sim
