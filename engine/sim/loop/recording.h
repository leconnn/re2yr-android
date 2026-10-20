#pragma once

// Recordings of a game's events, in the original's RECORD.BIN layout (P3 section 13.5), and frame
// CRC logs for comparing two runs.
//
// RECORD.BIN: a header with the values a game needs to start the same way (seed, scenario name,
// options), then for every frame an int32 count and that many 111-byte event records.

#include "sim/loop/event.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ra::sim {

struct RecordingHeader {
	uint32_t seed = 0;                     // 0x822CF4, the scenario RNG seed
	uint32_t value_a8ed94 = 0;             // 0xA8ED94 (meaning not identified)
	uint32_t session_1254 = 0;             // session +0x1254 (meaning not identified)
	std::array<char, 260> scenario {};     // session +0x125C, the scenario file name
	uint32_t value_a8ec90 = 0;             // 0xA8EC90 (meaning not identified)
	uint32_t option_flags = 0;             // 0xA8E960, game option flags
	std::array<uint8_t, 0xB8> options {};  // 0xA8EB60, the game options block (GameSpeed is its first int)

	static constexpr size_t kSize = 4 + 4 + 4 + 260 + 4 + 4 + 0xB8;

	std::string scenario_name() const;
	void set_scenario_name(const std::string& name);
};

// Writes a recording into memory; the caller saves the bytes.
class RecordingWriter {
public:
	explicit RecordingWriter(const RecordingHeader& header);
	// One frame's events (the recorder writes a count even when it is zero).
	void add_frame(std::span<const Event> events);
	const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
	std::vector<uint8_t> bytes_;
};

// Reads a recording frame by frame.
class RecordingReader {
public:
	// False if the data is too short for the header.
	bool open(std::span<const uint8_t> data);
	const RecordingHeader& header() const { return header_; }
	// The next frame's events, or nothing at the end of the data (or if it is cut short).
	std::optional<std::vector<Event>> next_frame();
	bool at_end() const { return pos_ >= data_.size(); }

private:
	std::span<const uint8_t> data_;
	size_t pos_ = 0;
	RecordingHeader header_;
};

// A per-frame CRC log: one line "frame crc" (decimal frame, 8 hex digits) per frame.
struct CrcLog {
	std::vector<std::pair<uint32_t, uint32_t>> entries; // frame, crc

	void add(uint32_t frame, uint32_t crc) { entries.emplace_back(frame, crc); }
	std::string to_text() const;
	static CrcLog parse(std::string_view text);

	// The first frame where the two logs differ (or where one ends early), if any.
	static std::optional<uint32_t> first_difference(const CrcLog& a, const CrcLog& b);
};

} // namespace ra::sim
