#pragma once

// The game's random number generators.
//
// Random2 is the synchronised generator that drives all game logic. It must
// produce exactly the original's sequence, and every call must happen in the
// original order, or multiplayer games and replays go out of sync.
// It is an R250 shift-register generator seeded with a psdes-style hash.

#include <array>
#include <cstdint>

namespace ra {

class Random2 {
public:
	static constexpr int kTableSize = 250;

	Random2() = default;
	explicit Random2(uint32_t seed) { this->seed(seed); }

	void seed(uint32_t seed);

	// Next 32-bit value, or 0 while disabled.
	uint32_t next();

	// A value in [lo, hi] (the bounds may be given in either order).
	// hi - lo must be below 2^31: the original never returns otherwise.
	int32_t ranged(int32_t lo, int32_t hi);

	bool disabled() const { return disabled_; }
	void set_disabled(bool disabled) { disabled_ = disabled; }

	// State access for saves and checksums.
	int32_t index1() const { return index1_; }
	int32_t index2() const { return index2_; }
	const std::array<uint32_t, kTableSize>& table() const { return table_; }
	void set_state(int32_t index1, int32_t index2, const std::array<uint32_t, kTableSize>& table);

private:
	bool disabled_ = false;
	int32_t index1_ = 0;
	int32_t index2_ = 103;
	std::array<uint32_t, kTableSize> table_ {};
};

} // namespace ra
