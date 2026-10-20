#include "sim/loop/event.h"
#include "sim/loop/event_queue.h"
#include "sim/loop/frame_crc.h"
#include "sim/loop/game_loop.h"
#include "sim/loop/recording.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace ra::sim;

namespace {

// Records what was executed, in order.
struct TestSink : EventSink {
	std::vector<bool> human = { true, false, true };
	std::vector<std::string> log;
	std::vector<uint32_t> desyncs;
	int house_count() const override { return static_cast<int>(human.size()); }
	bool house_is_human(int house) const override { return human[static_cast<size_t>(house)]; }
	void execute(const Event& e) override
	{
		log.push_back(std::string(event_name(e.type)) + ":" + std::to_string(e.house) + ":" + std::to_string(e.target().id));
	}
	void out_of_sync(uint32_t frame) override { desyncs.push_back(frame); }
};

Event make(EventType type, int house, uint32_t frame, int32_t id = 0)
{
	Event e = Event::with_target(type, house, Target::object(id));
	e.frame = frame;
	return e;
}

} // namespace

TEST(Event, PayloadSizesFromTheOriginalTable)
{
	EXPECT_EQ(event_payload_size(EventType::MegaMission), 23);
	EXPECT_EQ(event_payload_size(EventType::MegaMissionF), 24);
	EXPECT_EQ(event_payload_size(EventType::Place), 16);
	EXPECT_EQ(event_payload_size(EventType::FrameInfo), 7);
	EXPECT_EQ(event_payload_size(EventType::MegaFrameInfo), 104);
	EXPECT_EQ(event_payload_size(EventType::AbandonAll), 12);
	EXPECT_EQ(event_name(EventType::Exit), "EXIT");
	EXPECT_EQ(event_name(static_cast<EventType>(200)), "");
}

TEST(Event, RecordIs111BytesInTheOriginalLayout)
{
	MegaMission m;
	m.whom = Target::object(0x01020304);
	m.mission = 2; // Move
	m.destination = Target::cell(55, 66);
	Event e = m.to_event(3);
	e.frame = 0x11223344;
	e.executed = true;
	std::array<uint8_t, Event::kRecordSize> rec {};
	e.write_record(rec);
	EXPECT_EQ(rec[0], 4);          // type
	EXPECT_EQ(rec[1], 1);          // executed
	EXPECT_EQ(rec[2], 3);          // house
	EXPECT_EQ(rec[3], 0x44);       // frame, little endian
	EXPECT_EQ(rec[6], 0x11);
	EXPECT_EQ(rec[7], 0x04);       // Whom id
	EXPECT_EQ(rec[11], 0x34);      // Whom rtti: object
	EXPECT_EQ(rec[12], 2);         // mission
	const Event back = Event::read_record(rec);
	EXPECT_EQ(back, e);
	const MegaMission m2 = MegaMission::from_event(back);
	EXPECT_EQ(m2.destination.cell_x(), 55);
	EXPECT_EQ(m2.destination.cell_y(), 66);
	EXPECT_EQ(m2.whom, m.whom);
}

TEST(EventQueue, ExecutionFrameRoundsUpToTheSendRate)
{
	EXPECT_EQ(EventQueue::execution_frame(100, 0, 1), 100u);
	EXPECT_EQ(EventQueue::execution_frame(100, 7, 0), 107u);
	EXPECT_EQ(EventQueue::execution_frame(100, 7, 5), 110u); // (5 + 100 - 1 + 7) / 5 * 5
	EXPECT_EQ(EventQueue::execution_frame(98, 7, 5), 105u);
}

