// Compares two frame CRC logs ("frame crc" per line, as replay_runner writes them) and shows the
// first frame where they differ.
//
//   crc_diff a.txt b.txt
//
// Exit code: 0 when the logs match, 1 when they differ, 2 on bad arguments or unreadable files.

#include "sim/loop/recording.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace ra;

namespace {

bool read(const char* path, sim::CrcLog& out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	std::stringstream ss;
	ss << in.rdbuf();
	out = sim::CrcLog::parse(ss.str());
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::fprintf(stderr, "usage: crc_diff a.txt b.txt\n");
		return 2;
	}
	sim::CrcLog a, b;
	if (!read(argv[1], a) || !read(argv[2], b)) {
		std::fprintf(stderr, "cannot read both logs\n");
		return 2;
	}
	const auto diff = sim::CrcLog::first_difference(a, b);
	if (!diff) {
		std::printf("identical: %zu frames\n", a.entries.size());
		return 0;
	}
	std::printf("first difference at frame %u\n", *diff);
	auto show = [&](const sim::CrcLog& log, const char* name) {
		for (const auto& [frame, crc] : log.entries)
			if (frame + 3 >= *diff && frame <= *diff + 3)
				std::printf("  %s frame %u: %08X%s\n", name, frame, crc, frame == *diff ? "  <--" : "");
	};
	show(a, "a");
	show(b, "b");
	std::printf("frames: a has %zu, b has %zu\n", a.entries.size(), b.entries.size());
	return 1;
}
