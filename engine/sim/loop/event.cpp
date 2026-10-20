#include "sim/loop/event.h"

#include <cstring>

namespace ra::sim {

namespace {

struct EventInfo {
	const char* name;
	int payload;
};
const EventInfo kEvents[kEventTypeCount] = {
#include "sim/loop/event_lengths.inc"
};

void put32(uint8_t* p, uint32_t v)
{
	p[0] = uint8_t(v);
	p[1] = uint8_t(v >> 8);
	p[2] = uint8_t(v >> 16);
	p[3] = uint8_t(v >> 24);
}
uint32_t get32(const uint8_t* p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

} // namespace

std::string_view event_name(EventType type)
{
	const int i = static_cast<int>(type);
	return i < kEventTypeCount ? kEvents[i].name : "";
}

int event_payload_size(EventType type)
{
	const int i = static_cast<int>(type);
	return i < kEventTypeCount ? kEvents[i].payload : 0;
}

void Target::write(uint8_t* out) const
{
	put32(out, static_cast<uint32_t>(id));
	out[4] = rtti;
}

Target Target::read(const uint8_t* in)
{
	return { static_cast<int32_t>(get32(in)), in[4] };
}

void Event::write_record(std::span<uint8_t, kRecordSize> out) const
{
	out[0] = static_cast<uint8_t>(type);
	out[1] = static_cast<uint8_t>((flags & 0xFE) | (executed ? 1 : 0));
	out[2] = static_cast<uint8_t>(house);
	put32(&out[3], frame);
	std::memcpy(&out[7], data.data(), kDataSize);
}

Event Event::read_record(std::span<const uint8_t, kRecordSize> in)
{
	Event e;
	e.type = static_cast<EventType>(in[0]);
	e.executed = (in[1] & 1) != 0;
	e.flags = static_cast<uint8_t>(in[1] & 0xFE);
	e.house = static_cast<int8_t>(in[2]);
	e.frame = get32(&in[3]);
	std::memcpy(e.data.data(), &in[7], kDataSize);
	return e;
}

Event Event::with_target(EventType type, int house, Target target)
{
	Event e;
	e.type = type;
	e.house = static_cast<int8_t>(house);
	target.write(e.data.data());
	return e;
}

Event MegaMission::to_event(int house) const
{
	Event e;
	e.type = EventType::MegaMission;
	e.house = static_cast<int8_t>(house);
	whom.write(&e.data[0]);
	e.data[5] = mission;
	target.write(&e.data[7]);
	destination.write(&e.data[12]);
	follow.write(&e.data[17]);
	e.data[22] = planning ? 1 : 0;
	return e;
}

MegaMission MegaMission::from_event(const Event& e)
{
	MegaMission m;
	m.whom = Target::read(&e.data[0]);
	m.mission = e.data[5];
	m.target = Target::read(&e.data[7]);
	m.destination = Target::read(&e.data[12]);
	m.follow = Target::read(&e.data[17]);
	m.planning = e.data[22] != 0;
	return m;
}

Event FrameInfo::to_event(int house) const
{
	Event e;
	e.type = EventType::FrameInfo;
	e.house = static_cast<int8_t>(house);
	put32(&e.data[0], crc);
	e.data[4] = uint8_t(command_count);
	e.data[5] = uint8_t(command_count >> 8);
	e.data[6] = delay;
	return e;
}

FrameInfo FrameInfo::from_event(const Event& e)
{
	FrameInfo f;
	f.crc = get32(&e.data[0]);
	f.command_count = static_cast<uint16_t>(e.data[4] | (e.data[5] << 8));
	f.delay = e.data[6];
	return f;
}

} // namespace ra::sim
