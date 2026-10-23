#pragma once

// The running game's objects and what happens to them each frame (Phase 4).
//
// Simulation owns the objects, the cell occupancy and the local player's shroud, executes the events
// that give orders (MEGAMISSION: move, stop, guard), and updates movement every frame through the
// game loop's LogicObjects step.
//
// Movement follows the decoded originals (U section 10, 4B-fix): FacingClass turning, the height rule
// (vslot 0x1B0), the core of CanEnterCell for units and infantry, the infantry free-spot search, and
// Drive StartOfMove with curved turn tracks. Branches that depend on systems not written yet (gates,
// cloaking, mind control, bridges, tubes, occupiable buildings) are listed in FUTURE_THINGS.

#include "core/random.h"
#include "sim/loop/game_loop.h"
#include "sim/objects/object.h"
#include "sim/objects/type_info.h"
#include "sim/path/astar.h"
#include "sim/path/zones.h"
#include "sim/world/shroud.h"
#include "sim/world/world.h"

#include <array>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace ra::sim {

// A thing that can be built: a type of one of the four buildable kinds.
struct TypeRef {
	ra::rules::TypeKind kind = ra::rules::TypeKind::Building;
	int type = -1;
	bool valid() const { return type >= 0; }
	friend bool operator==(const TypeRef&, const TypeRef&) = default;
};

// Production categories: one FactoryClass per house and category (P3 section 9.4).
enum class Category : int { Building, Defense, Infantry, Unit, Aircraft, Naval, Count };

struct Factory {
	bool active = false;       // producing something
	TypeRef item;              // the object being built
	int step = 0;              // 0 to 54
	int32_t timer_start = 0;   // the step timer
	int rate = 1;              // frames per step
	int balance = 0;           // what is still to be paid (FactoryClass Balance)
	bool suspended = false;    // the "on hold" button, or finished
	bool on_hold = false;      // out of money
	std::vector<TypeRef> queue; // what comes next
	bool done() const { return step >= 54; }
};

struct HouseEconomy {
	double cost_mult[5] = { 1, 1, 1, 1, 1 };       // Infantry, Units, Aircraft, Buildings, Defenses
	double build_time_mult[5] = { 1, 1, 1, 1, 1 };
	double income_mult = 1.0;
	bool multiplay_passive = false;
};

struct House {
	std::string name;
	bool human = true; // human houses issue events; AI houses issue none
	int point_total = 0;        // PointTotal: the score from kills
	int killed_units = 0, killed_buildings = 0;
	int last_hurt_by = -1;      // WhoLastHurtMe
	// Economy (P3 section 9).
	HouseEconomy econ;
	int balance = 0;            // credits
	int credits_spent = 0;
	int tech_level = 10;
	std::array<Factory, size_t(Category::Count)> factories {};
	std::map<int, int> produced;     // FactoryProduced*Types: how many of each type were ever produced
	std::map<int, int> cash_timers;  // building id -> frame the production timer started (ProduceCash)
};

class Simulation final : public EventSink {
public:
	// The world and the random generator must outlive the simulation.
	Simulation(const ra::world::World& world, ra::Random2& random);

	// ---- setup ----
	int house_index(const std::string& name); // adds the house if it is new
	House& house(int index) { return houses_[static_cast<size_t>(index)]; }
	const House& house(int index) const { return houses_[static_cast<size_t>(index)]; }
	void set_local_house(int index) { local_house_ = index; }
	int local_house() const { return local_house_; }

	// Creates an object at a cell and returns its id. 'sub_cell' is for infantry.
	ObjectId spawn(ra::rules::TypeKind kind, int type, int house, Cell cell, Facing facing, int health, int sub_cell = 0);
	// Creates the objects the map starts with (World::placed_objects).
	void spawn_placed_objects();

