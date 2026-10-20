#include "sim/loop/game_loop.h"
#include "sim/loop/recording.h"
#include "sim/objects/drive_tracks.h"
#include "sim/objects/selection.h"
#include "sim/objects/simulation.h"

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
[Countries]
1=Americans
2=Russians
[InfantryTypes]
1=E1
[VehicleTypes]
1=HTNK
2=AMCV
[BuildingTypes]
1=GAPOWR
[E1]
Strength=125
Speed=4
Sight=5
[HTNK]
Strength=400
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
[GAPOWR]
Strength=750
[Clear]
Foot=100%
Track=100%
Wheel=100%
Float=0%
Hover=50%
Amphibious=80%
FloatBeach=0%
[Water]
Foot=0%
Track=0%
Wheel=0%
Float=100%
Hover=100%
Amphibious=100%
FloatBeach=100%
)";
const char* kArt = R"(
[GAPOWR]
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
		world.grid.set_size(60, 60, 10, 10, 50, 50);
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
