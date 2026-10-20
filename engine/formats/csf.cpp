#include "formats/csf.h"

#include <cstring>

namespace ra::formats {

namespace {

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

std::string lower(std::string_view s)
{
	std::string r(s);
	for (char& c : r) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return r;
}

} // namespace

bool CsfFile::parse(std::span<const uint8_t> data, CsfFile& out)
{
	if (data.size() < 24 || std::memcmp(data.data(), " FSC", 4) != 0)
		return false;
	out.version_ = rd32(data.data() + 4);
	const uint32_t label_count = rd32(data.data() + 8);
	out.language_ = rd32(data.data() + 20);
	out.labels_.clear();
	out.labels_.reserve(label_count);

	size_t p = 24;
	auto have = [&](size_t n) { return p + n <= data.size(); };
	for (uint32_t i = 0; i < label_count; ++i) {
		if (!have(12) || std::memcmp(data.data() + p, " LBL", 4) != 0)
			return false;
		const uint32_t pairs = rd32(data.data() + p + 4);
		const uint32_t name_len = rd32(data.data() + p + 8);
		p += 12;
		if (!have(name_len))
			return false;
		const std::string name(reinterpret_cast<const char*>(data.data() + p), name_len);
		p += name_len;

		CsfString first;
		for (uint32_t k = 0; k < pairs; ++k) {
			if (!have(8))
				return false;
			const bool wide = std::memcmp(data.data() + p, "WRTS", 4) == 0;
			if (!wide && std::memcmp(data.data() + p, " RTS", 4) != 0)
				return false;
			const uint32_t len = rd32(data.data() + p + 4);
			p += 8;
			if (!have(size_t(len) * 2))
				return false;
			CsfString s;
			s.text.resize(len);
			for (uint32_t c = 0; c < len; ++c) {
				// Each byte is stored inverted.
				const uint8_t lo = static_cast<uint8_t>(~data[p + c * 2]);
				const uint8_t hi = static_cast<uint8_t>(~data[p + c * 2 + 1]);
				s.text[c] = static_cast<char16_t>(lo | (hi << 8));
			}
			p += size_t(len) * 2;
			if (wide) {
				if (!have(4))
					return false;
				const uint32_t extra_len = rd32(data.data() + p);
				p += 4;
				if (!have(extra_len))
					return false;
				s.extra.assign(reinterpret_cast<const char*>(data.data() + p), extra_len);
				p += extra_len;
			}
			if (k == 0)
				first = std::move(s);
		}
		out.labels_.emplace(lower(name), std::move(first));
	}
	return p == data.size();
}

const CsfString* CsfFile::find(std::string_view label) const
{
	auto it = labels_.find(lower(label));
	return it == labels_.end() ? nullptr : &it->second;
}

std::string CsfFile::text_or_label(std::string_view label) const
{
	if (const CsfString* s = find(label))
		return utf16_to_utf8(s->text);
	return std::string(label);
}

std::string utf16_to_utf8(std::u16string_view text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); ++i) {
		uint32_t c = text[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + (text[i + 1] - 0xDC00);
			++i;
		}
		if (c < 0x80) {
			out.push_back(static_cast<char>(c));
		} else if (c < 0x800) {
			out.push_back(static_cast<char>(0xC0 | (c >> 6)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		} else if (c < 0x10000) {
			out.push_back(static_cast<char>(0xE0 | (c >> 12)));
			out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		} else {
			out.push_back(static_cast<char>(0xF0 | (c >> 18)));
			out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
	}
	return out;
}

} // namespace ra::formats
