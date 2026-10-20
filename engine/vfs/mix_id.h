#pragma once

#include <cstdint>
#include <string_view>

namespace ra::vfs {

// CRC-32 (IEEE polynomial) of a byte range.
uint32_t crc32(const uint8_t* data, size_t size);

// The id a name has inside a MIX archive (P1 section 5.3): CRC-32 of the
// upper-cased name, padded to a multiple of four bytes the Westwood way.
uint32_t mix_id(std::string_view name);

} // namespace ra::vfs
