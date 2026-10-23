#include "sim/loop/game_loop.h"
#include "sim/loop/recording.h"
#include "sim/objects/drive_tracks.h"
#include "sim/objects/selection.h"
#include "sim/objects/simulation.h"
#include "sim/world/shroud.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

using namespace ra;
using namespace ra::sim;

namespace {

const char* kRules = R"(
[General]
PathDelay=0.016
LeptonsPerSightIncrease=2000
RevealByHeight=yes
[Move]
Rate=.016
[Guard]
Rate=.030
[Countries]
1=Americans
2=Russians
[InfantryTypes]
1=E1
[VehicleTypes]
1=HTNK
2=AMCV
3=HOVR
4=JUMP
5=CRSH
6=HARV
[AircraftTypes]
1=ORCA
[BuildingTypes]
1=GAPOWR
2=GAWEAP
3=GACNST
4=NAPOWR
5=GAREF
[Tiberiums]
1=Riparius
[Riparius]
Value=25
[HARV]
Strength=600
Cost=1400
Speed=4
ROT=4
Sight=4
Harvester=yes
Storage=3
Dock=GAREF
SpeedType=Track
Locomotor={4A582741-9839-11d1-B709-00A024DDAFD1}
[GAREF]
Strength=900
Cost=2000
Refinery=yes
Factory=
[JumpjetControls]
Speed=14
Climb=5
Crash=5.0
Height=500
Accel=2
[E1]
Strength=125
Speed=4
Sight=5
Crushable=yes
Primary=Rifle
FireUp=2
[HOVR]
Strength=200
Speed=8
ROT=8
Sight=6
SpeedType=Hover
MovementZone=Amphibious
Locomotor={4A582742-9839-11d1-B709-00A024DDAFD1}
[JUMP]
Strength=100
Speed=6
ROT=8
Sight=6
SpeedType=Foot
Locomotor={92612C46-F71F-11d3-86C1-0000F8757D4C}
[ORCA]
Strength=200
Speed=12
ROT=8
Sight=8
SpeedType=Winged
SlowdownDistance=500
Locomotor={4A582746-9839-11d1-B709-00A024DDAFD1}
[CRSH]
Strength=300
Speed=6
ROT=5
Sight=4
SpeedType=Track
Crusher=yes
Locomotor={4A582741-9839-11d1-B709-00A024DDAFD1}
[HTNK]
Strength=400
Cost=800
Prerequisite=GAWEAP
TechLevel=2
Armor=heavy
Primary=Cannon
Speed=6
ROT=5
Sight=8
SpeedType=Track
Locomotor={4A582741-9839-11d1-B709-00A024DDAFD1}
[AMCV]
Strength=600
Speed=4
ROT=4
Sight=6
SpeedType=Wheel
Locomotor={4A582741-9839-11d1-B709-00A024DDAFD1}
[Americans]
CostUnitsMult=0.5
[GACNST]
Strength=1000
Cost=3000
Factory=BuildingType
Adjacent=3
[GAPOWR]
Strength=750
Armor=concrete
Cost=800
Power=200
Prerequisite=GACNST
TechLevel=1
[NAPOWR]
Strength=750
Cost=600
Owner=Russians
TechLevel=1
[GAWEAP]
Strength=1000
Cost=2000
Power=-50
Factory=UnitType
Prerequisite=GAPOWR
TechLevel=1
[Cannon]
Damage=100
ROF=50
Range=6
Projectile=ShellInstant
Warhead=APWarhead
Speed=100
[ShellInstant]
Inviso=yes
[Rifle]
Damage=15
ROF=20
Range=4
Projectile=ShellInstant
Warhead=APWarhead
[Lobbed]
Arcing=yes
Inaccurate=no
[LobCannon]
Damage=40
ROF=60
Range=10
Projectile=Lobbed
Warhead=APWarhead
[APWarhead]
Verses=100%,100%,100%,100%,90%,50%,100%,100%,100%,100%,100%
CellSpread=1.0
PercentAtMax=0.5
MinDebris=0
MaxDebris=1
[Clear]
Buildable=yes
Winged=100%
Foot=100%
Track=100%
Wheel=100%
Float=0%
Hover=50%
Amphibious=80%
FloatBeach=0%
[Water]
Winged=100%
Foot=0%
Track=0%
Wheel=0%
Float=100%
Hover=100%
Amphibious=100%
FloatBeach=100%
)";
const char* kArt = R"(
[E1]
Sequence=InfSeq
[InfSeq]
Ready=0,1,1
FireUp=8,6,6
[GAPOWR]
Foundation=2x2
[GAREF]
Foundation=3x3
[GAWEAP]
Foundation=3x3
[GACNST]
Foundation=3x3
[NAPOWR]
Foundation=2x2
)";

// A flat 60 x 60 map: cells 10..69 exist, all clear land at level 0.
struct TestWorld {
	ra::world::World world;
	ra::Random2 rng { 4242 };
	std::unique_ptr<Simulation> sim;
	int americans = 0, russians = 0;

	TestWorld()
	{
		ra::rules::RulesSources sources;
		sources.rules = kRules;
		sources.art = kArt;
		world.rules.load_from_text(ra::rules::GameKind::YurisRevenge, sources);
		world.grid.set_size(30, 60, 0, 0, 30, 60);
		for (int y = 10; y < 70; ++y)
			for (int x = 10; x < 70; ++x) {
				ra::world::MapCell* c = world.grid.mutable_at(x, y);
				c->exists = true;
				c->land = ra::world::LandType::Clear;
				c->level = 0;
			}
		sim = std::make_unique<Simulation>(world, rng);
		americans = sim->house_index("Americans");
		russians = sim->house_index("Russians");
	}

	int type(ra::rules::TypeKind kind, const char* name) { return world.rules.type_index(kind, name); }
	ObjectId tank(int house, int x, int y, int facing_byte = 0)
	{
		return sim->spawn(ra::rules::TypeKind::Vehicle, type(ra::rules::TypeKind::Vehicle, "HTNK"), house, { int16_t(x), int16_t(y) }, Facing { uint16_t(facing_byte << 8) }, 256);
	}
	ObjectId unit(const char* name, int house, int x, int y, int facing_byte = 0)
	{
		return sim->spawn(ra::rules::TypeKind::Vehicle, type(ra::rules::TypeKind::Vehicle, name), house, { int16_t(x), int16_t(y) }, Facing { uint16_t(facing_byte << 8) }, 256);
	}
	ObjectId gi(int house, int x, int y, int spot = 0)
	{
		return sim->spawn(ra::rules::TypeKind::Infantry, type(ra::rules::TypeKind::Infantry, "E1"), house, { int16_t(x), int16_t(y) }, Facing {}, 256, spot);
	}
	void wall_cell(int x, int y) { sim->spawn(ra::rules::TypeKind::Building, type(ra::rules::TypeKind::Building, "GAPOWR"), 0, { int16_t(x), int16_t(y) }, Facing {}, 256); }
};

// Runs frames until a condition holds or the limit is reached; returns the frames run.
template <typename Cond>
int run_until(GameLoop& loop, int limit, Cond cond)
{
	int n = 0;
	while (n < limit && !cond()) {
		EXPECT_TRUE(loop.run_frame());
		++n;
	}
	return n;
}

} // namespace

TEST(Types, ReadFromTheRulesWithDefaults)
{
	TestWorld w;
	const ObjectId id = w.tank(w.americans, 20, 20);
	const Object& o = *w.sim->find(id);
	const TypeInfo& t = w.sim->info(o);
	EXPECT_EQ(t.name, "HTNK");
	EXPECT_EQ(t.speed, 6);
	EXPECT_EQ(t.rot, 5);
	EXPECT_EQ(t.speed_type, SpeedType::Track);
	EXPECT_EQ(t.locomotor, LocomotorKind::Drive);
	EXPECT_DOUBLE_EQ(t.acceleration_factor, 0.03);
	EXPECT_EQ(t.slowdown_distance, 500);
	EXPECT_EQ(o.health, 400);
	const Object& g = *w.sim->find(w.gi(w.americans, 22, 22));
	EXPECT_EQ(w.sim->info(g).locomotor, LocomotorKind::Walk);
	EXPECT_EQ(w.sim->info(g).speed_type, SpeedType::Foot);
}

