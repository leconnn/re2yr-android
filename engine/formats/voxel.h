#pragma once

// Voxel models: VXL geometry, HVA animation matrices, VPL lighting tables
// (P1 section 6.4).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ra::formats {

struct Voxel {
	uint8_t x, y, z;
	uint8_t color;  // palette index
	uint8_t normal; // index into the normal table for the limb's normal mode
};

struct VxlLimb {
	std::string name;
	uint32_t number = 0;
	uint32_t span_start_offset = 0, span_end_offset = 0, span_data_offset = 0; // within the body
	float scale = 0.0f;
	float transform[12] = {}; // 3x4 matrix, row-major
	float min[3] = {}, max[3] = {};
	uint8_t xsize = 0, ysize = 0, zsize = 0;
	uint8_t normal_mode = 0; // 2 = Tiberian Sun (36 normals), 4 = Red Alert 2 (244 normals)
};

class VxlFile {
public:
	// 'data' must outlive the VxlFile: voxels are decoded from it on demand.
	static bool parse(std::span<const uint8_t> data, VxlFile& out);

	const std::vector<VxlLimb>& limbs() const { return limbs_; }
	uint8_t remap_start() const { return remap_start_; }
	uint8_t remap_end() const { return remap_end_; }
	// The 768-byte palette stored in the file, as written.
	std::span<const uint8_t> embedded_palette() const { return palette_; }

	// Decodes all voxels of limb 'index'. Returns false on malformed data.
	bool decode_limb(size_t index, std::vector<Voxel>& voxels) const;

private:
	std::span<const uint8_t> data_;
	std::span<const uint8_t> palette_;
	size_t body_offset_ = 0;
	size_t body_size_ = 0;
	uint8_t remap_start_ = 0, remap_end_ = 0;
	std::vector<VxlLimb> limbs_;
};

// Animation matrices for a voxel model: frames x sections, each a 3x4 matrix.
class HvaFile {
public:
	static bool parse(std::span<const uint8_t> data, HvaFile& out);

	uint32_t frame_count() const { return frames_; }
	uint32_t section_count() const { return sections_; }
	const std::vector<std::string>& section_names() const { return names_; }
	// The 12 floats (row-major 3x4) of a section in a frame.
	const float* matrix(uint32_t frame, uint32_t section) const { return &matrices_[(size_t(frame) * sections_ + section) * 12]; }

private:
	uint32_t frames_ = 0, sections_ = 0;
	std::vector<std::string> names_;
	std::vector<float> matrices_;
};

// Voxel lighting: for each of section_count light levels, a 256-entry table
// remapping a palette index to a lit one.
struct VplFile {
	uint32_t remap_start = 0, remap_end = 0, section_count = 0;
	std::vector<uint8_t> palette;     // 768 bytes, as written
	std::vector<uint8_t> lookup;      // section_count * 256 entries

	static bool parse(std::span<const uint8_t> data, VplFile& out);
	const uint8_t* table(uint32_t section) const { return &lookup[size_t(section) * 256]; }
};

} // namespace ra::formats
