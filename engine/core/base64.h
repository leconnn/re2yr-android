#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace ra {

// Decodes standard base64 (A-Z a-z 0-9 + /, '=' padding). Characters outside
// the alphabet (such as line breaks) are skipped. Returns false on a malformed tail.
bool base64_decode(std::string_view text, std::vector<uint8_t>& out);

} // namespace ra
