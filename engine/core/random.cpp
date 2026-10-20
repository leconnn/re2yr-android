#include "core/random.h"

#include "core/assert.h"

namespace ra {

void Random2::seed(uint32_t seed)
{
	static constexpr uint32_t c1[4] = { 0xBAA96887u, 0x1E17D32Cu, 0x03BCDC3Cu, 0x0F33D1B2u };
	static constexpr uint32_t c2[4] = { 0x4B0F3B58u, 0xE874F0C3u, 0x6955C5A6u, 0x55A7CA46u };

	index1_ = 0;
	index2_ = 103;
	for (uint32_t n = 0; n < kTableSize; ++n) {
		uint32_t lword = seed;
		uint32_t irword = n;
		uint32_t x = 0;
		for (int i = 0; i < 4; ++i) {
			const uint32_t ia = irword ^ c1[i];
			const int32_t hi = static_cast<int32_t>(ia) >> 16;
			const uint32_t lo = ia & 0xFFFFu;
			const uint32_t hi_sq = static_cast<uint32_t>(hi) * static_cast<uint32_t>(hi);
			const uint32_t ib = ~hi_sq + lo * lo;
			// An arithmetic right shift ORed with a left shift, not a rotate.
			const uint32_t t = static_cast<uint32_t>(static_cast<int32_t>(ib) >> 16) | (ib << 16);
			const uint32_t hi_lo = static_cast<uint32_t>(hi) * lo;
			x = ((t ^ c2[i]) + hi_lo) ^ lword;
			lword = irword;
			irword = x;
		}
		table_[n] = x;
	}
	disabled_ = false;
}

uint32_t Random2::next()
{
	if (disabled_)
		return 0;
	const uint32_t value = table_[index1_] ^= table_[index2_];
	if (++index1_ >= kTableSize)
		index1_ = 0;
	if (++index2_ >= kTableSize)
		index2_ = 0;
	return value;
}

int32_t Random2::ranged(int32_t lo, int32_t hi)
{
	if (lo == hi)
		return lo;
	if (hi < lo) {
		const int32_t t = lo;
		lo = hi;
		hi = t;
	}
	const uint32_t range = static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo);

	// The original returns lo + 2^31 without drawing when the range is exactly 2^31 - 1.
	if (range == 0x7FFFFFFFu)
		return static_cast<int32_t>(static_cast<uint32_t>(lo) + 0x80000000u);

	// Any larger range makes the original loop forever, so it never happens in a real game.
	RA_ASSERT_MSG(range < 0x80000000u, "Random2::ranged: hi - lo >= 2^31");
	if (range >= 0x80000000u)
		return lo;

	int bits = 30;
	while (bits > 0 && (range & (1u << bits)) == 0)
		--bits;
	const uint32_t mask = ~(0xFFFFFFFFu << (bits + 1));

	uint32_t value;
	do {
		value = next() & mask;
	} while (value > range);
	return static_cast<int32_t>(static_cast<uint32_t>(lo) + value);
}

void Random2::set_state(int32_t index1, int32_t index2, const std::array<uint32_t, kTableSize>& table)
{
	index1_ = index1;
	index2_ = index2;
	table_ = table;
}

} // namespace ra
