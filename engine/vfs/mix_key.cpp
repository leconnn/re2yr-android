#include "vfs/mix_key.h"

#include "core/assert.h"
#include "core/base64.h"

#include <array>
#include <cstring>
#include <string_view>
#include <vector>

namespace ra::vfs {

namespace {

// A small unsigned big integer: little-endian 32-bit limbs, 20 limbs = 640 bits.
// Enough for the product of two 320-bit values. Speed doesn't matter here:
// a header needs two modular exponentiations.
constexpr int kLimbs = 20;
using Big = std::array<uint32_t, kLimbs>;

Big from_le_bytes(const uint8_t* bytes, int size)
{
	Big b {};
	RA_ASSERT(size <= kLimbs * 4);
	for (int i = 0; i < size; ++i)
		b[i / 4] |= uint32_t(bytes[i]) << (8 * (i % 4));
	return b;
}

void to_le_bytes(const Big& b, uint8_t* out, int size)
{
	for (int i = 0; i < size; ++i)
		out[i] = i < kLimbs * 4 ? static_cast<uint8_t>(b[i / 4] >> (8 * (i % 4))) : 0;
}

int compare(const Big& a, const Big& b)
{
	for (int i = kLimbs - 1; i >= 0; --i) {
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	}
	return 0;
}

void subtract(Big& a, const Big& b)
{
	int64_t borrow = 0;
	for (int i = 0; i < kLimbs; ++i) {
		int64_t v = int64_t(a[i]) - int64_t(b[i]) - borrow;
		borrow = v < 0 ? 1 : 0;
		a[i] = static_cast<uint32_t>(v & 0xFFFFFFFF);
	}
}

// Returns the bit shifted out of the top.
uint32_t shift_left_one(Big& a, uint32_t low_bit)
{
	uint32_t carry = low_bit;
	for (int i = 0; i < kLimbs; ++i) {
		const uint32_t next = a[i] >> 31;
		a[i] = (a[i] << 1) | carry;
		carry = next;
	}
	return carry;
}

// (a * b) mod n by schoolbook multiplication then bitwise long division.
Big mod_mul(const Big& a, const Big& b, const Big& n)
{
	std::array<uint64_t, kLimbs * 2> product {};
	for (int i = 0; i < kLimbs / 2; ++i) { // inputs are below n < 2^320
		uint64_t carry = 0;
		for (int j = 0; j < kLimbs / 2; ++j) {
			const uint64_t cur = product[i + j] + uint64_t(a[i]) * b[j] + carry;
			product[i + j] = cur & 0xFFFFFFFFu;
			carry = cur >> 32;
		}
		product[i + kLimbs / 2] += carry;
	}
	Big r {};
	for (int bit = kLimbs * 32 - 1; bit >= 0; --bit) {
		const uint32_t in = static_cast<uint32_t>((product[bit / 32] >> (bit % 32)) & 1u);
		const uint32_t overflow = shift_left_one(r, in);
		if (overflow || compare(r, n) >= 0)
			subtract(r, n);
	}
	return r;
}

// The key is held as a base64 string in the original executable and in INI files.
constexpr std::string_view kPublicKeyBase64 = "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V";

struct PublicKey {
	std::array<uint8_t, 64> modulus_le {};
	int modulus_size = 0; // bytes holding the modulus
	int block_in = 0;     // ciphertext bytes per block
	int block_out = 0;    // plaintext bytes per block
};

const PublicKey& public_key()
{
	static const PublicKey key = [] {
		PublicKey k;
		std::vector<uint8_t> raw;
		base64_decode(kPublicKeyBase64, raw);
		// 02 <length> <big-endian modulus>
		RA_ASSERT(raw.size() >= 2 && raw[0] == 2 && size_t(raw[1]) + 2 <= raw.size() && raw[1] <= 64);
		const int n = raw[1];
		for (int i = 0; i < n; ++i)
			k.modulus_le[i] = raw[2 + n - 1 - i];
		k.modulus_size = n;
		int bit_length = 0;
		for (int i = n - 1; i >= 0; --i) {
			if (k.modulus_le[i]) {
				bit_length = i * 8;
				for (uint8_t v = k.modulus_le[i]; v; v >>= 1)
					++bit_length;
				break;
			}
		}
		k.block_out = (bit_length - 1) / 8; // 39
		k.block_in = k.block_out + 1;       // 40
		return k;
	}();
	return key;
}

} // namespace

void mod_pow_le(const uint8_t* base, int base_size, uint32_t exponent, const uint8_t* modulus, int modulus_size, uint8_t* out, int out_size)
{
	const Big n = from_le_bytes(modulus, modulus_size);
	Big b = from_le_bytes(base, base_size);
	// Reduce the base first.
	{
		Big one {};
		one[0] = 1;
		b = mod_mul(b, one, n);
	}
	Big result {};
	result[0] = 1;
	for (int bit = 31; bit >= 0; --bit) {
		result = mod_mul(result, result, n);
		if ((exponent >> bit) & 1u)
			result = mod_mul(result, b, n);
	}
	to_le_bytes(result, out, out_size);
}

void mix_blowfish_key(const uint8_t key_source[80], uint8_t key_out[56])
{
	const PublicKey& key = public_key();
	std::array<uint8_t, 128> plain {};
	int produced = 0;
	for (int offset = 0; offset + key.block_in <= 80; offset += key.block_in) {
		uint8_t block[64] = {};
		mod_pow_le(key_source + offset, key.block_in, 65537, key.modulus_le.data(), key.modulus_size, block, key.block_out);
		std::memcpy(plain.data() + produced, block, key.block_out);
		produced += key.block_out;
	}
	RA_ASSERT(produced >= 56);
	std::memcpy(key_out, plain.data(), 56);
}

} // namespace ra::vfs
