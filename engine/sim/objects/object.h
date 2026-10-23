#pragma once

// The objects of a running game: units, infantry, aircraft and buildings (U section 1).
//
// The original has a deep class tree (Object, Mission, Radio, Techno, Foot, Unit / Infantry / Aircraft,
// Building). The port keeps one plain record per object and puts the behaviour in the simulation,
// grouped by what the original's classes do. Objects live in one vector in creation order: that order
// is the original's logic list order and the order that sums into the frame CRC.

#include "core/coord.h"
#include "rules/rules_db.h"
#include "sim/loop/event.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>

namespace ra::sim {

using ObjectId = int32_t; // 0 = none; ids are handed out in creation order starting at 1

// AbstractType values (the WhatAmI of an object, used in the CRC).
enum class What : uint8_t { Unit = 1, Aircraft = 2, Building = 6, Bullet = 8, Infantry = 15 };

// MissionType (U section 2.1).
enum class Mission : uint8_t {
	Sleep = 0, Attack = 1, Move = 2, QMove = 3, Retreat = 4, Guard = 5, Sticky = 6, Enter = 7, Capture = 8,
	Eaten = 9, Harvest = 10, AreaGuard = 11, Return = 12, Stop = 13, Ambush = 14, Hunt = 15, Unload = 16,
	Sabotage = 17, Construction = 18, Selling = 19, Repair = 20, Rescue = 21, Missile = 22, Harmless = 23,
	Open = 24, Patrol = 25, ParadropApproach = 26, ParadropOverfly = 27, Wait = 28, AttackMove = 29,
	SpyplaneApproach = 30, SpyplaneOverfly = 31, None = 0xFF,
};

// A 16-bit binary angle: 0 is north, 0x4000 east.
struct Facing {
	uint16_t raw = 0;
	// The 8 compass directions (0 N, 1 NE, 2 E ... 7 NW), rounded to nearest.
	int dir8() const { return static_cast<int>(((uint32_t(raw) >> 12) + 1) >> 1) & 7; }
	// The facing byte the CRC uses.
	uint32_t crc_byte() const { return ((uint32_t(raw) >> 7) + 1) >> 1; }
};

// FacingClass (0x4C91C0...): a facing that turns toward a desired one at a fixed rate.
// The turn takes |desired - start| / ROT frames and the facing in between is interpolated linearly.
struct FacingControl {
	uint16_t desired = 0;
	uint16_t start = 0;
	int32_t timer_start = 0; // CDTimer: the frame it started
	int32_t time_left = 0;   // and its length
	int16_t rot = 0;         // binary-angle units per frame: min(ROT, 127) << 8

	void set_rot(int type_rot) { rot = static_cast<int16_t>(uint16_t(uint8_t(type_rot > 126 ? 127 : type_rot)) << 8); }
	int remaining(uint32_t frame) const
	{
		const int elapsed = static_cast<int>(frame) - timer_start;
		return elapsed >= time_left ? 0 : time_left - elapsed;
	}
	// The current facing (0x4C93D0).
	uint16_t current(uint32_t frame) const
	{
		if (rot < 1)
			return desired;
		const int rem = remaining(frame);
		if (rem == 0)
			return desired;
		const int16_t diff = static_cast<int16_t>(desired - start);
		const int16_t steps = static_cast<int16_t>(std::abs(int(diff)) / rot);
		if (steps < 1)
			return desired;
		return static_cast<uint16_t>(int16_t(desired) - int16_t(diff / steps) * int16_t(rem));
	}
	bool rotating(uint32_t frame) const { return rot > 0 && remaining(frame) > 0; }
	// Turns toward a new facing (0x4C9220). False if it was already the target.
	bool set_desired(uint16_t facing, uint32_t frame)
	{
		if (desired == facing)
			return false;
		start = current(frame);
		desired = facing;
		if (rot > 0) {
			timer_start = static_cast<int32_t>(frame);
			time_left = std::abs(int(int16_t(facing - start))) / rot;
		}
		return true;
	}
	// Jumps to a facing at once (0x4C9300). False if it was already there.
	bool set_current(uint16_t facing, uint32_t frame)
	{
		const bool changed = current(frame) != facing;
		if (changed)
			desired = start = facing;
		timer_start = static_cast<int32_t>(frame);
		time_left = 0;
		return changed;
	}
};

inline constexpr int kMaxPath = 24;

// The movement state of a unit or infantry (FootClass and the locomotor).
struct FootState {
	// The path: directions 0..7, 8 = tube jump, -1 ends it (Foot+0x5E0).
	std::array<int, kMaxPath> path {};
	int path_length = 0;
	bool has_path() const { return path[0] != -1; }

	Coord destination {};          // where the order says to go (a cell centre); valid when has_destination
	bool has_destination = false;
	Coord head_to {};              // the cell centre being driven to (Drive HeadToCoord)
	bool has_head_to = false;

