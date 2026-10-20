#include "vfs/mix_id.h"

#include <array>
#include <string>

namespace ra::vfs {

namespace {

constexpr std::array<uint32_t, 256> make_table()
{
	std::array<uint32_t, 256> t {};
	for (uint32_t i = 0; i < 256; ++i) {
		uint32_t c = i;
		for (int k = 0; k < 8; ++k)
			c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
		t[i] = c;
	}
	return t;
}

constexpr auto kTable = make_table();

} // namespace

uint32_t crc32(const uint8_t* data, size_t size)
{
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < size; ++i)
		c = kTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

uint32_t mix_id(std::string_view name)
{
	std::string s(name);
	for (char& ch : s) {
		if (ch >= 'a' && ch <= 'z')
			ch = static_cast<char>(ch - 'a' + 'A');
	}
	const size_t len = s.size();
	const size_t whole = len >> 2;
	if (len & 3) {
		s.push_back(static_cast<char>(len - (whole << 2)));
		const char pad = s[whole << 2];
		for (size_t i = 0; i < 3 - (len & 3); ++i)
			s.push_back(pad);
	}
	return crc32(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

} // namespace ra::vfs
