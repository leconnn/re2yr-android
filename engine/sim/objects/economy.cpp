// Money, costs, build time, factories, power, build permission and placement (P3 section 9).
//
// Not here yet (FUTURE_THINGS_TO_UNDERSTAND.md): building upgrades (PowersUpBuilding), the helipad's
// aircraft cost folding, stolen-tech and secret-lab permission, superweapon buildings, the allied-building
// proximity option, power blackouts and drains, silo storage, and the exact exit cell of a finished unit
// (ExitObject): units appear at the first free cell below their factory.

#include "core/fpu.h"
#include "sim/objects/simulation.h"

#include <algorithm>
#include <cstdlib>

namespace ra::sim {

namespace {

using ra::rules::TypeKind;

constexpr int kSteps = 54;

int cat_index(Category c) { return static_cast<int>(c); }

// The index into the house multipliers (Infantry, Units, Aircraft, Buildings, Defenses).
int mult_index(Category c)
{
	switch (c) {
	case Category::Infantry: return 0;
	case Category::Aircraft: return 2;
	case Category::Building: return 3;
	case Category::Defense: return 4;
	default: return 1;
	}
}

int produced_key(TypeRef r) { return int(r.kind) * 100000 + r.type; }

bool contains_name(const std::vector<std::string>& list, const std::string& name)
{
	const std::string n = ra::ini::to_lower(name);
	for (const std::string& s : list)
		if (ra::ini::to_lower(s) == n)
			return true;
	return false;
}

const char* land_name(ra::world::LandType land)
{
	static const char* const names[] = { "Clear", "Road", "Water", "Rock", "Wall", "Tiberium", "Beach", "Rough", "Ice", "Railroad", "Tunnel", "Weeds" };
	const int i = static_cast<int>(land);
	return i >= 0 && i < 12 ? names[i] : "Clear";
}

} // namespace

Category Simulation::category_of(const TypeInfo& t, TypeKind kind)
{
	switch (kind) {
	case TypeKind::Infantry: return Category::Infantry;
	case TypeKind::Aircraft: return Category::Aircraft;
	case TypeKind::Building: return t.combat_building ? Category::Defense : Category::Building;
	default: return t.naval ? Category::Naval : Category::Unit;
	}
}

TypeRef Simulation::type_ref(TypeKind kind, const char* name) const
{
	return { kind, world_->rules.type_index(kind, name) };
}

const TypeInfo& Simulation::type_info_of(TypeRef r) const
{
	for (const TypeInfo& t : types_)
		if (t.kind == r.kind && t.index == r.type)
			return t;
	// Types are read when first used; economy asks about types no object has used yet.
	auto* self = const_cast<Simulation*>(this);
	self->types_.push_back(TypeInfo::read(world_->rules, r.kind, r.type));
	TypeInfo& added = self->types_.back();
	added.weapon[0] = combat_.weapon(added.primary_name);
	added.weapon[1] = combat_.weapon(added.secondary_name);
	return added;
}

void Simulation::take_money(int house, int amount)
{
	House& h = houses_[size_t(house)];
	// 0x4F9790: the whole balance when short (the rest would come from silo storage, which isn't kept).
	h.balance -= std::min(amount, h.balance);
	h.credits_spent += amount;
}

// ---------------------------------------------------------------------------------------------
// Cost

int Simulation::actual_cost(int house, TypeRef r) const
{
	const TypeInfo& t = type_info_of(r);
	const Category c = category_of(t, r.kind);
	const House& h = houses_[size_t(house)];
	// houseFactoryMult (0x50BEB0): the product of the FactoryPlant bonuses of the house's buildings.
	double factory_mult = 1.0;
	for (const Object& o : objects_)
		if (o.alive && o.what == What::Building && o.house == house && !o.in_limbo && info(o).factory_plant)
			factory_mult = ra::fpu::mul(factory_mult, info(o).cost_bonus[mult_index(c)]);
	const double country = h.econ.cost_mult[mult_index(c)];
	int cost = ra::fpu::ftol(ra::fpu::mul(ra::fpu::mul(double(t.cost), factory_mult), country));
	if (r.kind == TypeKind::Building && !t.free_unit.empty()) {
		const TypeRef free = type_ref(TypeKind::Vehicle, t.free_unit.c_str());
		if (free.valid())
			cost = std::max(0, cost + actual_cost(house, free));
	}
	return cost;
}

int Simulation::refund(int house, TypeRef r, bool full) const
{
	const TypeInfo& t = type_info_of(r);
	const Category c = category_of(t, r.kind);
	const House& h = houses_[size_t(house)];
	const double country = h.econ.cost_mult[mult_index(c)];
	if (t.soylent != 0)
		return ra::fpu::ftol(ra::fpu::mul(double(t.soylent), country));
	double factory_mult = 1.0;
	for (const Object& o : objects_)
		if (o.alive && o.what == What::Building && o.house == house && !o.in_limbo && info(o).factory_plant)
			factory_mult = ra::fpu::mul(factory_mult, info(o).cost_bonus[mult_index(c)]);
	int r2 = ra::fpu::ftol(ra::fpu::mul(ra::fpu::mul(double(t.cost), country), factory_mult));
	if (h.human) {
		const ra::rules::RuleObject g = world_->rules.section("General");
		const double pct = g.has("RefundPercent") ? g.get_double("RefundPercent") : 0.5;
		r2 = ra::fpu::ftol(ra::fpu::mul(double(r2), full ? 1.0 : pct));
	}
	return r2;
}

// ---------------------------------------------------------------------------------------------
// Power

int Simulation::power_output(int house) const
{
	int total = 0;
	for (const Object& o : objects_) {
		if (!o.alive || o.what != What::Building || o.house != house || o.in_limbo)
			continue;
		const TypeInfo& t = info(o);
		if (t.power_bonus > 0)
			total += ra::fpu::ftol(ra::fpu::div(ra::fpu::mul(double(t.power_bonus), double(o.health)), double(std::max(1, t.strength))));
	}
	return total;
}

int Simulation::power_drain(int house) const
{
	int total = 0;
	for (const Object& o : objects_)
		if (o.alive && o.what == What::Building && o.house == house && !o.in_limbo)
			total += info(o).power_drain;
	return total;
}

double Simulation::power_percentage(int house) const
{
	const int out = power_output(house), drain = power_drain(house);
	if (out >= drain || drain == 0)
		return 1.0;
	if (out == 0)
		return 0.0;
	return ra::fpu::div(double(out), double(drain));
}

// ---------------------------------------------------------------------------------------------
// Build time

int Simulation::time_to_build(int house, TypeRef r) const
{
	const TypeInfo& type = type_info_of(r);
	const Category c = category_of(type, r.kind);
	const House& h = houses_[size_t(house)];
	const ra::rules::RuleObject g = world_->rules.section("General");
	const auto num = [&](const char* key, double fallback) { return g.has(key) ? g.get_double(key) : fallback; };
	// GetBuildSpeed (0x711EE0).
	int t = ra::fpu::ftol(ra::fpu::mul(ra::fpu::mul(double(type.cost), num("BuildSpeed", 1.0)), 0.9));
	t = ra::fpu::ftol(ra::fpu::mul(double(t), h.econ.build_time_mult[mult_index(c)]));
	t = ra::fpu::ftol(ra::fpu::mul(double(t), type.build_time_multiplier));
	const double p = power_percentage(house);
	double f = ra::fpu::sub(1.0, ra::fpu::mul(ra::fpu::sub(1.0, p), num("LowPowerPenaltyModifier", 1.0)));
	f = std::max(f, num("MinLowPowerProductionSpeed", 0.5));
	if (p < 1.0)
		f = std::min(f, num("MaxLowPowerProductionSpeed", 0.9));
	if (f == 0.0)
		f = 0.01;
	t = ra::fpu::ftol(ra::fpu::div(double(t), f));
	// The number of factories of this kind speeds production up (MultipleFactory).
	int n = 0;
	const int want = c == Category::Infantry ? 2 : c == Category::Aircraft ? 4 : (c == Category::Building || c == Category::Defense) ? 1 : 3;
	for (const Object& o : objects_)
		if (o.alive && o.what == What::Building && o.house == house && !o.in_limbo && info(o).factory_kind == want && info(o).naval == type.naval)
			++n;
	const double multiple = num("MultipleFactory", 1.0);
	if (multiple > 0.0)
		for (int i = 1; i < n; ++i)
			t = ra::fpu::ftol(ra::fpu::mul(double(t), multiple));
	if (r.kind == TypeKind::Building && type.wall)
		t = ra::fpu::ftol(ra::fpu::mul(double(t), num("WallBuildSpeedCoefficient", 0.5)));
	return t;
}

// ---------------------------------------------------------------------------------------------
// Build permission

int Simulation::owned_count(int house, TypeRef r) const
{
	int n = 0;
	for (const Object& o : objects_)
		if (o.alive && !o.in_limbo && o.house == house && o.kind == r.kind && o.type == r.type && o.what != What::Bullet)
			++n;
	return n;
}

int Simulation::can_build(int house, TypeRef r, bool ignore_limits, bool include_queued) const
{
	const TypeInfo& t = type_info_of(r);
	const House& h = houses_[size_t(house)];
	const Category cat = category_of(t, r.kind);
	const ra::rules::RuleObject g = world_->rules.section("General");

	const auto active_building = [&](const std::string& name) {
		const TypeRef b = type_ref(TypeKind::Building, name.c_str());
		return b.valid() && owned_count(house, b) > 0;
	};
	const auto any_in_list = [&](const char* key) {
		for (const std::string& name : g.get_list(key))
			if (active_building(name))
				return true;
		return false;
	};

	bool skip_to_limits = false;
	for (const std::string& name : t.prerequisite_override)
		if (active_building(name))
			skip_to_limits = true;
	if (!skip_to_limits) {
		if (t.tech_level == -1)
			return 0;
		// Owner mask: anyone when empty, otherwise the house's country must be listed.
		if (!t.owners.empty() && !contains_name(t.owners, h.name))
			return 0;
		if (contains_name(t.forbidden_houses, h.name))
			return 0;
		if (t.tech_level > h.tech_level)
			return 0;
		if (!h.human)
			return 1; // the AI base planner decides what it builds
		for (const std::string& pre : t.prerequisites) {
			const std::string p = ra::ini::to_lower(pre);
			bool met;
			if (p == "power") met = any_in_list("PrerequisitePower");
			else if (p == "factory") met = any_in_list("PrerequisiteFactory");
			else if (p == "barracks") met = any_in_list("PrerequisiteBarracks");
			else if (p == "radar") met = any_in_list("PrerequisiteRadar");
			else if (p == "tech") met = any_in_list("PrerequisiteTech");
			else if (p == "proc") met = any_in_list("PrerequisiteProc");
			else met = active_building(pre);
			if (!met)
				return 0;
		}
	}
	if (ignore_limits)
		return 1;

	// Build limit.
	int queued = 0;
	const Factory& f = h.factories[size_t(cat)];
	if (f.active && f.item == r)
		++queued;
	for (const TypeRef& q : f.queue)
		if (q == r)
			++queued;
	if (t.build_limit > 0 && t.build_limit != 0x7FFFFFFF) {
		if (owned_count(house, r) + queued >= t.build_limit) {
			if (include_queued && f.active && f.item == r)
				return 1;
			return -1;
		}
	} else if (t.build_limit < 0) {
		const auto it = h.produced.find(produced_key(r));
		const int ever = it == h.produced.end() ? 0 : it->second;
		if (ever + queued >= -t.build_limit)
			return 0;
	}
	return 1;
}

// ---------------------------------------------------------------------------------------------
// Factories

bool Simulation::start_production(int house, TypeRef r)
{
	House& h = houses_[size_t(house)];
	const TypeInfo& t = type_info_of(r);
	const Category cat = category_of(t, r.kind);
	Factory& f = h.factories[size_t(cat)];
	const ra::rules::RuleObject g = world_->rules.section("General");
	const int max_queue = g.has("MaximumQueuedObjects") ? g.get_int("MaximumQueuedObjects") : 5;
	if (can_build(house, r, false, false) != 1)
		return false;
	if (f.active) {
		if (int(f.queue.size()) + 1 >= max_queue)
			return false;
		f.queue.push_back(r);
		return true;
	}
	f.active = true;
	f.item = r;
	f.step = 0;
	f.suspended = false;
	f.on_hold = false;
	f.balance = actual_cost(house, r);
	f.rate = std::clamp(time_to_build(house, r) / kSteps, 1, 255);
	f.timer_start = int(frame_);
	return true;
}

void Simulation::suspend_production(int house, Category c)
{
	Factory& f = houses_[size_t(house)].factories[size_t(c)];
	f.suspended = true;
}

void Simulation::resume_production(int house, Category c)
{
	Factory& f = houses_[size_t(house)].factories[size_t(c)];
	if (!f.active || f.done())
		return;
	// Unsuspend recomputes the rate from the current TimeToBuild.
	f.suspended = false;
	f.rate = std::clamp(time_to_build(house, f.item) / kSteps, 1, 255);
	f.timer_start = int(frame_);
}

void Simulation::abandon_production(int house, Category c)
{
	House& h = houses_[size_t(house)];
	Factory& f = h.factories[size_t(c)];
	if (!f.active)
		return;
	// The refund is what was paid so far: actual cost minus the balance still owed.
	give_money(house, actual_cost(house, f.item) - f.balance);
	f = Factory {};
}

void Simulation::update_factories()
{
	// FactoryClass::Update (0x4C9B20), for every house's factories in index order.
	for (size_t hi = 0; hi < houses_.size(); ++hi) {
		House& h = houses_[hi];
		for (Factory& f : h.factories) {
			if (!f.active || f.suspended || f.done())
				continue;
			if (int(frame_) - f.timer_start < f.rate)
				continue;
			++f.step;
			f.timer_start = int(frame_);
			const int per_step = f.step >= kSteps ? f.balance : f.balance / (kSteps - f.step);
			if (h.balance < per_step) {
				f.on_hold = true;
				--f.step;
			} else {
				take_money(int(hi), per_step);
				f.balance -= per_step;
				f.on_hold = false;
			}
			if (f.step == kSteps) {
				f.suspended = true;
				take_money(int(hi), f.balance);
				f.balance = 0;
			}
		}
	}
}

void Simulation::update_houses()
{
	// Periodic income from buildings that produce cash (BuildingClass::Update, P3 section 9.1).
	for (size_t hi = 0; hi < houses_.size(); ++hi) {
		House& h = houses_[hi];
		for (const Object& o : objects_) {
			if (!o.alive || o.what != What::Building || o.house != int(hi) || o.in_limbo)
				continue;
			const TypeInfo& t = info(o);
			if (t.produce_cash_amount == 0 || t.produce_cash_delay <= 0)
				continue;
			auto it = h.cash_timers.find(o.id);
			if (it == h.cash_timers.end()) {
				h.cash_timers[o.id] = int(frame_);
				continue;
			}
			if (int(frame_) - it->second < t.produce_cash_delay)
				continue;
			it->second = int(frame_);
			if (h.econ.multiplay_passive)
				continue;
			if (t.powered && t.power_drain > 0 && power_percentage(int(hi)) < 1.0)
				continue; // not IsPowerOnline
			if (t.produce_cash_amount > 0)
				give_money(int(hi), t.produce_cash_amount);
			else
				take_money(int(hi), -t.produce_cash_amount);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Placement

bool Simulation::can_place(int house, TypeRef r, Cell origin, bool check_proximity) const
{
	if (r.kind != TypeKind::Building)
		return false;
	const TypeInfo& t = type_info_of(r);
	if (!t.place_anywhere) {
		int fits = 0;
		for (int y = 0; y < t.foundation_h; ++y)
			for (int x = 0; x < t.foundation_w; ++x) {
				const Cell c { int16_t(origin.x + x), int16_t(origin.y + y) };
				bool ok = world_->grid.in_local(c.x, c.y, true) && world_->grid.at(c).exists;
				if (ok) {
					const size_t i = cell_index(c);
					const CellOccupancy& occ = occupancy_[i];
					ok = building_at_[i] == 0 && !blocked_terrain(c) && occ.vehicle == 0 && occ.spot[0] == 0 && occ.spot[1] == 0
						&& occ.spot[2] == 0 && occ.spot[3] == 0 && occ.spot[4] == 0;
				}
				if (ok) {
					const ra::world::MapCell& mc = world_->grid.at(c);
					// Ore doesn't block building; any other overlay does.
					ok = (overlay_[cell_index(c)] < 0 || ore_type(c) >= 0) && mc.slope == 0;
					if (ok) {
						if (t.naval)
							ok = mc.land == ra::world::LandType::Water;
						else
							ok = world_->rules.section(land_name(mc.land)).get_bool("Buildable");
					}
				}
				if (ok)
					++fits;
			}
		if (fits != t.foundation_w * t.foundation_h)
			return false;
	}
	if (!check_proximity)
		return true;
	// Some cell in the ring of width Adjacent + 1 around the foundation holds a BaseNormal building of
	// the same house.
	const int ring = t.adjacent + 1;
	for (const Object& o : objects_) {
		if (!o.alive || o.what != What::Building || o.house != house || o.in_limbo)
			continue;
		const TypeInfo& bt = info(o);
		if (!bt.base_normal)
			continue;
		// The two rectangles, the foundation grown by 'ring', overlap?
		const int bx0 = o.cell().x, by0 = o.cell().y, bx1 = bx0 + bt.foundation_w - 1, by1 = by0 + bt.foundation_h - 1;
		const int ax0 = origin.x - ring, ay0 = origin.y - ring, ax1 = origin.x + t.foundation_w - 1 + ring, ay1 = origin.y + t.foundation_h - 1 + ring;
		if (bx1 >= ax0 && bx0 <= ax1 && by1 >= ay0 && by0 <= ay1)
			return true;
	}
	return false;
}

ObjectId Simulation::place_building(int house, Category c, Cell origin)
{
	House& h = houses_[size_t(house)];
	Factory& f = h.factories[size_t(c)];
	if (!f.active || !f.done() || f.item.kind != TypeKind::Building)
		return 0;
	if (!can_place(house, f.item, origin, false))
		return 0;
	const TypeRef item = f.item;
	const ObjectId id = spawn(item.kind, item.type, house, origin, Facing {}, 256);
	++h.produced[produced_key(item)];
	// FactoryClass::CompletedProduction: the next queued item starts.
	std::vector<TypeRef> rest = std::move(f.queue);
	f = Factory {};
	if (!rest.empty()) {
		const TypeRef next = rest.front();
		rest.erase(rest.begin());
		start_production(house, next);
		h.factories[size_t(c)].queue = std::move(rest);
	}
	return id;
}

ObjectId Simulation::exit_unit(int house, Category c)
{
	House& h = houses_[size_t(house)];
	Factory& f = h.factories[size_t(c)];
	if (!f.active || !f.done() || f.item.kind == TypeKind::Building)
		return 0;
	const int want = c == Category::Infantry ? 2 : c == Category::Aircraft ? 4 : 3;
	// PROVISIONAL: the first free cell below the first factory building of the house (BuildingClass
	// exit cells come from the art and are not decoded here).
	for (const Object& o : objects_) {
		if (!o.alive || o.what != What::Building || o.house != house || o.in_limbo || info(o).factory_kind != want)
			continue;
		const TypeInfo& bt = info(o);
		for (int dy = bt.foundation_h; dy < bt.foundation_h + 4; ++dy)
			for (int dx = 0; dx < std::max(1, bt.foundation_w); ++dx) {
				const Cell cell { int16_t(o.cell().x + dx), int16_t(o.cell().y + dy) };
				if (!world_->grid.at(cell).exists || blocked_terrain(cell) || cell_has_vehicle(cell, 0))
					continue;
				const TypeRef item = f.item;
				const ObjectId id = spawn(item.kind, item.type, house, cell, Facing {}, 256);
				++h.produced[produced_key(item)];
				std::vector<TypeRef> rest = std::move(f.queue);
				f = Factory {};
				if (!rest.empty()) {
					const TypeRef next = rest.front();
					rest.erase(rest.begin());
					start_production(house, next);
					h.factories[size_t(c)].queue = std::move(rest);
				}
				return id;
			}
	}
	return 0;
}

} // namespace ra::sim

namespace ra::sim {

bool Simulation::sell_building(Object& b)
{
	// BuildingClass::Mission_Selling (0x449C30) without the animation: the building goes at once and
	// the owner gets the refund (U section 9.10). Crew survivors and stored ore are not here yet.
	if (!b.alive || b.what != What::Building)
		return false;
	b.repairing = false;
	const TypeRef r { b.kind, b.type };
	give_money(b.house, refund(b.house, r, false));
	destroy(b);
	return true;
}

void Simulation::toggle_repair(Object& b)
{
	if (!b.alive || b.what != What::Building)
		return;
	if (b.health >= info(b).strength)
		return;
	b.repairing = !b.repairing;
}

void Simulation::update_building(Object& b)
{
	if (!b.alive || b.in_limbo)
		return;
	if (!b.repairing)
		return;
	// The player's wrench (0x450630): every ftol(RepairRate * 900) frames one step is paid and applied.
	const ra::rules::RuleObject g = world_->rules.section("General");
	const double rate = g.has("RepairRate") ? g.get_double("RepairRate") : 0.016;
	const int period = std::max(1, ra::fpu::ftol(ra::fpu::mul(rate, 900.0)));
	if (frame_ % uint32_t(period) != 0)
		return;
	const TypeInfo& t = info(b);
	const int step = g.has("RepairStep") ? g.get_int("RepairStep") : 5;
	const double percent = g.has("RepairPercent") ? g.get_double("RepairPercent") : 0.25;
	// TechnoTypeClass::GetRepairStepCost (0x7120D0): max(1, ftol(Cost / (Strength / RepairStep) * RepairPercent)).
	const double per = ra::fpu::div(double(t.cost), ra::fpu::div(double(t.strength), double(std::max(1, step))));
	const int cost = std::max(1, ra::fpu::ftol(ra::fpu::mul(per, percent)));
	if (available(b.house) < cost) {
		b.repairing = false; // out of money: repairing stops
		return;
	}
	take_money(b.house, cost);
	b.health = std::min(b.health + step, t.strength);
	if (b.health >= t.strength)
		b.repairing = false;
}

} // namespace ra::sim
