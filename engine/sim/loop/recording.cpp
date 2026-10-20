#include "sim/loop/recording.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace ra::sim {

namespace {

void put32(std::vector<uint8_t>& out, uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		out.push_back(uint8_t(v >> (8 * i)));
}
uint32_t get32(const uint8_t* p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

} // namespace

std::string RecordingHeader::scenario_name() const
{
	return std::string(scenario.data(), strnlen(scenario.data(), scenario.size()));
}

void RecordingHeader::set_scenario_name(const std::string& name)
{
	scenario.fill(0);
	std::memcpy(scenario.data(), name.data(), std::min(name.size(), scenario.size() - 1));
}

RecordingWriter::RecordingWriter(const RecordingHeader& h)
{
	put32(bytes_, h.seed);
	put32(bytes_, h.value_a8ed94);
	put32(bytes_, h.session_1254);
	bytes_.insert(bytes_.end(), h.scenario.begin(), h.scenario.end());
	put32(bytes_, h.value_a8ec90);
	put32(bytes_, h.option_flags);
	bytes_.insert(bytes_.end(), h.options.begin(), h.options.end());
}

void RecordingWriter::add_frame(std::span<const Event> events)
{
	put32(bytes_, static_cast<uint32_t>(events.size()));
	std::array<uint8_t, Event::kRecordSize> record;
	for (const Event& e : events) {
		e.write_record(record);
		bytes_.insert(bytes_.end(), record.begin(), record.end());
	}
}

bool RecordingReader::open(std::span<const uint8_t> data)
{
	data_ = data;
	pos_ = 0;
	if (data.size() < RecordingHeader::kSize)
		return false;
	const uint8_t* p = data.data();
	header_.seed = get32(p);
	header_.value_a8ed94 = get32(p + 4);
	header_.session_1254 = get32(p + 8);
	std::memcpy(header_.scenario.data(), p + 12, 260);
	header_.value_a8ec90 = get32(p + 272);
	header_.option_flags = get32(p + 276);
	std::memcpy(header_.options.data(), p + 280, 0xB8);
	pos_ = RecordingHeader::kSize;
	return true;
}

std::optional<std::vector<Event>> RecordingReader::next_frame()
{
	if (pos_ + 4 > data_.size())
		return std::nullopt;
	const uint32_t count = get32(data_.data() + pos_);
	if (count > 0x4000 || pos_ + 4 + size_t(count) * Event::kRecordSize > data_.size())
		return std::nullopt;
	pos_ += 4;
	std::vector<Event> events;
	events.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		events.push_back(Event::read_record(std::span<const uint8_t, Event::kRecordSize>(data_.data() + pos_, Event::kRecordSize)));
		pos_ += Event::kRecordSize;
	}
	return events;
}

std::string CrcLog::to_text() const
{
	std::string out;
	char line[32];
	for (const auto& [frame, crc] : entries) {
		std::snprintf(line, sizeof line, "%u %08X\n", frame, crc);
		out += line;
	}
	return out;
}

CrcLog CrcLog::parse(std::string_view text)
{
	CrcLog log;
	std::istringstream in { std::string(text) };
	std::string line;
	while (std::getline(in, line)) {
		unsigned frame = 0, crc = 0;
		if (std::sscanf(line.c_str(), "%u %x", &frame, &crc) == 2)
			log.add(frame, crc);
	}
	return log;
}

std::optional<uint32_t> CrcLog::first_difference(const CrcLog& a, const CrcLog& b)
{
	const size_t n = std::min(a.entries.size(), b.entries.size());
	for (size_t i = 0; i < n; ++i)
		if (a.entries[i] != b.entries[i])
			return std::min(a.entries[i].first, b.entries[i].first);
	if (a.entries.size() != b.entries.size()) {
		const CrcLog& longer = a.entries.size() > b.entries.size() ? a : b;
		return longer.entries[n].first;
	}
	return std::nullopt;
}

} // namespace ra::sim
