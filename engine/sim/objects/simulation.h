#pragma once

// The running game's objects and what happens to them each frame (Phase 4).
//
// Simulation owns the objects, the cell occupancy and the local player's shroud, executes the events
// that give orders (MEGAMISSION: move, stop, guard), and updates movement every frame through the
// game loop's LogicObjects step.
//
// PROVISIONAL parts (marked in the code, to be replaced by the decoded originals in block 4B-fix):
//  - CanEnterCell (FootClass vslot 0x1AC): terrain speed, buildings, trees and cliffs only.
//  - FacingClass turning: a fixed turn rate of ROT << 8 per frame.
//  - Drive StartOfMove: straight tracks only; a turn of more than 45 degrees is a turn in place first.
//    The curved turn tracks need the decoded track entry rules.

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
	// FootClass::CanEnterCell for the unit (PROVISIONAL).
	ra::sim::path::Move can_enter(const Object& o, Cell to, int dir, Cell from) const;

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
	bool blocked_terrain(Cell c) const { return blocked_[cell_index(c)] != 0; }

	// Movement.
	void update_foot(Object& o);
	void drive_process(Object& o);
	void walk_process(Object& o);
	void start_of_move(Object& o);
	void while_moving(Object& o);
	bool turn_towards(Object& o, Facing desired);
	void enter_cell(Object& o, Cell c);

	const ra::world::World* world_;
	ra::Random2* random_;
	std::vector<TypeInfo> types_;
	LandSpeeds land_speeds_;
	std::vector<House> houses_;
	std::vector<Object> objects_;
	std::vector<std::array<ObjectId, 5>> occupancy_;
	std::vector<uint8_t> blocked_; // cells that buildings and trees block
	ra::world::ShroudMap shroud_;
	int local_house_ = 0;
	uint32_t frame_ = 0;
};

} // namespace ra::sim
