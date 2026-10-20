#include "sim/loop/event_queue.h"

namespace ra::sim {

uint32_t EventQueue::execution_frame(uint32_t frame, int max_ahead, int frame_send_rate)
{
	if (frame_send_rate <= 1)
		return frame + static_cast<uint32_t>(max_ahead);
	const uint32_t fsr = static_cast<uint32_t>(frame_send_rate);
	return ((fsr + frame - 1 + static_cast<uint32_t>(max_ahead)) / fsr) * fsr;
}

bool EventQueue::add_local(const Event& event)
{
	if (out_.size() >= kOutListSize)
		return false;
	out_.push_back(event);
	return true;
}

bool EventQueue::add(const Event& event)
{
	if (count_ >= kDoListSize)
		return false;
	ring_[(head_ + count_) & (kDoListSize - 1)] = event;
	++count_;
	return true;
}

void EventQueue::flush_out_list()
{
	for (Event e : out_) {
		e.executed = false;
		add(e);
	}
	out_.clear();
}

std::vector<Event> EventQueue::events_for_frame(uint32_t frame) const
{
	std::vector<Event> out;
	for (size_t i = 0; i < count_; ++i) {
		const Event& e = at(i);
		if (e.frame == frame && !e.executed)
			out.push_back(e);
	}
	return out;
}

EventQueue::Result EventQueue::execute(uint32_t frame, EventSink& sink)
{
	Result result = Result::Ok;
	const int houses = sink.house_count();
	for (int h = 0; h < houses; ++h) {
		if (!sink.house_is_human(h))
			continue;
		std::vector<Event*> megamissions;
		for (size_t i = 0; i < count_; ++i) {
			Event& e = at(i);
			if (e.executed || e.house != h || e.frame > frame)
				continue;
			if (e.frame < frame && e.type != EventType::FrameInfo) {
				// "Packet received too late": fatal for that connection.
				e.executed = true;
				result = Result::TooLate;
				continue;
			}
			switch (e.type) {
			case EventType::FrameInfo: {
				const FrameInfo info = FrameInfo::from_event(e);
				if (frame_crc(e.frame - info.delay) != info.crc) {
					sink.out_of_sync(e.frame);
					if (result == Result::Ok)
						result = Result::OutOfSync;
				}
				break;
			}
			case EventType::Exit:
				sink.exit(e);
				break;
			case EventType::Options:
			case EventType::PageUser:
				break; // user interface only
			case EventType::MegaMission:
				megamissions.push_back(&e);
				break;
			default:
				sink.execute(e);
				break;
			}
			e.executed = true;
		}

		// The house's megamissions run after its other events. Runs of identical Area Guard orders are
		// first spread over nearby cells.
		for (size_t i = 0; i < megamissions.size();) {
			const MegaMission first = MegaMission::from_event(*megamissions[i]);
			size_t j = i + 1;
			while (j < megamissions.size()) {
				const MegaMission m = MegaMission::from_event(*megamissions[j]);
				if (m.mission != first.mission || m.target != first.target || m.destination != first.destination)
					break;
				++j;
			}
			if (first.mission == kMissionAreaGuard && j - i >= 2) {
				std::vector<Event*> run(megamissions.begin() + static_cast<long>(i), megamissions.begin() + static_cast<long>(j));
				sink.spread_area_guard(run);
			}
			for (size_t k = i; k < j; ++k)
				sink.execute(*megamissions[k]);
			i = j;
		}
	}
	return result;
}

void EventQueue::purge(uint32_t frame)
{
	while (count_ > 0) {
		const Event& e = at(0);
		if (!e.executed && e.frame >= frame)
			break;
		head_ = (head_ + 1) & (kDoListSize - 1);
		--count_;
	}
}

} // namespace ra::sim