TEST(EventQueue, HousesInIndexOrderAndMegamissionsLast)
{
	EventQueue q;
	TestSink sink;
	MegaMission move;
	move.mission = 2;
	move.whom = Target::object(7);
	Event mm = move.to_event(0);
	mm.frame = 5;
	q.add(make(EventType::Deploy, 2, 5, 1));   // house 2
	q.add(mm);                                  // house 0, a megamission
	q.add(make(EventType::Sell, 0, 5, 2));     // house 0
	q.add(make(EventType::Idle, 1, 5, 3));     // house 1 is not human: never runs
	q.add(make(EventType::Scatter, 0, 6, 4));  // a later frame
	EXPECT_EQ(q.execute(5, sink), EventQueue::Result::Ok);
	const std::vector<std::string> expected = { "SELL:0:2", "MEGAMISSION:0:7", "DEPLOY:2:1" };
	EXPECT_EQ(sink.log, expected);
	q.purge(5);
	// The front was executed; the non-human event is stale (frame 5 < 6 later) but still at the front now.
	EXPECT_EQ(q.pending(), 2u);
	sink.log.clear();
	EXPECT_EQ(q.execute(6, sink), EventQueue::Result::Ok);
	EXPECT_EQ(sink.log, std::vector<std::string> { "SCATTER:0:4" });
	q.purge(6);
	EXPECT_EQ(q.pending(), 0u);
}

TEST(EventQueue, LateEventsAreFatalAndFrameInfoChecksTheCrc)
{
	EventQueue q;
	TestSink sink;
	q.add(make(EventType::Deploy, 0, 3, 1));
	EXPECT_EQ(q.execute(4, sink), EventQueue::Result::TooLate);
	EXPECT_TRUE(sink.log.empty());

	EventQueue q2;
	q2.set_frame_crc(10, 0xCAFEF00D);
	FrameInfo good { 0xCAFEF00D, 0, 2 };
	Event e = good.to_event(0);
	e.frame = 12; // checks frame 12 - 2 = 10
	q2.add(e);
	FrameInfo bad { 0x12345678, 0, 0 };
	Event e2 = bad.to_event(2);
	e2.frame = 12;
	q2.add(e2);
	EXPECT_EQ(q2.execute(12, sink), EventQueue::Result::OutOfSync);
	EXPECT_EQ(sink.desyncs, std::vector<uint32_t> { 12 });
}

TEST(EventQueue, AreaGuardRunsAreSpreadFirst)
{
	struct SpreadSink : TestSink {
		size_t spread = 0;
		void spread_area_guard(std::vector<Event*>& run) override { spread = run.size(); }
	} sink;
	EventQueue q;
	for (int i = 0; i < 3; ++i) {
		MegaMission m;
		m.mission = EventQueue::kMissionAreaGuard;
		m.whom = Target::object(100 + i);
		m.destination = Target::cell(10, 10);
		Event e = m.to_event(0);
		e.frame = 1;
		q.add(e);
	}
	q.execute(1, sink);
	EXPECT_EQ(sink.spread, 3u);
	EXPECT_EQ(sink.log.size(), 3u);
}

TEST(FrameCrc, RotateAndAddWithTruncatingDivisions)
{
	FrameCrc crc;
	crc.add(1);
	EXPECT_EQ(crc.value(), 1u);
	crc.add(0x80000000u);
	EXPECT_EQ(crc.value(), 0x80000002u);
	crc.add(0);
	EXPECT_EQ(crc.value(), 0x00000005u); // the top bit wraps around
	EXPECT_EQ(FrameCrc::position_term(-19, 25, 3), uint32_t(-1) + (2u << 16) + 3u);
	EXPECT_EQ(FrameCrc::facing_byte(0x4000), 0x40u);
	EXPECT_EQ(FrameCrc::facing_byte(0x00FF), 1u);
}

TEST(Recording, RoundTripsHeaderAndFrames)
{
	RecordingHeader h;
	h.seed = 0xDEADBEEF;
	h.set_scenario_name("ALL01UMD.MAP");
	h.options[0] = 4;
	RecordingWriter w(h);
	w.add_frame({});
	std::vector<Event> two = { make(EventType::Deploy, 0, 1, 9), make(EventType::Sell, 0, 1, 10) };
	w.add_frame(two);
	EXPECT_EQ(w.bytes().size(), RecordingHeader::kSize + 4 + 4 + 2 * 111);

	RecordingReader r;
	ASSERT_TRUE(r.open(w.bytes()));
	EXPECT_EQ(r.header().seed, 0xDEADBEEFu);
	EXPECT_EQ(r.header().scenario_name(), "ALL01UMD.MAP");
	EXPECT_EQ(r.header().options[0], 4);
	auto f0 = r.next_frame();
	ASSERT_TRUE(f0);
	EXPECT_TRUE(f0->empty());
	auto f1 = r.next_frame();
	ASSERT_TRUE(f1);
	EXPECT_EQ(*f1, two);
	EXPECT_FALSE(r.next_frame());
}

