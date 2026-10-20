#pragma once

// The original's frame checksum (ComputeFrameCRC 0x64DAB0, P3 section 13.3).
//
// One accumulator, rotated left by one before each term is added. The objects add their terms in a
// fixed order (infantry, units, buildings, houses, display layers, logic list); the last term is a
// value drawn from the synchronised RNG, so computing the CRC advances it. The simulation calls this
// exactly where the original does: every networked frame, and every frame of a playback.

#include "core/random.h"

#include <cstdint>

namespace ra::sim {

class FrameCrc {
public:
	void add(uint32_t term) { crc_ = ((crc_ << 1) | (crc_ >> 31)) + term; }

	// The position term of an object: X / 10 + (Y / 10) << 16 + extra, with X and Y in leptons and the
	// divisions truncating toward zero. 'extra' is the facing byte, both facings of a unit, or WhatAmI.
	static uint32_t position_term(int32_t x, int32_t y, uint32_t extra)
	{
		return static_cast<uint32_t>(x / 10) + (static_cast<uint32_t>(y / 10) << 16) + extra;
	}
	void add_position(int32_t x, int32_t y, uint32_t extra) { add(position_term(x, y, extra)); }

	// The facing byte the CRC uses: (raw16 >> 7 + 1) >> 1, i.e. the facing rounded to 256 steps.
	static uint32_t facing_byte(uint16_t raw16) { return ((uint32_t(raw16) >> 7) + 1) >> 1; }

	// The final term: one draw from Scenario.Random.
	void finish(ra::Random2& scenario_random) { add(scenario_random.next()); }

	uint32_t value() const { return crc_; }
	void reset(uint32_t value = 0) { crc_ = value; }

private:
	uint32_t crc_ = 0;
};

} // namespace ra::sim
