#pragma once

#include <cstddef>
#include <cstdint>

namespace ra::vfs {

// Standard Blowfish (16 rounds) in ECB mode, big-endian words, as used for
// encrypted MIX headers. Only decryption is needed to read archives;
// encryption is provided for tests.
class Blowfish {
public:
	// key_size is 1..56 bytes.
	Blowfish(const uint8_t* key, size_t key_size);

	void encrypt_block(uint8_t block[8]) const;
	void decrypt_block(uint8_t block[8]) const;

private:
	void encrypt_words(uint32_t& left, uint32_t& right) const;
	void decrypt_words(uint32_t& left, uint32_t& right) const;
	uint32_t f(uint32_t x) const;

	uint32_t p_[18];
	uint32_t s_[4][256];
};

} // namespace ra::vfs