	// ---- lookup ----
	Object* find(ObjectId id);
	const Object* find(ObjectId id) const;
	const std::deque<Object>& objects() const { return objects_; }
	const TypeInfo& info(const Object& o) const { return types_[static_cast<size_t>(o.type_info)]; }
	// The object standing in a cell (ground level), checking the five infantry spots and the vehicle spot.
	ObjectId occupant(Cell cell, int spot = 0) const;
	// All live objects whose position is inside a lepton rectangle, in logic list order.
	std::vector<ObjectId> objects_in_rect(Coord min, Coord max) const;

	// ---- orders (what a MEGAMISSION event does, ObjectClickedAction / ClickedMission) ----
	// MissionClass: the queue and the scheduler (U section 2.1).
	// QueueMission (0x5B35E0): queue a mission; with 'start_now' it starts at once (CommenceMission 0x5B3570).
	void queue_mission(Object& o, Mission mission, bool start_now);
	void commence_mission(Object& o);
	// MissionClass::AI (0x5B3060): runs the handler of the current mission when its timer has run out and
	// restarts the timer with the delay it returns.
	void mission_ai(Object& o);
	// ftol(Rate * 900) + Random(0, 2) for a mission's [Rate] (the "delay(M)" of U section 9).
	int mission_delay(Mission mission);
	void order_move(Object& o, Cell destination);
	void order_stop(Object& o);
	void order_guard(Object& o);
	// Builds the events for a player's orders (they go to GameLoop::issue).
	static Event move_event(int house, ObjectId unit, Cell destination);
	static Event stop_event(int house, ObjectId unit);
	static Event guard_event(int house, ObjectId unit);
	static Event attack_event(int house, ObjectId unit, Target target);
	// Production and building events (PRODUCE / SUSPEND / ABANDON / PLACE / REPAIR / SELL, P3 section 13.4).
	static Event produce_event(int house, TypeRef item);
	static Event suspend_event(int house, TypeRef item);
	static Event abandon_event(int house, TypeRef item);
	static Event place_event(int house, TypeRef item, Cell cell);
	static Event repair_event(int house, ObjectId building);
	static Event sell_event(int house, ObjectId building);
	void order_attack(Object& o, const Target& target);

	// ---- the game loop ----
	// Hooks the simulation into a loop: the LogicObjects step and the CRC terms.
	void attach(GameLoop& loop);
	void update_objects(); // LogicClass::Update step 15
	void add_crc_terms(FrameCrc& crc) const;

	// EventSink
	int house_count() const override { return static_cast<int>(houses_.size()); }
	bool house_is_human(int house) const override { return houses_[static_cast<size_t>(house)].human; }
	void execute(const Event& event) override;

	// ---- sight ----
	ra::world::ShroudMap& shroud() { return shroud_; }
	const ra::world::ShroudMap& shroud() const { return shroud_; }
	// TechnoClass::See: reveal the area around an object of the local player.
	void reveal_around(const Object& o);

	// ---- pathfinding ----
	// FootClass::UpdatePathfinding for a unit going to 'goal': fills its path. False if there is none.
	bool find_path(Object& o, Cell goal);
	// CellClass +0x4C (0x483C80): the passability class the zones are built from (Zones, U section 11.7).
	int passability(int x, int y) const;
	// MapClass::IsInSameZone (0x56D100) as TechnoClass::IsInSameZoneAsCoords (0x4D3810) asks it.
	bool same_zone(const Object& o, Cell dest) const;
	const ra::sim::path::Zones& zones() const { return zones_; }
	// FootClass::CanEnterCell (vslot 0x1AC) for a unit or infantry moving into 'to' in direction 'dir'.
	// 'level' is the height the unit moves at (-1: the level of 'from').
	ra::sim::path::Move can_enter(const Object& o, Cell to, int dir, Cell from, int level = -1) const;
	// The height rule between neighbouring cells (vslot 0x1B0): false means the move is impossible.
	bool level_rule(Cell from, Cell to, int unit_level) const;
	// CellClass free spot search (0x481180): the point of a free infantry spot in 'cell', starting from
	// the spot that 'near' falls in. Draws from the scenario RNG when it starts from the centre spot.
	bool find_free_spot(Cell cell, Coord near, Coord& out);
	static int spot_of(Coord c); // 0x4810A0
	bool allied(int house_a, int house_b) const { return house_a == house_b; }