TEST(Recording, CrcLogFindsTheFirstDifference)
{
	CrcLog a, b;
	for (uint32_t f = 0; f < 5; ++f) {
		a.add(f, f * 7);
		b.add(f, f == 3 ? 99 : f * 7);
	}
	EXPECT_EQ(CrcLog::first_difference(a, a), std::nullopt);
	EXPECT_EQ(CrcLog::first_difference(a, b), 3u);
	const CrcLog parsed = CrcLog::parse(a.to_text());
	EXPECT_EQ(parsed.entries, a.entries);
	CrcLog shorter = a;
	shorter.entries.pop_back();
	EXPECT_EQ(CrcLog::first_difference(a, shorter), 4u);
}

TEST(GameLoop, LogicStepsRunInOrderEachFrame)
{
	TestSink sink;
	ra::Random2 rng(12345);
	GameLoop loop(sink, rng);
	std::vector<int> order;
	loop.logic().set_step(LogicStep::Houses, [&] { order.push_back(3); });
	loop.logic().set_step(LogicStep::LogicObjects, [&] { order.push_back(2); });
	loop.logic().set_step(LogicStep::ScenarioTimers, [&] { order.push_back(1); });
	ASSERT_TRUE(loop.run_frame());
	EXPECT_EQ(order, (std::vector<int> { 1, 2, 3 }));
	EXPECT_EQ(loop.frame(), 1u);
	EXPECT_EQ(loop.logic().logic_frame(), 1u);
}

TEST(GameLoop, RecordedEventsPlayBackOnTheSameFramesWithTwoDrawsPerFrame)
{
	// Record: issue two actions on frames 1 and 3.
	TestSink rec_sink;
	ra::Random2 rec_rng(777);
	GameLoop rec(rec_sink, rec_rng);
	RecordingHeader h;
	h.seed = 777;
	RecordingWriter writer(h);
	rec.start_recording(&writer);
	for (int f = 0; f < 5; ++f) {
		if (f == 1)
			rec.issue(Event::with_target(EventType::Deploy, 0, Target::object(42)));
		if (f == 3)
			rec.issue(Event::with_target(EventType::Sell, 2, Target::object(43)));
		ASSERT_TRUE(rec.run_frame());
	}
	EXPECT_EQ(rec_sink.log, (std::vector<std::string> { "DEPLOY:0:42", "SELL:2:43" }));
	EXPECT_EQ(rec_rng.index1(), ra::Random2(777).index1()); // normal single-player play draws nothing

	// Play back twice: the same events on the same frames, the same CRCs, and two draws per frame.
	auto play = [&](CrcLog& log, ra::Random2& rng, TestSink& sink) {
		RecordingReader reader;
		EXPECT_TRUE(reader.open(writer.bytes()));
		GameLoop loop(sink, rng);
		loop.start_playback(&reader);
		loop.crc_log = &log;
		while (loop.run_frame()) {
		}
	};
	CrcLog log1, log2;
	TestSink s1, s2;
	ra::Random2 r1(777), r2(777);
	play(log1, r1, s1);
	play(log2, r2, s2);
	EXPECT_EQ(s1.log, rec_sink.log);
	EXPECT_EQ(log1.entries.size(), 6u); // five frames played, the sixth CRC is computed before the end is found
	EXPECT_EQ(CrcLog::first_difference(log1, log2), std::nullopt);
	ra::Random2 expect(777);
	for (int i = 0; i < 12; ++i)
		expect.next();
	EXPECT_EQ(r1.index1(), expect.index1());
	EXPECT_EQ(r1.table(), expect.table());
}