TEST(DriveTracks, StraightTracksArriveAtTheCellCentreFromTheRightSide)
{
	using namespace ra::sim::drive_tracks;
	// Straight ahead for each of the eight facings: TurnTrack[facing * 9]. The track's first point
	// is on the side the unit comes from, and the last one is the cell centre.
	const int from_x[8] = { 0, -248, -245, -248, 0, 248, 245, 248 };
	const int from_y[8] = { 245, 248, 0, -248, -245, -248, 0, 248 };
	for (int dir = 0; dir < 8; ++dir) {
		const TurnTrack& t = turn_track(dir * 9);
		ASSERT_NE(t.track, 0) << dir;
		const RawTrack& raw = raw_track(t.track);
		const Point first = transform(raw.points[0], t.flag);
		const Point last = transform(raw.points[raw.count - 1], t.flag);
		EXPECT_EQ(first.x, from_x[dir]) << dir;
		EXPECT_EQ(first.y, from_y[dir]) << dir;
		EXPECT_EQ(last.x, 0);
		EXPECT_EQ(last.y, 0);
		EXPECT_EQ(t.facing, dir * 32);
	}
}

TEST(Orders, MoveEventDrivesAUnitToTheCellCentre)
{
	TestWorld w;
	const ObjectId tank = w.tank(w.americans, 30, 30, 0);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::move_event(w.americans, tank, { 30, 25 }));
	const Object& o = *w.sim->find(tank);
	int last_y = o.location.y;
	const int frames = run_until(loop, 600, [&] {
		EXPECT_LE(o.location.y, last_y); // always northward
		last_y = o.location.y;
		return o.cell() == Cell { 30, 25 } && !o.foot.has_destination;
	});
	EXPECT_LT(frames, 600);
	EXPECT_EQ(o.location.x, 30 * 256 + 128);
	EXPECT_EQ(o.location.y, 25 * 256 + 128);
	// The tank stops at once; the Move handler sends it to Guard when it next runs (every 14 to 16 frames).
	EXPECT_EQ(o.mission, Mission::Move);
	run_until(loop, 40, [&] { return o.mission == Mission::Guard; });
	EXPECT_EQ(o.mission, Mission::Guard);
	// A straight track is 24 points of 7 speed units; at 6 per frame that is about 28 frames a cell.
	// Within SlowdownDistance (500 leptons) of the goal the tank brakes down to 30% speed, so the last
	// two cells take much longer: five cells take roughly 200 to 350 frames.
	EXPECT_GT(frames, 150);
	EXPECT_LT(frames, 400);
	// The cell the tank left is free, the one it reached is taken.
	EXPECT_EQ(w.sim->occupant({ 30, 30 }), 0);
	EXPECT_EQ(w.sim->occupant({ 30, 25 }), tank);
}

TEST(Orders, TheBodyTurnsInPlaceBeforeDrivingOff)
{
	TestWorld w;
	const ObjectId tank = w.tank(w.americans, 30, 30, 128); // facing south
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::move_event(w.americans, tank, { 30, 28 })); // north
	const Object& o = *w.sim->find(tank);
	bool moved_while_turning = false;
	int turning_frames = 0;
	while (o.body.dir8() != 0 && turning_frames < 200) {
		const Coord before = o.location;
		ASSERT_TRUE(loop.run_frame());
		moved_while_turning |= before != o.location;
		++turning_frames;
	}
	EXPECT_FALSE(moved_while_turning);
	EXPECT_GT(turning_frames, 5);
	run_until(loop, 400, [&] { return o.cell() == Cell { 30, 28 } && !o.foot.has_destination; });
	EXPECT_EQ(o.cell(), (Cell { 30, 28 }));
}

TEST(Orders, DiagonalMovesUseTheDiagonalTrack)
{
	TestWorld w;
	const ObjectId tank = w.tank(w.americans, 30, 30, 32); // north-east
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::move_event(w.americans, tank, { 33, 27 }));
	const Object& o = *w.sim->find(tank);
	run_until(loop, 900, [&] { return o.cell() == Cell { 33, 27 } && !o.foot.has_destination && o.foot.track_number == -1; });
	EXPECT_EQ(o.cell(), (Cell { 33, 27 }));
}

TEST(Orders, GoesAroundBuildingsAndNeverEntersThem)
{
	TestWorld w;
	for (int y = 24; y <= 36; y += 2) // a column of 2x2 buildings with no gaps, x = 30..31, y = 24..37
		w.wall_cell(30, y);
	const ObjectId tank = w.tank(w.americans, 27, 30, 64); // east
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::move_event(w.americans, tank, { 34, 30 }));
	const Object& o = *w.sim->find(tank);
	bool entered_wall = false;
	run_until(loop, 3000, [&] {
		const Cell c = o.cell();
		if (c.x == 30 || c.x == 31)
			if (c.y >= 24 && c.y <= 37)
				entered_wall = true;
		return o.cell() == Cell { 34, 30 } && !o.foot.has_destination && o.foot.track_number == -1;
	});
	EXPECT_FALSE(entered_wall);
	EXPECT_EQ(o.cell(), (Cell { 34, 30 }));
}

TEST(Orders, AnUnreachableGoalLeavesTheUnitWhereItIs)
{
	TestWorld w;
	// Surround the goal (40,40) with water, which a tank can't cross.
	for (int y = 38; y <= 42; ++y)
		for (int x = 38; x <= 42; ++x)
			if (x != 40 || y != 40)
				w.world.grid.mutable_at(x, y)->land = ra::world::LandType::Water;
	const ObjectId tank = w.tank(w.americans, 30, 30, 0);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::move_event(w.americans, tank, { 40, 40 }));
	for (int i = 0; i < 400; ++i)
		ASSERT_TRUE(loop.run_frame());
	const Object& o = *w.sim->find(tank);
	EXPECT_NE(o.cell(), (Cell { 40, 40 }));
	EXPECT_EQ(o.foot.track_number == -1 || o.location.y > 0, true);
}

TEST(Orders, InfantryWalkToTheirCell)
{
	TestWorld w;
	const ObjectId id = w.gi(w.americans, 30, 30, 0);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::move_event(w.americans, id, { 34, 30 }));
	const Object& o = *w.sim->find(id);
	const int frames = run_until(loop, 600, [&] { return o.cell() == Cell { 34, 30 } && !o.foot.has_destination; });
	EXPECT_LT(frames, 600);
	// Speed 4 leptons per frame: four cells (1024 leptons) take about 256 frames.
	EXPECT_GT(frames, 200);
	EXPECT_LT(frames, 320);
}

