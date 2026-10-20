#pragma once

// The event queues and the order in which events execute (P3 sections 4.2, 13.1, 13.2).
//
// Local actions go into the OutList; the DoList holds every event (local and received) until its
// frame comes. Execute_DoList runs the events of the current frame house by house, in index order,
// with each house's megamissions after its other events. The queue decides order and timing only;
// what an event does is up to the EventSink (the simulation).

#include "sim/loop/event.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ra::sim {

class EventSink {
public:
	virtual ~EventSink() = default;

	virtual int house_count() const = 0;
	// Only human houses' events are executed (AI houses issue none).
	virtual bool house_is_human(int house) const = 0;
	// EventClass::Execute (0x4C6CB0).
	virtual void execute(const Event& event) = 0;
	// EXIT (19): the house leaves. By default it goes to execute().
	virtual void exit(const Event& event) { execute(event); }
	// Runs of two or more identical Area Guard megamissions are spread over nearby cells before they
	// execute (0x64CDA0); 'run' points into the house's megamission list. Default: nothing.
	virtual void spread_area_guard(std::vector<Event*>& run) { (void)run; }
	// A FRAMEINFO event's CRC didn't match ours: the game is out of sync.
	virtual void out_of_sync(uint32_t frame) { (void)frame; }
};

class EventQueue {
public:
	static constexpr int kDoListSize = 0x4000; // 16,384 entries, a ring
	static constexpr int kOutListSize = 0x4000;
	// The mission number of Area Guard, for the megamission spreading rule.
	static constexpr uint8_t kMissionAreaGuard = 11;

	// The frame a locally issued event executes on (0x64B660): Frame + MaxAhead, rounded up to a
	// multiple of FrameSendRate (no rounding when the rate is 0 or 1).
	static uint32_t execution_frame(uint32_t frame, int max_ahead, int frame_send_rate);

	// A local action: queued for sending (multiplayer) or for this frame (single player).
	bool add_local(const Event& event);
	// An event for the DoList (local after sending, or received). False when the list is full, in
	// which case the original drops the event.
	bool add(const Event& event);
	// Single player (Queue_AI 0x647260): every OutList event goes to the DoList with its executed flag cleared.
	void flush_out_list();

	// The events the recorder writes for a frame (0x64D9E0): not executed, scheduled for exactly that frame,
	// in queue order.
	std::vector<Event> events_for_frame(uint32_t frame) const;

	enum class Result { Ok, TooLate, OutOfSync };
	// Execute_DoList (0x64C380) for frame 'frame'.
	Result execute(uint32_t frame, EventSink& sink);
	// Drops executed and stale events from the front of the DoList (0x64D990): the ring only shrinks from
	// its oldest end, and stops at the first event that is still waiting.
	void purge(uint32_t frame);

	// LatestFramesCRC (0xB04474): the frame CRC of each of the last 256 frames.
	void set_frame_crc(uint32_t frame, uint32_t crc) { frame_crcs_[frame & 0xFF] = crc; }
	uint32_t frame_crc(uint32_t frame) const { return frame_crcs_[frame & 0xFF]; }

	size_t pending() const { return count_; }
	size_t out_pending() const { return out_.size(); }

private:
	Event& at(size_t i) { return ring_[(head_ + i) & (kDoListSize - 1)]; }
	const Event& at(size_t i) const { return ring_[(head_ + i) & (kDoListSize - 1)]; }

	std::vector<Event> ring_ = std::vector<Event>(kDoListSize);
	size_t head_ = 0, count_ = 0;
	std::vector<Event> out_;
	std::array<uint32_t, 256> frame_crcs_ {};
};

} // namespace ra::sim
