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

namespace ra::sim {

using ObjectId = int32_t; // 0 = none; ids are handed out in creation order starting at 1

// AbstractType values (the WhatAmI of an object, used in the CRC).
enum class What : uint8_t { Unit = 1, Aircraft = 2, Building = 6, Infantry = 15 };

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

	Facing desired_facing {};      // where the body is turning to
	bool rotating = false;

	int path_delay = 0;            // frames until the next pathfinding attempt
	Cell current_cell {};          // CurrentMapCoords: the cell the unit is registered in
};

struct Object {
	ObjectId id = 0;
	What what = What::Unit;
	ra::rules::TypeKind kind = ra::rules::TypeKind::Vehicle;
	int type = -1;                 // index into the rules' types of this kind
	int type_info = -1;            // index into the simulation's TypeInfo list
	int house = 0;
	Coord location {};             // leptons
	Facing body {};
	Facing turret {};
	int health = 0;                // strength points
	int sub_cell = 0;              // infantry spot in the cell (0 centre, 2, 3, 4 others)
	Mission mission = Mission::Guard;
	bool alive = true;
	bool in_limbo = false;
	FootState foot {};

	Cell cell() const { return ra::cell_of(location); }
};

} // namespace ra::sim
