#pragma once

#include <cstdint>

namespace ra::vfs {

// Recovers the 56-byte Blowfish key from the 80-byte "key source" stored in an
// encrypted MIX header (P1 section 5.4). The key source is two RSA blocks of
// 40 bytes; Westwood's 319-bit public key (exponent 65537) turns each into
// 39 plaintext bytes.
void mix_blowfish_key(const uint8_t key_source[80], uint8_t key_out[56]);

// Plain modular exponentiation for little-endian byte strings, exposed for
// tests: returns (base ^ exponent) mod modulus as 'out_size' little-endian bytes.
// All values must fit in 80 bytes (640 bits).
void mod_pow_le(const uint8_t* base, int base_size, uint32_t exponent, const uint8_t* modulus, int modulus_size, uint8_t* out, int out_size);

} // namespace ra::vfs
