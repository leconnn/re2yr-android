#pragma once

// The game loop: fixed frames at the GameSpeed rate, events executed through the DoList, then
// LogicClass::Update in the original's order, then Frame++ (P1 section 7.2, P3 section 13).
//
// Only the single-player / skirmish queue (Queue_AI 0x647260) is here; the networked queue comes
// with multiplayer. Each LogicClass::Update step is a hook that the subsystems fill in as they are
// written; a step with no hook does nothing.

#include "core/random.h"
#include "sim/loop/event_queue.h"
#include "sim/loop/frame_crc.h"
#include "sim/loop/recording.h"

#include <array>
#include <cstdint>
#include <functional>

namespace ra::sim {

// Frame timing. The clock ticks every 16 ms (timeGetTime() >> 4).
struct FramePacing {
	static constexpr int kTickMs = 16;
	// Single player and skirmish: GameSpeed ticks per frame (0 = as fast as possible).
	static int ticks_single_player(int game_speed) { return game_speed < 0 ? 0 : game_speed; }
	// Network games: 60 / FPS ticks (integer), or 2 when the FPS is 0.
	static int ticks_network(int fps) { return fps > 0 ? 60 / fps : 2; }
};

// The steps of LogicClass::Update (0x55AFB0), in order.
enum class LogicStep {
	ScenarioTimers,       // trigger springing for time events, mission timer
	ClearChangeFlags,     // VariablesChanged, PickedUpAnyCrate, AmbientChanged
	ShroudRegrowth,       // every ShroudRate minutes
	VisibilityCheck,      // every 120 frames (0x578100)
	Fog,                  // every FogRate minutes
	Ambient,              // ambient light toward its target
	Tiberium,             // growth and spread
	BombsAndKamikaze,
	Teams,                // a copy of the team array, AI() each in index order
	DiskLasers,           // reverse order
	SpotlightsEffectsLightning,
	Radiation,            // reverse order
	LightSourcesAndEmp,
	LogicObjects,         // every object in the logic list, in order
	MoveFlash,            // multiplayer only, cosmetic
	IonBlastsLightsCrates,
	Tactical,
	Factories,
	Houses,
	FollowCamera,
	Count
};

class Logic {
public:
	using Step = std::function<void()>;

	void set_step(LogicStep step, Step fn) { steps_[static_cast<size_t>(step)] = std::move(fn); }

	// One simulation tick: LogicFrame++, then each step in order.
	void update();
	uint32_t logic_frame() const { return logic_frame_; }

private:
	std::array<Step, static_cast<size_t>(LogicStep::Count)> steps_ {};
	uint32_t logic_frame_ = 0;
};

class GameLoop {
public:
	GameLoop(EventSink& sink, ra::Random2& scenario_random) : sink_(&sink), random_(&scenario_random) {}

	Logic& logic() { return logic_; }
	EventQueue& queue() { return queue_; }
	uint32_t frame() const { return frame_; }

	// A local player action (single player): it executes on the current frame (MaxAhead 0).
	void issue(Event event)
	{
		event.frame = EventQueue::execution_frame(frame_, 0, 1);
		queue_.add_local(event);
	}

	// Adds the objects' CRC terms in the original's order (infantry, units, buildings, houses, layers,
	// logic list). The last term, the RNG draw, is added by the loop.
	std::function<void(FrameCrc&)> crc_terms;
	// When set, the frame CRC of every played-back frame is appended here.
	CrcLog* crc_log = nullptr;

	// Recording: every frame's events go to the writer (the original's record bit, 0xA8D5F8 & 1).
	void start_recording(RecordingWriter* writer) { recorder_ = writer; }
	// Playback: events come from the reader instead of the OutList. Each frame then computes the frame
	// CRC, which draws one RNG value, and logs it, which draws another (P3 section 13.5).
	void start_playback(RecordingReader* reader) { player_ = reader; }
	bool playing_back() const { return player_ != nullptr; }

	// Runs one frame: the queue (events of this frame), LogicClass::Update, Frame++.
	// Returns false when a playback has run out of frames or the DoList reported a fatal error.
	bool run_frame();
	EventQueue::Result last_result() const { return last_result_; }

	// Fixed-rate pacing for single player: true when the frame started at 'last_start_ms' has lasted
	// GameSpeed ticks by 'now_ms'. The original never runs extra frames to catch up.
	static bool frame_due(int game_speed, uint64_t last_start_ms, uint64_t now_ms)
	{
		const int ticks = FramePacing::ticks_single_player(game_speed);
		return (now_ms >> 4) - (last_start_ms >> 4) >= static_cast<uint64_t>(ticks);
	}

private:
	EventSink* sink_;
	ra::Random2* random_;
	Logic logic_;
	EventQueue queue_;
	uint32_t frame_ = 0;
	RecordingWriter* recorder_ = nullptr;
	RecordingReader* player_ = nullptr;
	EventQueue::Result last_result_ = EventQueue::Result::Ok;
};

} // namespace ra::sim
