#pragma once

// Events: every player action is one of these, executed on a given frame (P3 section 4.1, 13.4).
//
// In memory an event is 111 bytes: u8 type, u8 executed, s8 house, u32 frame, u8 data[104].
// The same record is what the original's recorder writes to RECORD.BIN, so records here are
// read and written in exactly that byte layout.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace ra::sim {

enum class EventType : uint8_t {
	Empty = 0,
	PowerOn, PowerOff, Ally, MegaMission, MegaMissionF, Idle, Scatter, Destruct, Deploy, Detonate,
	Place, Options, GameSpeed, Produce, Suspend, Abandon, Primary, SpecialPlace, Exit, Animation,
	Repair, Sell, SellCell, Special, FrameSync, Message, ResponseTime, FrameInfo, SaveGame, Archive,
	AddPlayer, Timing, ProcessTime, PageUser, RemovePlayer, LatencyFudge, MegaFrameInfo, PacketTiming,
	AboutToExit, FallbackHost, AddressChange, PlanConnect, PlanCommit, PlanNodeDelete, AllCheer, AbandonAll,
};
inline constexpr int kEventTypeCount = 47;

// The name and payload size of an event type (the table at 0x8208EC). Unknown types give ("", 0).
std::string_view event_name(EventType type);
int event_payload_size(EventType type);

// A target as events carry it: { int32 id; u8 rtti }, 5 bytes packed (TargetClass 0x6E6AB0).
struct Target {
	static constexpr uint8_t kRttiNone = 0x00;
	static constexpr uint8_t kRttiCell = 0x0B;
	static constexpr uint8_t kRttiObject = 0x34;

	int32_t id = 0;
	uint8_t rtti = kRttiNone;

	static Target none() { return {}; }
	static Target cell(int x, int y) { return { x + 1000 * y, kRttiCell }; }
	static Target object(int32_t unique_id) { return { unique_id, kRttiObject }; }

	bool is_none() const { return rtti == kRttiNone && id == 0; }
	bool is_cell() const { return rtti == kRttiCell; }
	int cell_x() const { return id % 1000; }
	int cell_y() const { return id / 1000; }

	void write(uint8_t* out) const;
	static Target read(const uint8_t* in);

	friend bool operator==(const Target&, const Target&) = default;
};

struct Event {
	static constexpr int kRecordSize = 0x6F; // 111
	static constexpr int kDataSize = 104;

	EventType type = EventType::Empty;
	bool executed = false;
	int8_t house = -1;
	uint32_t frame = 0;
	std::array<uint8_t, kDataSize> data {};

	// The 111-byte record (little endian, packed, as in memory and in RECORD.BIN). Bit 0 of the second
	// byte is the executed flag; the original keeps other flags in that byte, which are preserved.
	void write_record(std::span<uint8_t, kRecordSize> out) const;
	static Event read_record(std::span<const uint8_t, kRecordSize> in);

	// Helpers for the common payloads.
	static Event with_target(EventType type, int house, Target target); // POWERON, IDLE, SCATTER, DEPLOY, SELL...
	Target target() const { return Target::read(data.data()); }

	uint8_t flags = 0; // the second byte, minus the executed bit

	friend bool operator==(const Event&, const Event&) = default;
};

// MEGAMISSION (4): Whom, mission, pad, Target, Destination, Follow, planning (23 bytes).
struct MegaMission {
	Target whom;
	uint8_t mission = 0;
	Target target;
	Target destination;
	Target follow;
	bool planning = false;

	Event to_event(int house) const;
	static MegaMission from_event(const Event& e);
};

// FRAMEINFO (28): u32 CRC, u16 command count, u8 delay.
struct FrameInfo {
	uint32_t crc = 0;
	uint16_t command_count = 0;
	uint8_t delay = 0;

	Event to_event(int house) const;
	static FrameInfo from_event(const Event& e);
};

} // namespace ra::sim