	const ra::world::World& world() const { return *world_; }
	uint32_t frame() const { return frame_; }
	void set_frame(uint32_t frame) { frame_ = frame; }

	// The speed a unit moves at this frame, in leptons (GetCurrentSpeed, U section 10.1).
	int current_speed(const Object& o) const;

private:
	friend class UnitSearchMap;

	size_t cell_index(Cell c) const { return size_t(c.y) * ra::world::MapGrid::kSize + size_t(c.x); }
	void occupy(const Object& o, Cell c);
	void vacate(const Object& o, Cell c);
	bool cell_has_vehicle(Cell c, ObjectId except) const;
	bool blocked_terrain(Cell c) const { return blocked_[cell_index(c)] != 0; }

	// Movement.
	int run_mission_handler(Object& o);
	void enter_idle_mode(Object& o);
	void update_foot(Object& o);
	void drive_process(Object& o);
	void walk_process(Object& o);
	void hover_process(Object& o);
	void jumpjet_process(Object& o);
	void fly_process(Object& o);
	// UnitClass::TryCrushCell (0x7416A0) for the cell the crusher is in.
	void try_crush(Object& o);
	// Takes an object out of play (no death effects yet).
	void destroy(Object& o);
	void face_toward(Object& o, Coord target);
	// The firing AI (U section 16).
	void update_targeting(Object& o);       // TechnoClass::Update's targeting timer and zone check
	void unit_fire_step(Object& o);         // 0x736DF0
	void unit_turret_step(Object& o);       // 0x736990
	void infantry_fire_step(Object& o);     // 0x5206B0
	void infantry_stage(Object& o);         // the StageClass of the infantry sequence
	void building_fire_step(Object& o);
	void play_sequence(Object& o, int sequence);
	bool is_clear_to_move(const Object& o, Cell c) const; // CellClass::IsClearToMove 0x4834A0
	int attempt_path(const Object& o, Cell a, Cell b);     // AStarClass::AttemptPath 0x42D170
	void retaliate(Object& victim, ObjectId attacker);
	bool can_retaliate(const Object& o, const Object& attacker) const;
	int dir_to(const Object& o, Coord target) const;       // 16-bit facing toward a point
	void drop_healed_target(Object& o, int slot, What healed_kind);
	int building_attack(Object& o);
	void order_move_to(Object& o, Cell c);
	bool mission_flag(Mission m, const char* key, bool fallback) const;

public:
	// ---- Combat (U sections 3 to 7; Phase 5) ----
	// The ReceiveDamage result codes.
	enum DamageResult { kNoEffect = 0, kUnchanged = 1, kYellow = 2, kRed = 3, kDead = 4 };
	// The GetFireError codes.
	enum FireError { kFireOk = 0, kFireAmmo = 1, kFireRearm = 3, kFireIllegal = 5, kFireCant = 6, kFireRange = 8, kFireCloaked = 9 };

