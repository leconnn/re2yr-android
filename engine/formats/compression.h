#pragma once

// Decompressors for the data packed inside maps and archives.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ra::formats {

// Westwood LCW ("Format80"). Appends to 'out'. Stops at the 0x80 end marker or
// at the end of the input; returns false if the data is malformed or would
// produce more than max_output bytes.
bool lcw_decompress(std::span<const uint8_t> in, std::vector<uint8_t>& out, size_t max_output = 64u * 1024 * 1024);

// LZO1X. Appends to 'out'. Returns false if the data is malformed or would
// produce more than max_output bytes.
bool lzo1x_decompress(std::span<const uint8_t> in, std::vector<uint8_t>& out, size_t max_output = 64u * 1024 * 1024);

// Map sections hold a sequence of chunks, each { u16 compressed size, u16
// decompressed size, data }, compressed with LZO (IsoMapPack5, PreviewPack)
// or LCW (OverlayPack, OverlayDataPack). Concatenates the decompressed chunks.
enum class ChunkCodec { Lzo, Lcw };
bool decode_chunks(std::span<const uint8_t> in, ChunkCodec codec, std::vector<uint8_t>& out);

} // namespace ra::formats
