#include "core/base64.h"

namespace ra {

bool base64_decode(std::string_view text, std::vector<uint8_t>& out)
{
	out.clear();
	out.reserve(text.size() / 4 * 3);
	uint32_t acc = 0;
	int bits = 0;
	for (char c : text) {
		int v;
		if (c >= 'A' && c <= 'Z')
			v = c - 'A';
		else if (c >= 'a' && c <= 'z')
			v = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			v = c - '0' + 52;
		else if (c == '+')
			v = 62;
		else if (c == '/')
			v = 63;
		else if (c == '=')
			break;
		else
			continue;
		acc = (acc << 6) | static_cast<uint32_t>(v);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
		}
	}
	return true;
}

} // namespace ra
