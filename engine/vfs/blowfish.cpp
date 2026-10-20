#include "vfs/blowfish.h"

#include <cstring>

namespace ra::vfs {

namespace {

#include "vfs/blowfish_tables.inc"

uint32_t load_be(const uint8_t* p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void store_be(uint8_t* p, uint32_t v)
{
	p[0] = static_cast<uint8_t>(v >> 24);
	p[1] = static_cast<uint8_t>(v >> 16);
	p[2] = static_cast<uint8_t>(v >> 8);
	p[3] = static_cast<uint8_t>(v);
}

} // namespace

uint32_t Blowfish::f(uint32_t x) const
{
	return ((s_[0][x >> 24] + s_[1][(x >> 16) & 0xFF]) ^ s_[2][(x >> 8) & 0xFF]) + s_[3][x & 0xFF];
}

Blowfish::Blowfish(const uint8_t* key, size_t key_size)
{
	std::memcpy(p_, kInitP, sizeof p_);
	std::memcpy(s_[0], kInitS0, sizeof s_[0]);
	std::memcpy(s_[1], kInitS1, sizeof s_[1]);
	std::memcpy(s_[2], kInitS2, sizeof s_[2]);
	std::memcpy(s_[3], kInitS3, sizeof s_[3]);

	size_t k = 0;
	for (uint32_t& p : p_) {
		uint32_t word = 0;
		for (int i = 0; i < 4; ++i) {
			word = (word << 8) | key[k];
			k = (k + 1) % key_size;
		}
		p ^= word;
	}

	uint32_t left = 0, right = 0;
	for (int i = 0; i < 18; i += 2) {
		encrypt_words(left, right);
		p_[i] = left;
		p_[i + 1] = right;
	}
	for (auto& box : s_) {
		for (int i = 0; i < 256; i += 2) {
			encrypt_words(left, right);
			box[i] = left;
			box[i + 1] = right;
		}
	}
}

void Blowfish::encrypt_words(uint32_t& left, uint32_t& right) const
{
	uint32_t l = left, r = right;
	for (int i = 0; i < 16; i += 2) {
		l ^= p_[i];
		r ^= f(l);
		r ^= p_[i + 1];
		l ^= f(r);
	}
	l ^= p_[16];
	r ^= p_[17];
	left = r;
	right = l;
}

void Blowfish::decrypt_words(uint32_t& left, uint32_t& right) const
{
	uint32_t l = left, r = right;
	for (int i = 17; i > 1; i -= 2) {
		l ^= p_[i];
		r ^= f(l);
		r ^= p_[i - 1];
		l ^= f(r);
	}
	l ^= p_[1];
	r ^= p_[0];
	left = r;
	right = l;
}

void Blowfish::encrypt_block(uint8_t block[8]) const
{
	uint32_t l = load_be(block), r = load_be(block + 4);
	encrypt_words(l, r);
	store_be(block, l);
	store_be(block + 4, r);
}

void Blowfish::decrypt_block(uint8_t block[8]) const
{
	uint32_t l = load_be(block), r = load_be(block + 4);
	decrypt_words(l, r);
	store_be(block, l);
	store_be(block + 4, r);
}

} // namespace ra::vfs