	CombatData& combat() const { return combat_; }
	// MapClass::GetTotalDamage (0x489180).
	int get_total_damage(int damage, int warhead, int armor, int distance) const;
	// MapClass::DamageArea (0x489280): the ground-object part (overlays, walls, bridges and rocking are not here).
	void damage_area(Coord at, int damage, ObjectId source, int warhead, int source_house);
	// ObjectClass / TechnoClass ReceiveDamage (0x5F5390, 0x701900). Returns a DamageResult.
	int receive_damage(Object& victim, int damage, int distance, int warhead, ObjectId source, int source_house, bool ignore_defenses = false);
	// The weapon in slot 0 (Primary) or 1 (Secondary); -1 if none.
	int weapon_of(const Object& o, int slot) const { return info(o).weapon[slot]; }
	int get_fire_error(const Object& o, const Target& target, int slot, bool check_range) const;
	bool in_range(const Object& o, const Target& target, int slot) const;
	int get_rof(Object& o, int slot);
	// TechnoClass::Fire (0x6FDD50). Returns the new bullet's id, or 0.
	ObjectId fire(Object& o, const Target& target, int slot);
	void update_bullet(Object& b);
	Coord target_coords(const Target& t) const;
	bool rearm_ready(const Object& o) const { return int(frame_) - o.rearm_start >= o.rearm_delay; }
	// FireError codes the class checks add.
	enum { kFireFacing = 2, kFireRotating = 4, kFireMoving = 7, kFireBusy = 10, kFireMustDeploy = 11 };
	// Veterancy (U section 12.1) and healing (12.2).
	bool is_veteran(const Object& o) const { return o.veterancy >= 1.0 && o.veterancy < 2.0; }
	bool is_elite(const Object& o) const { return o.veterancy >= 2.0; }
	bool has_ability(const Object& o, int ability) const;            // 0x70D0D0
	void register_destruction(Object& killer, const Object& victim); // 0x702D40
	bool should_self_heal(const Object& o) const;                    // 0x70BE80
	// ---- Tiberium and harvesting (P3 section 8.4, U section 9.7) ----
	// The ore on a cell: the Tiberium type (-1 none), the stage 0..11 and the value (Value * (stage + 1)).
	int ore_type(Cell c) const;
	int ore_stage(Cell c) const;
	int ore_value(Cell c) const;
	// CellClass::ReduceTiberium (0x480A80): takes up to n bales and returns how many it gave.
	int reduce_tiberium(Cell c, int n);
	// HouseClass::GiveTiberium (0x4F9610).
	void give_tiberium(int house, double amount, int type);
	double storage_total(const Object& o) const;
	// FootClass::ScanForTiberium (0x4DD0A0): the best ore cell within 'radius' cells, or {-1, -1}.
	Cell scan_for_tiberium(const Object& o, int radius) const;
	bool unit_harvesting(Object& o);                                   // UnitClass::Harvesting 0x73D450
	int harvest_mission(Object& o);                                    // UnitClass::Mission_Harvest 0x73E5E0
	ObjectId nearest_dock(const Object& o) const;                      // TryNearestDockBuilding
	// ---- Economy and production (P3 section 9; Phase 6) ----
	void give_money(int house, int amount) { houses_[size_t(house)].balance += amount; }
	void take_money(int house, int amount);                            // 0x4F9790
	int available(int house) const { return houses_[size_t(house)].balance; }
	static Category category_of(const TypeInfo& t, ra::rules::TypeKind kind);
	TypeRef type_ref(ra::rules::TypeKind kind, const char* name) const;
	const TypeInfo& type_info_of(TypeRef r) const;
	int actual_cost(int house, TypeRef r) const;                       // GetActualCost 0x711F00
	int refund(int house, TypeRef r, bool full) const;                 // GetRefund 0x711F60
	double power_percentage(int house) const;
	int power_output(int house) const;
	int power_drain(int house) const;
	int time_to_build(int house, TypeRef r) const;                     // 0x6F47A0
	int owned_count(int house, TypeRef r) const;
	int can_build(int house, TypeRef r, bool ignore_limits, bool include_queued) const; // 0x4F7870
	bool start_production(int house, TypeRef r);                       // enqueue / start (FactoryClass 0x4CA5A0)
	void suspend_production(int house, Category c);                    // 0x4C9E60
	void resume_production(int house, Category c);                     // 0x4C9EA0
	void abandon_production(int house, Category c);                    // 0x4C9FF0
	void update_factories();                                           // LogicStep::Factories
	void update_houses();                                              // LogicStep::Houses
	bool can_place(int house, TypeRef r, Cell origin, bool check_proximity) const; // CanPlaceHere + proximity
	// Places the finished building of a category. Returns the new object's id, or 0.
	ObjectId place_building(int house, Category c, Cell origin);
	// Puts a finished infantry, vehicle or aircraft into the world at the first free cell near its factory.
	ObjectId exit_unit(int house, Category c);
	// Selling (BuildingClass Mission_Selling, without the animation) and repair (0x450630).
	bool sell_building(Object& b);
	void toggle_repair(Object& b);
	void update_building(Object& b);
	const Factory& factory(int house, Category c) const { return houses_[size_t(house)].factories[size_t(c)]; }
	void set_target(Object& o, const Target& t);                      // 0x6FCDB0
	int select_weapon(const Object& o, const Target& t) const;        // 0x6F3330
	int weapon_range(const Object& o, int slot) const;                // 0x7012C0
	bool in_range_from(const Object& o, Coord from, const Target& t, int slot) const; // 0x6F7220
	Target approach_target(Object& o, bool skip_move);               // 0x4D5690 with the Unit / Infantry wrappers
	bool target_and_estimate_damage(Object& o, Coord from, int flags); // 0x709820
	Target greatest_threat(Object& o, int flags, Coord from);         // 0x6F8DF0 (local scans)
	double threat_value(const Object& o, const Object& target, const Coord* from) const; // 0x70CD10
	bool can_passive_acquire(const Object& o) const;                  // 0x709290
	// Scatter (Unit 0x743A50, Infantry 0x51D0D0). 'source' null: no source position.
	void scatter(Object& o, const Coord* source, bool forced, bool no_kidding);
	// An object takes its place in the world as a bullet in flight.
	ObjectId spawn_bullet(const BulletState& state, Coord at);

private:
	void explode_bullet(Object& b, bool direct_hit);
	void detonate(Object& b, Coord at);
	Coord random_coords_near(Coord c, int radius);
	double dist3d(Coord a, Coord b) const;
	double dist2d(Coord a, Coord b) const;
	bool in_air(const Object& o) const;
	void start_of_move(Object& o);
	void while_moving(Object& o);
	void sync_facing(Object& o) { o.body.raw = o.primary_facing.current(frame_); o.turret.raw = o.secondary_facing.current(frame_); }
	double speed_target(const Object& o, Cell head) const;
	bool begin_track(Object& o, int track, Cell head, int steps_used);
	void enter_cell(Object& o, Cell c);

