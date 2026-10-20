#pragma once

// Facings.
//
// A facing is a 16-bit binary angle: 0x0000 is north, 0x4000 east,
// 0x8000 south, 0xC000 west. Converting to N facings (for sprites, or the
// 8 compass directions) rounds to the nearest step, as the original does.

#include <bit>
#include <cstdint>

namespace ra {

class Dir16 {
public:
	constexpr Dir16() = default;
	constexpr explicit Dir16(uint16_t raw) : raw_(raw) {}

	constexpr uint16_t raw() const { return raw_; }
	constexpr void set_raw(uint16_t raw) { raw_ = raw; }

	// The facing as a value with the given number of bits, rounded to nearest.
	template <int Bits>
	constexpr uint32_t value(uint32_t offset = 0) const
	{
		static_assert(Bits >= 1 && Bits <= 16);
		if constexpr (Bits == 16)
			return (raw_ + offset) & 0xFFFFu;
		else
			return ((((uint32_t(raw_) >> (16 - Bits - 1)) + 1) >> 1) + offset) & ((1u << Bits) - 1);
	}

	template <int Bits>
	constexpr void set_value(uint32_t value, uint32_t offset = 0)
	{
		static_assert(Bits >= 1 && Bits <= 16);
		raw_ = static_cast<uint16_t>(((value - offset) & ((1u << Bits) - 1)) << (16 - Bits));
	}

	// The facing as one of Count steps (Count a power of two, e.g. 8 or 32).
	template <uint32_t Count>
	constexpr uint32_t facing(uint32_t offset = 0) const
	{
		static_assert(std::has_single_bit(Count) && Count >= 2 && Count <= 65536);
		return value<std::bit_width(Count - 1)>(offset);
	}

	template <uint32_t Count>
	constexpr void set_facing(uint32_t step, uint32_t offset = 0)
	{
		static_assert(std::has_single_bit(Count) && Count >= 2 && Count <= 65536);
		set_value<std::bit_width(Count - 1)>(step, offset);
	}

	// The 8-bit facing used by the sync checksum and many game rules.
	constexpr uint8_t byte() const { return static_cast<uint8_t>(value<8>()); }

	friend constexpr bool operator==(Dir16 a, Dir16 b) { return a.raw_ == b.raw_; }
	friend constexpr bool operator!=(Dir16 a, Dir16 b) { return a.raw_ != b.raw_; }

private:
	uint16_t raw_ = 0;
};

} // namespace ra
