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
#include "sim/world/shroud.h"
#include "sim/world/world.h"

#include <string>
#include <vector>

namespace ra::sim {

struct House {
	std::string name;
	bool human = true; // human houses issue events; AI houses issue none
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
	const std::vector<Object>& objects() const { return objects_; }
	const TypeInfo& info(const Object& o) const { return types_[static_cast<size_t>(o.type_info)]; }
	// The object standing in a cell (ground level), checking the five infantry spots and the vehicle spot.
	ObjectId occupant(Cell cell, int spot = 0) const;
	// All live objects whose position is inside a lepton rectangle, in logic list order.
	std::vector<ObjectId> objects_in_rect(Coord min, Coord max) const;

	// ---- orders (what a MEGAMISSION event does, ObjectClickedAction / ClickedMission) ----
	void order_move(Object& o, Cell destination);
	void order_stop(Object& o);
	void order_guard(Object& o);
	// Builds the events for a player's orders (they go to GameLoop::issue).
	static Event move_event(int house, ObjectId unit, Cell destination);
	static Event stop_event(int house, ObjectId unit);
	static Event guard_event(int house, ObjectId unit);

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
	// FootClass::CanEnterCell (vslot 0x1AC) for a unit or infantry moving into 'to' in direction 'dir'.
	ra::sim::path::Move can_enter(const Object& o, Cell to, int dir, Cell from) const;
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
	void update_foot(Object& o);
	void drive_process(Object& o);
	void walk_process(Object& o);
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
	std::vector<Object> objects_;
	struct CellOccupancy {
		ObjectId vehicle = 0;               // a unit standing in or moving into the cell
		std::array<ObjectId, 5> spot {};    // infantry spots 0 (centre), 2, 3, 4 (1 is never used)
	};
	std::vector<CellOccupancy> occupancy_;
	std::vector<uint8_t> blocked_; // cells that buildings and trees block
	ra::world::ShroudMap shroud_;
	int local_house_ = 0;
	uint32_t frame_ = 0;
};

} // namespace ra::sim