	const ra::world::World* world_;
	ra::Random2* random_;
	std::vector<TypeInfo> types_;
	LandSpeeds land_speeds_;
	std::vector<House> houses_;
	std::deque<Object> objects_; // a deque, so references stay valid while objects (bullets) are added
	struct CellOccupancy {
		ObjectId vehicle = 0;               // a unit standing in or moving into the cell
		std::array<ObjectId, 5> spot {};    // infantry spots 0 (centre), 2, 3, 4 (1 is never used)
	};
	std::vector<CellOccupancy> occupancy_;
	std::vector<uint8_t> blocked_; // cells that buildings and trees block
	std::vector<ObjectId> building_at_; // the building covering each cell, 0 none
	std::vector<int16_t> overlay_;      // the map's overlays, changed by harvesting and growth
	std::vector<uint8_t> overlay_data_;
	mutable CombatData combat_;
	std::vector<int16_t> terrain_type_; // the terrain object (tree) type in each cell, -1 none
	ra::sim::path::Zones zones_;
	std::vector<uint8_t> near_feet_; // CellClass +0x122 BlockedNeighbours: foot units in the eight cells around
	void mark_neighbours(Object& o);  // FootClass::UpdatePosition's part that moves those counts
	ra::world::ShroudMap shroud_;
	int local_house_ = 0;
	uint32_t frame_ = 0;
};

} // namespace ra::sim