TEST(Orders, OnlyTheOwnersOrdersCountAndStopAndGuardStopAUnit)
{
	TestWorld w;
	const ObjectId tank = w.tank(w.americans, 30, 30, 0);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::move_event(w.russians, tank, { 30, 20 })); // not the owner: ignored
	for (int i = 0; i < 20; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_EQ(w.sim->find(tank)->location.y, 30 * 256 + 128);
	loop.issue(Simulation::move_event(w.americans, tank, { 30, 20 }));
	for (int i = 0; i < 30; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_NE(w.sim->find(tank)->location.y, 30 * 256 + 128);
	loop.issue(Simulation::stop_event(w.americans, tank));
	run_until(loop, 100, [&] { return w.sim->find(tank)->foot.track_number == -1; });
	run_until(loop, 40, [&] { return w.sim->find(tank)->mission == Mission::Guard; });
	EXPECT_EQ(w.sim->find(tank)->mission, Mission::Guard);
	const Cell stopped = w.sim->find(tank)->cell();
	for (int i = 0; i < 100; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_EQ(w.sim->find(tank)->cell(), stopped); // it stays on the cell it finished
	EXPECT_EQ(w.sim->find(tank)->location.x, stopped.x * 256 + 128);
}

TEST(Selection, ClickBoxAndOrders)
{
	TestWorld w;
	const ObjectId a = w.tank(w.americans, 30, 30);
	const ObjectId b = w.tank(w.americans, 32, 30);
	const ObjectId enemy = w.tank(w.russians, 31, 33);
	const ObjectId g = w.gi(w.americans, 31, 31);
	w.wall_cell(40, 40);
	Selection sel;
	sel.click(*w.sim, w.americans, cell_center({ 30, 30 }));
	EXPECT_EQ(sel.ids(), std::vector<ObjectId> { a });
	sel.click(*w.sim, w.americans, cell_center({ 32, 30 }), 192, true); // shift: add
	EXPECT_EQ(sel.ids(), (std::vector<ObjectId> { a, b }));
	sel.click(*w.sim, w.americans, cell_center({ 32, 30 }), 192, true); // shift again: toggle off
	EXPECT_EQ(sel.ids(), std::vector<ObjectId> { a });
	sel.click(*w.sim, w.americans, cell_center({ 50, 50 })); // empty ground clears
	EXPECT_TRUE(sel.empty());
	sel.click(*w.sim, w.americans, cell_center({ 31, 33 })); // an enemy can't be selected
	EXPECT_TRUE(sel.empty());
	sel.box(*w.sim, w.americans, cell_center({ 29, 29 }), cell_center({ 33, 34 }));
	EXPECT_EQ(sel.ids(), (std::vector<ObjectId> { a, b, g }));
	(void)enemy;
	sel.box(*w.sim, w.americans, cell_center({ 39, 39 }), cell_center({ 42, 42 })); // a building isn't box-selected
	EXPECT_TRUE(sel.empty());

	sel.box(*w.sim, w.americans, cell_center({ 29, 29 }), cell_center({ 33, 32 }));
	const auto orders = sel.move_orders(*w.sim, w.americans, { 35, 35 });
	ASSERT_EQ(orders.size(), 3u);
	for (const Event& e : orders) {
		EXPECT_EQ(e.type, EventType::MegaMission);
		const MegaMission m = MegaMission::from_event(e);
		EXPECT_EQ(m.mission, static_cast<uint8_t>(Mission::Move));
		EXPECT_EQ(m.destination, Target::cell(35, 35));
	}
}

TEST(Sight, MovingUnitsOfTheLocalPlayerRevealTheMap)
{
	TestWorld w;
	w.sim->set_local_house(w.americans);
	const ObjectId tank = w.tank(w.americans, 30, 30, 0);
	const ObjectId enemy = w.tank(w.russians, 50, 50, 0);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	EXPECT_FALSE(w.sim->shroud().revealed(30, 30));
	w.sim->reveal_around(*w.sim->find(tank)); // what spawning an own object does
	EXPECT_TRUE(w.sim->shroud().revealed(30, 30));
	EXPECT_TRUE(w.sim->shroud().revealed(30, 23)); // Sight=8
	EXPECT_FALSE(w.sim->shroud().revealed(30, 15));
	loop.issue(Simulation::move_event(w.americans, tank, { 30, 20 }));
	run_until(loop, 600, [&] { return w.sim->find(tank)->cell() == Cell { 30, 20 }; });
	EXPECT_TRUE(w.sim->shroud().revealed(30, 15));
	EXPECT_FALSE(w.sim->shroud().revealed(50, 50)); // the enemy's sight reveals nothing
	loop.issue(Simulation::move_event(w.russians, enemy, { 50, 45 }));
	for (int i = 0; i < 100; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_FALSE(w.sim->shroud().revealed(50, 45));
}

TEST(Simulation, ReplaysOfTheSameRecordingGiveTheSameFrameCrcs)
{
	auto run = [](const std::vector<uint8_t>* recording, std::vector<uint8_t>* record_out, CrcLog& log) {
		TestWorld w;
		const ObjectId t1 = w.tank(w.americans, 30, 30, 0);
		const ObjectId t2 = w.tank(w.americans, 34, 30, 128);
		const ObjectId i1 = w.gi(w.americans, 31, 34);
		GameLoop loop(*w.sim, w.rng);
		w.sim->attach(loop);
		RecordingWriter writer(RecordingHeader {});
		RecordingReader reader;
		if (recording) {
			EXPECT_TRUE(reader.open(*recording));
			loop.start_playback(&reader);
			loop.crc_log = &log;
			while (loop.run_frame()) {
			}
		} else {
			loop.start_recording(&writer);
			for (int f = 0; f < 150; ++f) {
				if (f == 3)
					loop.issue(Simulation::move_event(w.americans, t1, { 30, 24 }));
				if (f == 10)
					loop.issue(Simulation::move_event(w.americans, t2, { 28, 33 }));
				if (f == 20)
					loop.issue(Simulation::move_event(w.americans, i1, { 36, 36 }));
				if (f == 60)
					loop.issue(Simulation::stop_event(w.americans, t2));
				EXPECT_TRUE(loop.run_frame());
			}
			*record_out = writer.bytes();
		}
		return w.sim->find(t1)->location;
	};
	std::vector<uint8_t> recording;
	CrcLog unused;
	const Coord live = run(nullptr, &recording, unused);
	CrcLog a, b;
	const Coord p1 = run(&recording, nullptr, a);
	const Coord p2 = run(&recording, nullptr, b);
	EXPECT_EQ(p1, p2);
	EXPECT_EQ(CrcLog::first_difference(a, b), std::nullopt);
	EXPECT_GE(a.entries.size(), 150u);
	EXPECT_EQ(p1, live); // playback reproduces the movement of the recorded game
}

// The same recorded orders must give the same frame CRCs on every platform (desktop, Android x86_64
// and arm64): the CRCs of a fixed game are folded into one number, checked against the desktop's.
TEST(Simulation, AFixedOrderSequenceGivesTheSameCrcsOnEveryPlatform)
{
	auto play = [](const std::vector<uint8_t>* recording, std::vector<uint8_t>* record_out) {
		TestWorld w;
		for (int y = 26; y <= 36; y += 2) // a wall of buildings to path around
			w.wall_cell(36, y);
		std::vector<ObjectId> tanks, gis;
		for (int i = 0; i < 6; ++i)
			tanks.push_back(w.tank(w.americans, 28 + (i % 3) * 2, 28 + (i / 3) * 3, i * 40));
		for (int i = 0; i < 4; ++i)
			gis.push_back(w.gi(w.americans, 30 + i, 36));
		GameLoop loop(*w.sim, w.rng);
		w.sim->attach(loop);
		RecordingWriter writer(RecordingHeader {});
		RecordingReader reader;
		CrcLog log;
		loop.crc_log = &log;
		if (recording) {
			EXPECT_TRUE(reader.open(*recording));
			loop.start_playback(&reader);
			while (loop.run_frame()) {
			}
		} else {
			loop.start_recording(&writer);
			for (int f = 0; f < 400; ++f) {
				if (f == 2)
					for (size_t i = 0; i < tanks.size(); ++i)
						loop.issue(Simulation::move_event(w.americans, tanks[i], { int16_t(42 + int(i % 2)), int16_t(30 + int(i)) }));
				if (f == 5)
					for (ObjectId g : gis)
						loop.issue(Simulation::move_event(w.americans, g, { 40, 40 }));
				if (f == 120)
					loop.issue(Simulation::stop_event(w.americans, tanks[2]));
				if (f == 200)
					loop.issue(Simulation::move_event(w.americans, tanks[0], { 26, 26 }));
				EXPECT_TRUE(loop.run_frame());
			}
			*record_out = writer.bytes();
		}
		uint32_t folded = 0;
		for (const auto& [frame, crc] : log.entries)
			folded = folded * 31u + crc;
		return std::make_pair(folded, log.entries.size());
	};
	std::vector<uint8_t> recording;
	play(nullptr, &recording);
	const auto a = play(&recording, nullptr);
	const auto b = play(&recording, nullptr);
	EXPECT_EQ(a, b);
	EXPECT_GE(a.second, 400u);
	std::printf("folded frame CRCs: %08X over %zu frames\n", a.first, a.second);
	EXPECT_EQ(a.first, 0x4A6558B0u); // the desktop build's value
}

TEST(Hover, FloatsUpToHoverHeightAndCrossesWaterAtItsOwnSpeed)
{
	TestWorld w;
	// A strip of water the tank can't cross but a hovercraft can.
	for (int y = 10; y < 70; ++y)
		w.world.grid.mutable_at(36, y)->land = ra::world::LandType::Water;
	w.sim.reset(new Simulation(w.world, w.rng));
	w.americans = w.sim->house_index("Americans");
	const ObjectId h = w.unit("HOVR", w.americans, 33, 30, 64);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	const Object& o = *w.sim->find(h);
	for (int i = 0; i < 200; ++i)
		ASSERT_TRUE(loop.run_frame());
	// The spring settles around HoverHeight (120) above the ground (the bob adds a couple of leptons).
	EXPECT_NEAR(o.location.z, 120, 12);
	loop.issue(Simulation::move_event(w.americans, h, { 40, 30 }));
	int frames = 0;
	double max_speed = 0.0;
	while (frames < 900 && !(o.cell() == Cell { 40, 30 } && !o.foot.has_destination && !o.foot.has_head_to)) {
		ASSERT_TRUE(loop.run_frame());
		max_speed = std::max(max_speed, o.foot.loco_speed);
		++frames;
	}
	EXPECT_EQ(o.cell(), (Cell { 40, 30 }));
	EXPECT_LT(frames, 900);
	EXPECT_GT(max_speed, 0.9); // reached cruising speed
	EXPECT_LT(max_speed, 1.31);
}

TEST(Jumpjet, ClimbsCruisesAndLandsOnTheDestination)
{
	TestWorld w;
	const ObjectId j = w.unit("JUMP", w.americans, 30, 30);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	const Object& o = *w.sim->find(j);
	EXPECT_EQ(w.sim->info(o).locomotor, LocomotorKind::Jumpjet);
	EXPECT_EQ(w.sim->info(o).jj_height, 500);
	loop.issue(Simulation::move_event(w.americans, j, { 38, 30 }));
	int max_z = 0;
	bool cruised = false;
	int frames = 0;
	// It takes off, cruises at full height, then comes down on the destination.
	while (frames < 600) {
		ASSERT_TRUE(loop.run_frame());
		max_z = std::max(max_z, o.location.z);
		cruised |= o.foot.jj_state == 3;
		++frames;
		if (frames > 20 && o.foot.jj_state == 0)
			break;
	}
	EXPECT_TRUE(cruised);
	EXPECT_EQ(max_z, 500);
	EXPECT_EQ(o.foot.jj_state, 0);
	EXPECT_EQ(o.cell(), (Cell { 38, 30 }));
	EXPECT_EQ(o.location.z, 0);
}

TEST(Fly, AircraftClimbToTheFlightLevelAndSlowDownOnArrival)
{
	TestWorld w;
	const ObjectId a = w.sim->spawn(ra::rules::TypeKind::Aircraft, w.type(ra::rules::TypeKind::Aircraft, "ORCA"), w.americans, { 30, 30 }, Facing {}, 200);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	const Object& o = *w.sim->find(a);
	EXPECT_EQ(w.sim->info(o).locomotor, LocomotorKind::Fly);
	loop.issue(Simulation::move_event(w.americans, a, { 44, 30 }));
	ASSERT_TRUE(loop.run_frame());
	int frames = 0;
	double fastest = 0.0;
	while (frames < 900 && o.foot.has_destination) {
		ASSERT_TRUE(loop.run_frame());
		fastest = std::max(fastest, o.foot.loco_speed);
		++frames;
	}
	EXPECT_FALSE(o.foot.has_destination);
	EXPECT_EQ(o.location.x, 44 * 256 + 128);
	EXPECT_EQ(o.location.y, 30 * 256 + 128);
	EXPECT_DOUBLE_EQ(fastest, 1.0); // full speed far from the target
	for (int i = 0; i < 40; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_EQ(o.location.z, 500); // holds the flight level (Rules FlightLevel default 500)
}

TEST(Crushing, TanksFlattenEnemyInfantryButNotFriendsOrTankTypesWithoutTheFlag)
{
	TestWorld w;
	const ObjectId crusher = w.unit("CRSH", w.americans, 30, 30, 64); // east
	const ObjectId enemy = w.gi(w.russians, 33, 30);
	const ObjectId friend_gi = w.gi(w.americans, 31, 31);
	const ObjectId plain = w.tank(w.americans, 30, 40, 64);
	const ObjectId enemy2 = w.gi(w.russians, 33, 40);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::move_event(w.americans, crusher, { 36, 30 }));
	loop.issue(Simulation::move_event(w.americans, plain, { 36, 40 }));
	for (int i = 0; i < 300; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_EQ(w.sim->find(crusher)->cell(), (Cell { 36, 30 }));
	EXPECT_FALSE(w.sim->find(enemy)->alive);
	EXPECT_TRUE(w.sim->find(friend_gi)->alive);
	// A tank without Crusher=yes doesn't flatten infantry in its way: it stops short or goes round.
	EXPECT_TRUE(w.sim->find(enemy2)->alive);
	EXPECT_TRUE(w.sim->find(plain)->alive);
}

TEST(Combat, TotalDamageFollowsVersesSpreadAndMaxDamage)
{
	TestWorld w;
	const int wh = w.sim->combat().warhead("APWarhead");
	ASSERT_GE(wh, 0);
	// 100 damage, PercentAtMax 0.5, spread 256 leptons: at distance 0 full, at 128 three quarters, at 256 the minimum.
	EXPECT_EQ(w.sim->get_total_damage(100, wh, ArmorNone, 0), 100);
	EXPECT_EQ(w.sim->get_total_damage(100, wh, ArmorNone, 128), 75);
	EXPECT_EQ(w.sim->get_total_damage(100, wh, ArmorNone, 256), 50);
	// Verses: heavy armor takes half, medium 90 percent.
	EXPECT_EQ(w.sim->get_total_damage(100, wh, ArmorHeavy, 0), 50);
	EXPECT_EQ(w.sim->get_total_damage(100, wh, ArmorMedium, 0), 89); // "90%" is 90 * 0.01 truncated to 0.8999.. in the original's arithmetic
	EXPECT_EQ(w.sim->get_total_damage(100000, wh, ArmorNone, 0), 1000); // Rules MaxDamage
	EXPECT_EQ(w.sim->get_total_damage(-50, wh, ArmorNone, 4), -50);     // healing only within 8 leptons
	EXPECT_EQ(w.sim->get_total_damage(-50, wh, ArmorNone, 9), 0);
	EXPECT_EQ(w.sim->get_total_damage(0, wh, ArmorNone, 0), 0);
}

TEST(Combat, ADamageStateCrossingGivesYellowAndRedThenDeath)
{
	TestWorld w;
	const ObjectId t = w.tank(w.americans, 30, 30); // 400 HP, ConditionRed default 0.5 -> red at 200, yellow at 200
	Object& o = *w.sim->find(t);
	const int wh = w.sim->combat().warhead("APWarhead");
	// heavy armor halves the 100: 50 damage per hit.
	EXPECT_EQ(w.sim->receive_damage(o, 100, 0, wh, 0, -1), Simulation::kUnchanged);
	EXPECT_EQ(o.health, 350);
	EXPECT_EQ(w.sim->receive_damage(o, 100, 0, wh, 0, -1), Simulation::kUnchanged);
	EXPECT_EQ(o.health, 300);
	for (int i = 0; i < 3; ++i)
		w.sim->receive_damage(o, 100, 0, wh, 0, -1);
	EXPECT_EQ(o.health, 150);
	EXPECT_TRUE(o.alive);
	// An overkill hit clamps and kills.
	EXPECT_EQ(w.sim->receive_damage(o, 1000, 0, wh, 0, -1), Simulation::kDead);
	EXPECT_FALSE(o.alive);
	EXPECT_EQ(o.health, 0);
}

TEST(Combat, AFiredShotDrawsItsRofAndHurtsTheTarget)
{
	TestWorld w;
	const ObjectId a = w.tank(w.americans, 30, 30, 64); // facing east, at the target
	const ObjectId b = w.tank(w.russians, 33, 30);
	Object& shooter = *w.sim->find(a);
	const Target target = Target::object(b);
	EXPECT_EQ(w.sim->get_fire_error(shooter, target, 0, true), Simulation::kFireOk);
	// Facing away, a weapon without OmniFire can't fire: FACING (the unit's fire step then turns).
	const ObjectId c = w.tank(w.americans, 30, 32, 192);
	EXPECT_EQ(w.sim->get_fire_error(*w.sim->find(c), target, 0, true), Simulation::kFireFacing);
	ra::Random2 copy = w.rng;
	EXPECT_NE(w.sim->fire(shooter, target, 0), 0);
	// ROF 50 plus Random(0, 2), drawn once; the burst index wraps back to 0.
	EXPECT_EQ(shooter.rearm_delay, 50 + copy.ranged(0, 2));
	EXPECT_EQ(shooter.burst_index, 0);
	EXPECT_EQ(w.sim->get_fire_error(shooter, target, 0, true), Simulation::kFireRearm);
	// The instant-hit bullet detonates on its first update: 100 damage, halved by heavy armor.
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	ASSERT_TRUE(loop.run_frame());
	EXPECT_EQ(w.sim->find(b)->health, 350);
	// Out of range fails the check: 6 cells = 1536 leptons.
	const ObjectId far = w.tank(w.russians, 50, 30);
	Object& s2 = *w.sim->find(a);
	s2.rearm_delay = 0;
	EXPECT_EQ(w.sim->get_fire_error(s2, Target::object(far), 0, true), Simulation::kFireRange);
}

TEST(Combat, SplashHurtsNeighboursByDistanceButNotTheShooter)
{
	TestWorld w;
	const ObjectId a = w.tank(w.americans, 30, 30);
	const ObjectId v = w.tank(w.russians, 34, 30);
	const ObjectId near_v = w.tank(w.russians, 34, 31); // one cell away from the target: distance 256 = the full spread
	const int wh = w.sim->combat().warhead("APWarhead");
	w.sim->damage_area(w.sim->find(v)->location, 100, a, wh, w.americans);
	EXPECT_EQ(w.sim->find(v)->health, 350);     // direct: 100 * 0.5 (heavy)
	EXPECT_EQ(w.sim->find(near_v)->health, 375); // 256 away: the minimum 50 percent of 100, halved by armor
	EXPECT_EQ(w.sim->find(a)->health, 400);
}

TEST(Combat, TanksOrderedToAttackWalkIntoRangeAndKillTheTarget)
{
	TestWorld w;
	const ObjectId a = w.tank(w.americans, 30, 30, 64);
	const ObjectId b = w.tank(w.russians, 42, 30, 0);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::attack_event(w.americans, a, Target::object(b)));
	int frames = 0;
	while (frames < 3000 && w.sim->find(b)->alive) {
		ASSERT_TRUE(loop.run_frame());
		++frames;
	}
	EXPECT_FALSE(w.sim->find(b)->alive);
	EXPECT_TRUE(w.sim->find(a)->alive);
	// 400 HP at 50 per hit takes 8 shots at 50 to 52 frames each.
	EXPECT_GE(frames, 7 * 50);
	EXPECT_LE(frames, 400 + 8 * 52 + 200);
}

TEST(Combat, ALobbedShellFliesAnArcAndLandsOnTheTarget)
{
	TestWorld w;
	// An artillery piece: the lobbed projectile uses the ballistic solver and Rules Gravity.
	const ObjectId a = w.tank(w.americans, 30, 30);
	const ObjectId b = w.tank(w.russians, 36, 30);
	w.sim->info(*w.sim->find(a)); // (types are read on spawn)
	TypeInfo& ti = const_cast<TypeInfo&>(w.sim->info(*w.sim->find(a)));
	ti.weapon[0] = w.sim->combat().weapon("LobCannon");
	Object& shooter = *w.sim->find(a);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	const ObjectId bullet = w.sim->fire(shooter, Target::object(b), 0);
	ASSERT_NE(bullet, 0);
	int frames = 0, max_z = 0;
	while (frames < 200 && w.sim->find(bullet)->alive) {
		ASSERT_TRUE(loop.run_frame());
		max_z = std::max(max_z, w.sim->find(bullet)->location.z);
		++frames;
	}
	EXPECT_FALSE(w.sim->find(bullet)->alive);
	EXPECT_GT(max_z, 100); // it went up
	EXPECT_LT(w.sim->find(b)->health, 400); // and hit
}

TEST(FiringAI, ApproachTargetPicksACellInRangeOnTheUnitsSide)
{
	TestWorld w;
	const ObjectId a = w.tank(w.americans, 30, 40, 64);
	const ObjectId b = w.tank(w.russians, 50, 40, 192);
	Object& o = *w.sim->find(a);
	w.sim->order_attack(o, Target::object(b));
	const Target spot = w.sim->approach_target(o, false);
	ASSERT_TRUE(spot.is_cell());
	// Range 6 cells = 1536, minus 128 (range >= 512), minus 179.2: the first ring is 1228 leptons from the
	// target, toward the unit (west): the cell 4.8 cells west of (50, 40).
	EXPECT_EQ(spot.cell_y(), 40);
	EXPECT_EQ(spot.cell_x(), 45);
	EXPECT_TRUE(o.foot.has_destination);
	EXPECT_EQ(ra::cell_of(o.foot.destination), (Cell { 45, 40 }));
	// In range already: nothing to do.
	Object& near_o = *w.sim->find(w.tank(w.americans, 47, 38, 64));
	near_o.target = Target::object(b);
	EXPECT_TRUE(w.sim->approach_target(near_o, false).is_none());
}

TEST(FiringAI, AGuardingTankAcquiresAnEnemyInRangeTurnsAndFires)
{
	TestWorld w;
	const ObjectId guard = w.tank(w.americans, 30, 30, 0);   // facing north
	const ObjectId enemy = w.tank(w.russians, 35, 30, 192);  // 5 cells east, in range
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	ra::Random2 before = w.rng;
	ASSERT_TRUE(loop.run_frame());
	const Object& g = *w.sim->find(guard);
	EXPECT_EQ(g.target, Target::object(enemy));
	// The targeting timer drew its Random(0, 2); NormalTargetingDelay is 27 by default.
	EXPECT_GE(g.targeting_delay, 27);
	EXPECT_LE(g.targeting_delay, 29);
	EXPECT_NE(before.index1(), w.rng.index1());
	int frames = 0;
	while (frames < 400 && w.sim->find(enemy)->health == 400) {
		ASSERT_TRUE(loop.run_frame());
		++frames;
	}
	EXPECT_LT(w.sim->find(enemy)->health, 400);
	// It had to turn from north to east first (a quarter turn at ROT 5: 0x4000 / (5 << 8) = 12 frames).
	EXPECT_GE(frames, 10);
}

TEST(FiringAI, AnAttackedUnitInGuardRetaliates)
{
	TestWorld w;
	const ObjectId victim = w.tank(w.americans, 30, 30, 64);
	const ObjectId attacker = w.tank(w.russians, 45, 30, 192); // outside the victim's weapon range
	Object& v = *w.sim->find(victim);
	const int wh = w.sim->combat().warhead("APWarhead");
	w.sim->receive_damage(v, 100, 0, wh, attacker, w.russians);
	// Human-owned and out of range: only within (Sight + 0.5) cells, 8.5 cells here, so no.
	EXPECT_TRUE(v.target.is_none());
	const ObjectId close = w.tank(w.russians, 37, 30, 192);
	w.sim->receive_damage(v, 100, 0, wh, close, w.russians);
	EXPECT_EQ(v.target, Target::object(close));
	EXPECT_EQ(v.mission, Mission::Attack);
}

TEST(FiringAI, InfantryFireOnTheirFireUpFrame)
{
	TestWorld w;
	const ObjectId gi = w.gi(w.americans, 30, 30);
	const ObjectId enemy = w.tank(w.russians, 33, 30);
	Object& o = *w.sim->find(gi);
	w.sim->order_attack(o, Target::object(enemy));
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	int started = -1, shot = -1;
	for (int f = 0; f < 40 && shot < 0; ++f) {
		ASSERT_TRUE(loop.run_frame());
		if (started < 0 && o.is_firing)
			started = f;
		if (w.sim->find(enemy)->health < 400)
			shot = f;
	}
	ASSERT_GE(started, 0);
	ASSERT_GE(shot, 0);
	// FireUp=2 at rate 1: the shot is two stages after the sequence starts; the instant-hit bullet is
	// added to the end of the logic list and lands in the same pass.
	EXPECT_EQ(shot - started, 2);
	EXPECT_EQ(o.sequence, 4);
}

TEST(FiringAI, AScatterOrderMovesTheUnitToANeighbouringCell)
{
	TestWorld w;
	const ObjectId t = w.tank(w.americans, 30, 30, 0);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Event::with_target(EventType::Scatter, w.americans, Target::object(t)));
	ASSERT_TRUE(loop.run_frame());
	ASSERT_TRUE(loop.run_frame());
	const Object& o = *w.sim->find(t);
	ASSERT_TRUE(o.foot.has_destination);
	const Cell d = ra::cell_of(o.foot.destination);
	EXPECT_EQ(std::max(std::abs(d.x - 30), std::abs(d.y - 30)), 1);
}

TEST(Facing, TurnsAtRotAndInterpolatesLikeTheOriginal)
{
	FacingControl f;
	f.set_rot(5); // 0x500 per frame
	EXPECT_EQ(f.rot, 0x500);
	EXPECT_TRUE(f.set_desired(0x4000, 100));
	EXPECT_EQ(f.time_left, 0x4000 / 0x500); // 12 frames
	EXPECT_TRUE(f.rotating(100));
	// current = desired - (diff / steps) * remaining, with steps = |diff| / rot = 12 and diff / steps = 1365.
	EXPECT_EQ(f.current(100), uint16_t(0x4000 - 1365 * 12)); // 4: the original never starts exactly at 0
	EXPECT_EQ(f.current(106), uint16_t(0x4000 - 1365 * 6));
	EXPECT_EQ(f.current(112), 0x4000);
	EXPECT_FALSE(f.rotating(112));
	EXPECT_FALSE(f.set_desired(0x4000, 120)); // already the target
	// Turning the short way round through north.
	EXPECT_TRUE(f.set_desired(0xC000, 120));
	EXPECT_EQ(f.time_left, 0x8000 / 0x500);
	EXPECT_EQ(f.current(121), uint16_t(0xC000 - int16_t(int16_t(0xC000 - 0x4000) / int16_t(0x8000 / 0x500)) * int16_t(0x8000 / 0x500 - 1)));
	// ROT above 127 is capped; ROT 0 turns at once.
	FacingControl g;
	g.set_rot(500);
	EXPECT_EQ(g.rot, 0x7F00);
	FacingControl h;
	h.set_rot(0);
	h.set_desired(0x2000, 5);
	EXPECT_EQ(h.current(5), 0x2000);
}

TEST(Infantry, SpotsOfPointsAndTheFreeSpotSearch)
{
	EXPECT_EQ(Simulation::spot_of({ 128, 128, 0 }), 0);
	EXPECT_EQ(Simulation::spot_of({ 192, 64, 0 }), 2);
	EXPECT_EQ(Simulation::spot_of({ 64, 192, 0 }), 3);
	EXPECT_EQ(Simulation::spot_of({ 192, 192, 0 }), 4);
	EXPECT_EQ(Simulation::spot_of({ 64, 64, 0 }), 0);  // the top-left quadrant counts as the centre
	EXPECT_EQ(Simulation::spot_of({ 150, 140, 0 }), 0); // within 60 leptons of the centre

	TestWorld w;
	Coord out;
	// Starting from a corner spot that is free: that spot.
	ASSERT_TRUE(w.sim->find_free_spot({ 30, 30 }, { 30 * 256 + 200, 30 * 256 + 60, 0 }, out));
	EXPECT_EQ(out.x, 30 * 256 + 192);
	EXPECT_EQ(out.y, 30 * 256 + 64);
	// Starting from the centre draws one value from the scenario RNG to pick the order.
	ra::Random2 copy = w.rng;
	ASSERT_TRUE(w.sim->find_free_spot({ 30, 30 }, cell_center({ 30, 30 }), out));
	const int r = copy.ranged(0, 3);
	EXPECT_EQ(w.rng.index1(), copy.index1());
	const int first_of_rotation[4] = { 2, 2, 3, 4 }; // rotations {1,2,3,4}, {2,3,4,1}, {3,4,1,2}, {4,1,2,3}, spot 1 skipped
	EXPECT_EQ(Simulation::spot_of(out), first_of_rotation[r]);
	// A cell with a unit in it has no free spot.
	w.tank(w.americans, 31, 31);
	EXPECT_FALSE(w.sim->find_free_spot({ 31, 31 }, cell_center({ 31, 31 }), out));
}

TEST(CanEnterCell, TheHeightRule)
{
	TestWorld w;
	auto at = [&](int x, int y) { return w.world.grid.mutable_at(x, y); };
	at(31, 30)->level = 1;
	at(32, 30)->level = 1;
	at(33, 30)->level = 3;
	EXPECT_TRUE(w.sim->level_rule({ 30, 30 }, { 30, 31 }, 0));  // same level
	EXPECT_FALSE(w.sim->level_rule({ 30, 30 }, { 31, 30 }, 0)); // one up, no ramp
	at(30, 30)->slope = 2;                                      // a ramp in the cell climbed from
	EXPECT_TRUE(w.sim->level_rule({ 30, 30 }, { 31, 30 }, 0));
	EXPECT_FALSE(w.sim->level_rule({ 32, 30 }, { 33, 30 }, 1)); // two up: a cliff
	EXPECT_FALSE(w.sim->level_rule({ 31, 30 }, { 30, 31 }, 1) && at(30, 31)->slope == 0); // one down needs a ramp where it lands
	at(30, 31)->slope = 4;
	EXPECT_TRUE(w.sim->level_rule({ 31, 30 }, { 30, 31 }, 1));
}

TEST(Orders, TurnsOfFortyFiveDegreesUseTheCurvedTrack)
{
	TestWorld w;
	const ObjectId tank = w.tank(w.americans, 30, 30, 0); // north
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	// The hierarchical corridor makes the search take north-east, north, north-east here.
	loop.issue(Simulation::move_event(w.americans, tank, { 32, 27 }));
	const Object& o = *w.sim->find(tank);
	bool used_curve = false;
	bool turned_with_two_steps_left = false;
	const Coord start = o.location;
	run_until(loop, 900, [&] {
		if (o.foot.track_number >= 0 && (ra::sim::drive_tracks::turn_track(o.foot.track_number).flag & 8))
			used_curve = true;
		// Once driving, a 45-degree turn with a following step is made by a curved track; only a last
		// single step (no second cell for a curve) is turned to in place.
		if (o.foot.rotating && o.location != start && o.foot.path[1] != -1)
			turned_with_two_steps_left = true;
		return o.cell() == Cell { 32, 27 } && !o.foot.has_destination;
	});
	EXPECT_EQ(o.cell(), (Cell { 32, 27 }));
	EXPECT_TRUE(used_curve);
	EXPECT_FALSE(turned_with_two_steps_left);
}

TEST(Orders, DamagedUnitsDriveAtThreeQuartersOfTheirSpeed)
{
	TestWorld w;
	const ObjectId healthy = w.tank(w.americans, 30, 40, 0);
	const ObjectId damaged = w.sim->spawn(ra::rules::TypeKind::Vehicle, w.type(ra::rules::TypeKind::Vehicle, "HTNK"), w.americans, { 34, 40 }, Facing {}, 100);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	loop.issue(Simulation::move_event(w.americans, healthy, { 30, 30 }));
	loop.issue(Simulation::move_event(w.americans, damaged, { 34, 30 }));
	for (int i = 0; i < 80; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_DOUBLE_EQ(w.sim->find(damaged)->foot.target_speed, 0.75);
	EXPECT_DOUBLE_EQ(w.sim->find(healthy)->foot.target_speed, 1.0);
	EXPECT_LT(40 * 256 - w.sim->find(damaged)->location.y, 40 * 256 - w.sim->find(healthy)->location.y);
}

TEST(Missions, TheSchedulerDrawsTheDelayFromTheRngAndKeepsItsRhythm)
{
	TestWorld w;
	const ObjectId tank = w.unit("AMCV", w.americans, 30, 30, 0); // unarmed: no targeting timer draws
	// delay(Move) = ftol(0.016 * 900) + Random(0, 2) = 14 + (0..2); delay(Guard) = 26 + (0..2).
	ra::Random2 copy = w.rng;
	const int move_delay = w.sim->mission_delay(Mission::Move);
	EXPECT_EQ(move_delay, 14 + copy.ranged(0, 2));
	EXPECT_GE(move_delay, 14);
	EXPECT_LE(move_delay, 16);
	const int guard_delay = w.sim->mission_delay(Mission::Guard);
	const int guard_draw = copy.ranged(0, 2);
	EXPECT_EQ(guard_delay, 26 + guard_draw); // the truncating multiply makes 0.030 * 900 = 26.99.. -> 26
	EXPECT_EQ(w.rng.index1(), copy.index1()); // exactly one draw each

	// A new tank runs its Guard handler on its first update, then every 26 to 28 frames, one draw each.
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	ra::Random2 before = w.rng;
	int runs = 0;
	for (int f = 0; f < 120; ++f) {
		const int32_t last = w.sim->find(tank)->mission_timer_start;
		ASSERT_TRUE(loop.run_frame());
		if (w.sim->find(tank)->mission_timer_start != last || f == 0)
			++runs;
	}
	EXPECT_GE(runs, 4);
	EXPECT_LE(runs, 6);
	// Replaying one ranged(0, 2) per run from the same start reaches the same generator state.
	for (int i = 0; i < runs; ++i)
		before.ranged(0, 2);
	EXPECT_EQ(before.index1(), w.rng.index1());
	EXPECT_EQ(before.index2(), w.rng.index2());
}

TEST(Missions, QueueMissionFollowsTheOriginalsRules)
{
	TestWorld w;
	const ObjectId id = w.tank(w.americans, 30, 30, 0);
	Object& o = *w.sim->find(id);
	w.sim->queue_mission(o, Mission::Move, false);
	EXPECT_EQ(o.mission, Mission::Guard);
	EXPECT_EQ(o.mission_queue, static_cast<int>(Mission::Move));
	w.sim->commence_mission(o);
	EXPECT_EQ(o.mission, Mission::Move);
	EXPECT_EQ(o.mission_queue, -1);
	// Queueing the mission that is already running does nothing.
	w.sim->queue_mission(o, Mission::Move, false);
	EXPECT_EQ(o.mission_queue, -1);
	// A waiting object does not take Guard (the check at 0x5B35E0 refuses that pair); one under construction takes nothing.
	o.mission = Mission::Wait;
	o.mission_queue = -1;
	w.sim->queue_mission(o, Mission::Guard, true);
	EXPECT_EQ(o.mission, Mission::Wait);
	EXPECT_EQ(o.mission_queue, -1);
	o.mission = Mission::Construction;
	w.sim->queue_mission(o, Mission::Guard, true);
	EXPECT_EQ(o.mission, Mission::Construction);
	EXPECT_EQ(o.mission_queue, -1);
}

TEST(Sight, HeightMovesTheCircleUpTheScreenAndCliffsBlockTheView)
{
	TestWorld w;
	// A viewer at ground level sees a full circle of radius 8 around its cell.
	auto count = [&](const ra::Coord& at, int radius, bool by_height) {
		ra::world::ShroudMap shroud;
		shroud.reveal_from(w.world.grid, at, radius, by_height);
		int n = 0;
		for (int y = 15; y < 50; ++y)
			for (int x = 15; x < 50; ++x)
				n += shroud.revealed(x, y);
		return n;
	};
	const ra::Coord ground = cell_center({ 30, 30 }, 0);
	EXPECT_EQ(count(ground, 1, true), 9);
	EXPECT_EQ(count(ground, 2, false), 21);
	EXPECT_EQ(count(ground, 8, false), 205);
	// At height 4 levels (416 leptons: zpix = 60 px) the circle shifts two cells on both axes.
	ra::Coord high = ground;
	high.z = 4 * 104;
	ra::world::ShroudMap shroud;
	shroud.reveal_from(w.world.grid, high, 1, false);
	EXPECT_TRUE(shroud.revealed(28, 28));
	EXPECT_FALSE(shroud.revealed(30, 30));
	// A cliff more than three levels above the viewer hides what is behind it.
	for (int y = 20; y <= 40; ++y)
		for (int x = 31; x <= 33; ++x)
			w.world.grid.mutable_at(x, y)->level = 6;
	EXPECT_LT(count(ground, 8, true), count(ground, 8, false));
}

TEST(Veterancy, KillsPromoteTheKillerAndAbilitiesChangeTheNumbers)
{
	TestWorld w;
	const ObjectId a = w.unit("HTNK", w.americans, 30, 30);
	const ObjectId b = w.unit("HTNK", w.russians, 34, 30);
	Object& killer = *w.sim->find(a);
	TypeInfo& ti = const_cast<TypeInfo&>(w.sim->info(killer));
	ti.cost = 100;
	w.sim->combat().rules_for_test().veteran_cap = 2.0;
	const int wh = w.sim->combat().warhead("APWarhead");
	w.sim->receive_damage(*w.sim->find(b), 100000, 0, wh, a, w.americans, true);
	EXPECT_FALSE(w.sim->find(b)->alive);
	// value 100 / (cost 100 * ratio 10) = 0.1
	EXPECT_NEAR(killer.veterancy, 0.1, 1e-9);
	EXPECT_EQ(w.sim->house(w.americans).point_total, 100);
	EXPECT_EQ(w.sim->house(w.americans).killed_units, 1);
	// A veteran with STRONGER takes damage divided by VeteranArmor.
	ti.veteran_abilities = 1u << 1;
	w.sim->combat().rules_for_test().veteran_armor = 2.0;
	killer.veterancy = 1.0;
	const int before = killer.health;
	const ObjectId c = w.unit("HTNK", w.russians, 36, 30);
	w.sim->receive_damage(killer, 100, 0, wh, c, w.russians);
	EXPECT_EQ(before - killer.health, 25); // 100 / 2 = 50, halved by the heavy armor
}

namespace {

ObjectId building(TestWorld& w, const char* name, int house, int x, int y)
{
	return w.sim->spawn(ra::rules::TypeKind::Building, w.type(ra::rules::TypeKind::Building, name), house, { int16_t(x), int16_t(y) }, Facing {}, 256);
}

} // namespace

TEST(Economy, CostsFollowTheCountryAndBuildTimeFollowsCostAndPower)
{
	TestWorld w;
	const TypeRef tank = w.sim->type_ref(ra::rules::TypeKind::Vehicle, "HTNK");
	// Americans pay CostUnitsMult 0.5; Russians the full price.
	EXPECT_EQ(w.sim->actual_cost(w.americans, tank), 400);
	EXPECT_EQ(w.sim->actual_cost(w.russians, tank), 800);
	// GetBuildSpeed: ftol(800 * 1.0 * 0.9) = 720 frames.
	EXPECT_EQ(w.sim->time_to_build(w.russians, tank), 720);
	// Refund: human houses get RefundPercent (0.5 by default) back.
	EXPECT_EQ(w.sim->refund(w.russians, tank, false), 400);
	EXPECT_EQ(w.sim->refund(w.russians, tank, true), 800);
	// Low power slows production: a drain with no output gives p = 0, f = max(1 - 1 * 1.0, 0.5) -> 0.5.
	building(w, "GAWEAP", w.russians, 20, 20);
	EXPECT_DOUBLE_EQ(w.sim->power_percentage(w.russians), 0.0);
	EXPECT_EQ(w.sim->time_to_build(w.russians, tank), 1440); // 720 / 0.5
}

TEST(Economy, CanBuildNeedsPrerequisitesOwnersAndTechLevel)
{
	TestWorld w;
	const TypeRef tank = w.sim->type_ref(ra::rules::TypeKind::Vehicle, "HTNK");
	const TypeRef weap = w.sim->type_ref(ra::rules::TypeKind::Building, "GAWEAP");
	const TypeRef plant = w.sim->type_ref(ra::rules::TypeKind::Building, "GAPOWR");
	const TypeRef russian_plant = w.sim->type_ref(ra::rules::TypeKind::Building, "NAPOWR");
	EXPECT_EQ(w.sim->can_build(w.americans, plant, false, false), 0); // needs a construction yard
	building(w, "GACNST", w.americans, 20, 20);
	EXPECT_EQ(w.sim->can_build(w.americans, plant, false, false), 1);
	EXPECT_EQ(w.sim->can_build(w.americans, weap, false, false), 0);   // needs power
	building(w, "GAPOWR", w.americans, 24, 20);
	building(w, "GAWEAP", w.americans, 28, 20);
	// Tech level: the tank needs 2, the scenario allows 10 by default.
	EXPECT_EQ(w.sim->can_build(w.americans, tank, false, false), 1);
	w.sim->house(w.americans).tech_level = 1;
	EXPECT_EQ(w.sim->can_build(w.americans, tank, false, false), 0);
	// Owner restricts to the Russians.
	EXPECT_EQ(w.sim->can_build(w.americans, russian_plant, false, false), 0);
	// AI houses skip prerequisites.
	w.sim->house(w.russians).human = false;
	EXPECT_EQ(w.sim->can_build(w.russians, plant, false, false), 1);
}

TEST(Economy, AFactoryChargesStepByStepAndPausesWhenBroke)
{
	TestWorld w;
	building(w, "GACNST", w.russians, 20, 20);
	building(w, "GAPOWR", w.russians, 24, 20);
	building(w, "GAWEAP", w.russians, 28, 20);
	const TypeRef tank = w.sim->type_ref(ra::rules::TypeKind::Vehicle, "HTNK");
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	w.sim->give_money(w.russians, 300);
	ASSERT_TRUE(w.sim->start_production(w.russians, tank));
	const Factory& f = w.sim->factory(w.russians, Category::Unit);
	ASSERT_TRUE(f.active);
	EXPECT_EQ(f.balance, 800);
	// 720 / 54 = 13 frames a step.
	EXPECT_EQ(f.rate, 13);
	for (int i = 0; i < 13; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_EQ(f.step, 1);
	EXPECT_EQ(f.balance, 800 - 800 / 53); // one step: balance / (54 - 1)
	// 300 credits pay for part of it; the rest waits.
	for (int i = 0; i < 700; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_TRUE(f.on_hold);
	EXPECT_LT(f.step, 54);
	EXPECT_EQ(w.sim->available(w.russians), 0 + (300 - w.sim->house(w.russians).credits_spent));
	// More money lets it finish; the total spent is exactly the cost.
	w.sim->give_money(w.russians, 1000);
	for (int i = 0; i < 900 && !f.done(); ++i)
		ASSERT_TRUE(loop.run_frame());
	ASSERT_TRUE(f.done());
	EXPECT_EQ(w.sim->house(w.russians).credits_spent, 800);
	const ObjectId unit = w.sim->exit_unit(w.russians, Category::Unit);
	ASSERT_NE(unit, 0);
	EXPECT_EQ(w.sim->find(unit)->house, w.russians);
	EXPECT_FALSE(w.sim->factory(w.russians, Category::Unit).active);
}

TEST(Economy, AbandoningRefundsWhatWasPaid)
{
	TestWorld w;
	building(w, "GACNST", w.russians, 20, 20);
	building(w, "GAPOWR", w.russians, 24, 20);
	building(w, "GAWEAP", w.russians, 28, 20);
	const TypeRef tank = w.sim->type_ref(ra::rules::TypeKind::Vehicle, "HTNK");
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	w.sim->give_money(w.russians, 1000);
	ASSERT_TRUE(w.sim->start_production(w.russians, tank));
	for (int i = 0; i < 100; ++i)
		ASSERT_TRUE(loop.run_frame());
	const int paid = 1000 - w.sim->available(w.russians);
	EXPECT_GT(paid, 0);
	w.sim->abandon_production(w.russians, Category::Unit);
	EXPECT_EQ(w.sim->available(w.russians), 1000);
	EXPECT_FALSE(w.sim->factory(w.russians, Category::Unit).active);
}

TEST(Economy, PlacementNeedsFreeFlatLandNextToTheBase)
{
	TestWorld w;
	building(w, "GACNST", w.americans, 30, 30);
	const TypeRef plant = w.sim->type_ref(ra::rules::TypeKind::Building, "GAPOWR");
	EXPECT_TRUE(w.sim->can_place(w.americans, plant, { 34, 30 }, true));  // Adjacent 3 + 1 cells around the 3x3
	EXPECT_FALSE(w.sim->can_place(w.americans, plant, { 45, 30 }, true)); // too far from the base
	EXPECT_TRUE(w.sim->can_place(w.americans, plant, { 45, 30 }, false));
	EXPECT_FALSE(w.sim->can_place(w.americans, plant, { 31, 31 }, false)); // on top of the yard
	w.gi(w.americans, 35, 31);
	EXPECT_FALSE(w.sim->can_place(w.americans, plant, { 34, 30 }, true)); // a soldier stands in the way
	w.world.grid.mutable_at(40, 40)->slope = 3;
	EXPECT_FALSE(w.sim->can_place(w.americans, plant, { 40, 40 }, false));
}

TEST(Economy, ProductionEventsPlaceBuildingsSellAndRepair)
{
	TestWorld w;
	const ObjectId yard = building(w, "GACNST", w.americans, 30, 30);
	(void)yard;
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	w.sim->give_money(w.americans, 5000);
	const TypeRef plant = w.sim->type_ref(ra::rules::TypeKind::Building, "GAPOWR");
	loop.issue(Simulation::produce_event(w.americans, plant));
	int frames = 0;
	while (frames < 1500 && !w.sim->factory(w.americans, Category::Building).done()) {
		ASSERT_TRUE(loop.run_frame());
		++frames;
	}
	ASSERT_TRUE(w.sim->factory(w.americans, Category::Building).done());
	EXPECT_EQ(w.sim->house(w.americans).credits_spent, 800);
	loop.issue(Simulation::place_event(w.americans, plant, { 34, 30 }));
	ASSERT_TRUE(loop.run_frame());
	ASSERT_TRUE(loop.run_frame());
	EXPECT_EQ(w.sim->owned_count(w.americans, plant), 1);
	EXPECT_FALSE(w.sim->factory(w.americans, Category::Building).active);
	// The new plant is "damaged": repair costs credits and heals in steps.
	ObjectId plant_id = 0;
	for (const Object& o : w.sim->objects())
		if (o.what == What::Building && o.type == plant.type)
			plant_id = o.id;
	Object& p = *w.sim->find(plant_id);
	p.health = 600;
	loop.issue(Simulation::repair_event(w.americans, plant_id));
	const int money = w.sim->available(w.americans);
	for (int i = 0; i < 200; ++i)
		ASSERT_TRUE(loop.run_frame());
	EXPECT_GT(p.health, 600);
	EXPECT_LT(w.sim->available(w.americans), money);
	// Selling pays the refund (half the cost for a human house) and removes the building.
	const int before = w.sim->available(w.americans);
	loop.issue(Simulation::sell_event(w.americans, plant_id));
	ASSERT_TRUE(loop.run_frame());
	ASSERT_TRUE(loop.run_frame());
	EXPECT_FALSE(w.sim->find(plant_id)->alive);
	EXPECT_EQ(w.sim->available(w.americans), before + 400);
}

TEST(Harvesting, AHarvesterGathersOreReturnsToTheRefineryAndGetsPaid)
{
	TestWorld w;
	ra::world::OverlayInfo ore;
	ore.name = "TIB01";
	ore.tiberium = true;
	w.world.overlays.push_back(ore);
	w.sim.reset(new Simulation(w.world, w.rng));
	w.americans = w.sim->house_index("Americans");
	// Three ore cells of full stage (12 bales each) east of the start.
	for (int x = 36; x <= 38; ++x) {
		w.world.grid.mutable_at(x, 30)->overlay = 0;
		w.world.grid.mutable_at(x, 30)->overlay_data = 11;
	}
	w.sim.reset(new Simulation(w.world, w.rng));
	w.americans = w.sim->house_index("Americans");
	building(w, "GAREF", w.americans, 26, 26);
	const ObjectId h = w.unit("HARV", w.americans, 30, 30, 64);
	EXPECT_EQ(w.sim->ore_type({ 36, 30 }), 0);
	EXPECT_EQ(w.sim->ore_value({ 36, 30 }), 25 * 12);
	GameLoop loop(*w.sim, w.rng);
	w.sim->attach(loop);
	w.sim->queue_mission(*w.sim->find(h), Mission::Harvest, true);
	int frames = 0;
	while (frames < 3000 && w.sim->available(w.americans) == 0) {
		ASSERT_TRUE(loop.run_frame());
		++frames;
	}
	// The first load (capacity 3 bales) is paid as 3 x 25 credits at the dock.
	EXPECT_EQ(w.sim->available(w.americans), 75);
	EXPECT_EQ(w.sim->house(w.americans).point_total, 15);  // ftol(3 * 5.0)
	// The three bales came off the ore cells.
	int stages = 0;
	for (int x = 36; x <= 38; ++x)
		stages += w.sim->ore_type({ int16_t(x), 30 }) >= 0 ? w.sim->ore_stage({ int16_t(x), 30 }) + 1 : 0;
	EXPECT_EQ(stages, 36 - 3);
}
