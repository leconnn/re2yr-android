// Replays a recording (the original's RECORD.BIN layout) through the game loop and writes the
// frame CRC of every frame, for comparing runs with crc_diff.
//
//   replay_runner RECORD.BIN --crc out.txt [--frames N] [--seed S] [--events]
//
// The recording is played back the way the original plays it back: each frame computes the frame
// CRC (one RNG draw) and logs it (another draw) before the frame's events run. The simulation is
// still being written, so for now the CRC covers the RNG and the events only.

#include "core/random.h"
#include "sim/loop/game_loop.h"
#include "sim/loop/recording.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ra;

namespace {

// Executes nothing yet; prints the events when asked. Every house counts as human, so recorded
// events of any house run.
struct PrintingSink : sim::EventSink {
	bool print = false;
	const sim::GameLoop* loop = nullptr;
	int house_count() const override { return 32; }
	bool house_is_human(int) const override { return true; }
	void execute(const sim::Event& e) override
	{
		if (print)
			std::printf("frame %u: %s house %d\n", loop ? loop->frame() : 0u, std::string(sim::event_name(e.type)).c_str(), int(e.house));
	}
};

int usage()
{
	std::fprintf(stderr, "usage: replay_runner RECORD.BIN --crc out.txt [--frames N] [--seed S] [--events]\n");
	return 2;
}

} // namespace

int main(int argc, char** argv)
{
	std::string record_path, crc_path;
	long max_frames = -1;
	long long seed_override = -1;
	bool print_events = false;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--crc" && i + 1 < argc)
			crc_path = argv[++i];
		else if (a == "--frames" && i + 1 < argc)
			max_frames = std::strtol(argv[++i], nullptr, 10);
		else if (a == "--seed" && i + 1 < argc)
			seed_override = std::strtoll(argv[++i], nullptr, 0);
		else if (a == "--events")
			print_events = true;
		else if (!a.empty() && a[0] != '-')
			record_path = a;
		else
			return usage();
	}
	if (record_path.empty())
		return usage();

	std::ifstream in(record_path, std::ios::binary);
	if (!in) {
		std::fprintf(stderr, "cannot open %s\n", record_path.c_str());
		return 1;
	}
	const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	sim::RecordingReader reader;
	if (!reader.open(bytes)) {
		std::fprintf(stderr, "%s is too short to be a recording\n", record_path.c_str());
		return 1;
	}
	const auto& h = reader.header();
	const uint32_t seed = seed_override >= 0 ? uint32_t(seed_override) : h.seed;
	std::printf("recording: scenario \"%s\", seed %u, game speed %d\n", h.scenario_name().c_str(), seed, int(h.options[0]));

	Random2 scenario_random(seed);
	PrintingSink sink;
	sink.print = print_events;
	sim::GameLoop loop(sink, scenario_random);
	sink.loop = &loop;
	sim::CrcLog log;
	loop.crc_log = &log;
	loop.start_playback(&reader);
	while ((max_frames < 0 || long(loop.frame()) < max_frames) && loop.run_frame()) {
	}
	std::printf("played %u frames\n", loop.frame());
	if (loop.last_result() == sim::EventQueue::Result::TooLate)
		std::printf("stopped: an event was scheduled for a frame that had already passed\n");

	if (!crc_path.empty()) {
		std::ofstream out(crc_path, std::ios::binary);
		out << log.to_text();
		std::printf("wrote %zu frame CRCs to %s\n", log.entries.size(), crc_path.c_str());
	}
	return 0;
}
