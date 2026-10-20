#pragma once

// SHP sprites, the Tiberian Sun / Red Alert 2 variant (P1 section 6.2).
//
// A file is a canvas size and a list of frames. Each frame covers a sub-rectangle
// of the canvas and is stored raw, per-line, or run-length coded. Pixels are
// palette indices; index 0 is transparent.

#include <cstdint>
#include <span>
#include <vector>

namespace ra::formats {

struct ShpFrame {
	uint16_t x = 0, y = 0, width = 0, height = 0; // sub-rectangle inside the canvas
	uint32_t flags = 0;                           // low byte: 0/1 raw, 2 per-line, 3 run-length
	uint32_t radar_color = 0;                     // 0x00BBGGRR-style colour for the radar
	uint32_t offset = 0;                          // of the pixel data, from the file start; 0 = empty

	bool empty() const { return offset == 0 || width == 0 || height == 0; }
};

class ShpFile {
public:
	// 'data' must outlive the ShpFile: frames are decoded from it on demand.
	static bool parse(std::span<const uint8_t> data, ShpFile& out);

	uint16_t width() const { return width_; }
	uint16_t height() const { return height_; }
	const std::vector<ShpFrame>& frames() const { return frames_; }

	// Decodes frame 'index' into width*height palette indices (0 = transparent).
	// An empty frame decodes to an empty vector. Returns false on malformed data.
	bool decode_frame(size_t index, std::vector<uint8_t>& pixels) const;

private:
	std::span<const uint8_t> data_;
	uint16_t width_ = 0, height_ = 0;
	std::vector<ShpFrame> frames_;
};

} // namespace ra::formats
