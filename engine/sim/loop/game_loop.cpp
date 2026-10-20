#include "sim/loop/game_loop.h"

namespace ra::sim {

void Logic::update()
{
	++logic_frame_;
	for (const Step& step : steps_)
		if (step)
			step();
}

bool GameLoop::run_frame()
{
	if (player_) {
		// Playback (Queue_AI 0x647260 with the playback bit): the frame CRC first (one RNG draw), then
		// LogFrameCRC (another draw), then this frame's events from the file.
		FrameCrc crc;
		if (crc_terms)
			crc_terms(crc);
		crc.finish(*random_);
		queue_.set_frame_crc(frame_, crc.value());
		random_->next(); // LogFrameCRC 0x650A90
		if (crc_log)
			crc_log->add(frame_, crc.value());
		auto events = player_->next_frame();
		if (!events)
			return false;
		for (Event e : *events) {
			e.executed = false;
			queue_.add(e);
		}
	} else {
		queue_.flush_out_list();
		if (recorder_) {
			const auto events = queue_.events_for_frame(frame_);
			recorder_->add_frame(events);
		}
	}

	last_result_ = queue_.execute(frame_, *sink_);
	queue_.purge(frame_);
	if (last_result_ == EventQueue::Result::TooLate)
		return false;

	logic_.update();
	++frame_;
	return true;
}

} // namespace ra::sim