	// Drive: tracks (U section 10.2).
	int track_number = -1;         // index into the turn track table, -1 = not on a track
	int track_index = 0;
	bool on_short_track = false;
	int speed_accum = 0;           // the remainder of the last frame's travel, 0..7
	double speed_percentage = 1.0; // SpeedPercentage (Foot+0x578)
	double target_speed = 1.0;

	bool rotating = false;         // Drive IsRotating: waiting for a turn in place to finish

	// Hover, Jumpjet and Fly (U sections 10.4 to 10.6).
	double loco_speed = 0.0;       // Hover / Fly: the speed fraction; Jumpjet: CurrentSpeed in leptons
	double loco_max = 0.0;         // Jumpjet MaxSpeed
	double hover_vz = 0.0;         // Hover vertical velocity (+0x60)
	double wobble = 0.0;           // Jumpjet wobble phase
	int jj_state = 0;              // 0 Grounded, 1 Ascending, 2 Hovering, 3 Cruising, 4 Descending, 5 Crashing
	bool landing = false;          // Fly: IsLanding

	int path_delay = 0;            // frames until the next pathfinding attempt
	Cell current_cell {};          // CurrentMapCoords: the cell the unit is registered in
};

// A projectile in flight (BulletClass, U section 7). Bullets are objects too, so they take their place in
// the logic list and use ids from the same counter.
struct BulletState {
	ObjectId owner = 0;            // the firing object (0 if it is gone)
	int owner_house = -1;
	Target target {};              // an object or a cell
	int weapon = -1;               // CombatData indices
	int projectile = -1;
	int warhead = -1;
	int damage = 0;
	int speed = 0;                 // leptons per frame
	double vx = 0, vy = 0, vz = 0; // velocity, leptons per frame
	Coord source {};
	Coord target_coords {};
	int arm_start = 0;             // frame the arming timer started
	int arm_delay = 0;
	bool course_lock = false;
	int course_lock_frames = 0;
	double near_sum = 0.0;         // the miss detector
	int near_frames = 0;
	double last_distance = 0.0;    // proximity check
	int age = 0;
};

struct Object {
	ObjectId id = 0;
	What what = What::Unit;
	ra::rules::TypeKind kind = ra::rules::TypeKind::Vehicle;
	int type = -1;                 // index into the rules' types of this kind
	int type_info = -1;            // index into the simulation's TypeInfo list
	int house = 0;
	Coord location {};             // leptons
	Facing body {};                // the body facing as of the last update (PrimaryFacing.current())
	Facing turret {};
	FacingControl primary_facing {};
	FacingControl secondary_facing {};
	int health = 0;                // strength points
	int sub_cell = 0;              // infantry spot in the cell (0 centre, 2, 3, 4 others)
	Mission mission = Mission::Guard;
	int mission_queue = -1;        // MissionQueue (+0xB4): the mission that starts next, -1 none
	int mission_status = 0;        // MissionStatus (+0xBC): the handler's own state machine
	int32_t mission_timer_start = 0; // CDTimer (+0xC8, +0xD0): the handler runs when it has run for the delay
	int32_t mission_timer_delay = 0;
	bool alive = true;
	bool in_limbo = false;
	FootState foot {};
	Cell neighbour_mark { -1, -1 }; // Foot +0x55C: the cell whose neighbours count this unit (BlockedNeighbours)

	// Combat (Techno).
	Target target {};              // TechnoClass +0x...: what it is attacking
	int32_t rearm_start = 0;       // RearmTimer (CDTimer): ready when frame - start >= delay
	int32_t rearm_delay = 0;
	int burst_index = 0;           // CurrentBurstIndex
	int ammo = -1;                 // -1 unlimited
	ObjectId last_attacker = 0;
	// Harvesting (U section 9.7).
	std::array<double, 4> storage {};   // bales per Tiberium type
	bool is_harvesting = false;
	int32_t load_start = -1, load_delay = 0;
	ObjectId dock_building = 0;
	bool repairing = false;        // the wrench is on (BuildingClass::IsRepairing)
	double veterancy = 0.0;        // VeterancyStruct: <1 rookie, 1..2 veteran, >=2 elite
	bool should_lose_target = false; // ShouldLoseTargetNow
	int32_t targeting_start = 0;   // TargetingTimer
	int32_t targeting_delay = 0;
	int estimated_health = 0;      // EstimatedHealth (damage already on its way)
	int32_t last_fire_frame = -1000; // LastFireBulletFrame
	bool turret_busy = false;      // UnitClass +0x6AF (the turret is turning)
	Coord archive {};              // ArchiveTarget: where Area Guard returns to
	bool has_archive = false;
	// Infantry animation (SequenceAnim and its StageClass): the shot comes on the fire frame.
	bool is_firing = false;
	int sequence = -1;
	int stage = 0;
	int32_t stage_start = 0;
	int stage_rate = 0;            // frames per stage; 0 = stopped
	BulletState bullet {};         // used when what == Bullet

	Cell cell() const { return ra::cell_of(location); }
};

} // namespace ra::sim
