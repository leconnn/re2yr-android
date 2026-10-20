#include "formats/voxel.h"

#include <cstring>

namespace ra::formats {

namespace {

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

float rdf(const uint8_t* p)
{
	const uint32_t v = rd32(p);
	float f;
	std::memcpy(&f, &v, 4);
	return f;
}

constexpr size_t kHeaderSize = 32 + 2 + 768; // 802
constexpr size_t kLimbHeaderSize = 28;
constexpr size_t kLimbTailerSize = 92;

} // namespace

bool VxlFile::parse(std::span<const uint8_t> data, VxlFile& out)
{
	static constexpr char magic[16] = { 'V', 'o', 'x', 'e', 'l', ' ', 'A', 'n', 'i', 'm', 'a', 't', 'i', 'o', 'n', 0 };
	if (data.size() < kHeaderSize || std::memcmp(data.data(), magic, 16) != 0)
		return false;
	const uint32_t limb_count = rd32(data.data() + 20);
	const uint32_t tailer_count = rd32(data.data() + 24);
	const uint32_t body_size = rd32(data.data() + 28);
	if (limb_count == 0 || limb_count != tailer_count || limb_count > 64)
		return false;

	const size_t body = kHeaderSize + size_t(limb_count) * kLimbHeaderSize;
	const size_t tailers = body + body_size;
	if (tailers + size_t(limb_count) * kLimbTailerSize != data.size())
		return false;

	out.data_ = data;
	out.remap_start_ = data[32];
	out.remap_end_ = data[33];
	out.palette_ = data.subspan(34, 768);
	out.body_offset_ = body;
	out.body_size_ = body_size;
	out.limbs_.clear();
	for (uint32_t i = 0; i < limb_count; ++i) {
		const uint8_t* h = data.data() + kHeaderSize + size_t(i) * kLimbHeaderSize;
		const uint8_t* t = data.data() + tailers + size_t(i) * kLimbTailerSize;
		VxlLimb limb;
		limb.name.assign(reinterpret_cast<const char*>(h), strnlen(reinterpret_cast<const char*>(h), 16));
		limb.number = rd32(h + 16);
		limb.span_start_offset = rd32(t);
		limb.span_end_offset = rd32(t + 4);
		limb.span_data_offset = rd32(t + 8);
		limb.scale = rdf(t + 12);
		for (int k = 0; k < 12; ++k)
			limb.transform[k] = rdf(t + 16 + 4 * k);
		for (int k = 0; k < 3; ++k) {
			limb.min[k] = rdf(t + 64 + 4 * k);
			limb.max[k] = rdf(t + 76 + 4 * k);
		}
		limb.xsize = t[88];
		limb.ysize = t[89];
		limb.zsize = t[90];
		limb.normal_mode = t[91];
		if (limb.normal_mode < 1 || limb.normal_mode > 4)
			return false;
		out.limbs_.push_back(std::move(limb));
	}
	return true;
}

bool VxlFile::decode_limb(size_t index, std::vector<Voxel>& voxels) const
{
	voxels.clear();
	if (index >= limbs_.size())
		return false;
	const VxlLimb& limb = limbs_[index];
	const uint8_t* body = data_.data() + body_offset_;
	const size_t columns = size_t(limb.xsize) * limb.ysize;

	auto in_body = [&](size_t offset, size_t bytes) { return offset + bytes <= body_size_; };
	if (!in_body(limb.span_start_offset, columns * 4) || !in_body(limb.span_end_offset, columns * 4))
		return false;

	for (size_t c = 0; c < columns; ++c) {
		const int32_t start = static_cast<int32_t>(rd32(body + limb.span_start_offset + 4 * c));
		const int32_t end = static_cast<int32_t>(rd32(body + limb.span_end_offset + 4 * c));
		if (start == -1) {
			if (end != -1)
				return false;
			continue;
		}
		if (start < 0 || end < start)
			return false;
		size_t p = size_t(limb.span_data_offset) + size_t(start);
		const size_t column_end = size_t(limb.span_data_offset) + size_t(end);
		const uint8_t x = static_cast<uint8_t>(c % limb.xsize);
		const uint8_t y = static_cast<uint8_t>(c / limb.xsize);

		unsigned z = 0;
		while (z < limb.zsize) {
			if (!in_body(p, 2))
				return false;
			const unsigned skip = body[p];
			const unsigned count = body[p + 1];
			p += 2;
			z += skip;
			if (!in_body(p, size_t(count) * 2 + 1) || z + count > limb.zsize)
				return false;
			for (unsigned k = 0; k < count; ++k) {
				voxels.push_back({ x, y, static_cast<uint8_t>(z + k), body[p], body[p + 1] });
				p += 2;
			}
			z += count;
			if (body[p] != count) // each span repeats its voxel count after the data
				return false;
			++p;
		}
		if (p != column_end + 1 && p - 1 != column_end)
			return false;
	}
	return true;
}

bool HvaFile::parse(std::span<const uint8_t> data, HvaFile& out)
{
	if (data.size() < 24)
		return false;
	const uint32_t frames = rd32(data.data() + 16);
	const uint32_t sections = rd32(data.data() + 20);
	if (frames == 0 || frames > 4096 || sections > 64) // a file with no sections is a valid placeholder
		return false;
	const size_t names_end = 24 + size_t(sections) * 16;
	if (names_end + size_t(frames) * sections * 48 != data.size())
		return false;
	out.frames_ = frames;
	out.sections_ = sections;
	out.names_.clear();
	for (uint32_t i = 0; i < sections; ++i) {
		const char* n = reinterpret_cast<const char*>(data.data() + 24 + size_t(i) * 16);
		out.names_.emplace_back(n, strnlen(n, 16));
	}
	out.matrices_.resize(size_t(frames) * sections * 12);
	for (size_t i = 0; i < out.matrices_.size(); ++i)
		out.matrices_[i] = rdf(data.data() + names_end + 4 * i);
	return true;
}

bool VplFile::parse(std::span<const uint8_t> data, VplFile& out)
{
	if (data.size() < 16 + 768)
		return false;
	out.remap_start = rd32(data.data());
	out.remap_end = rd32(data.data() + 4);
	out.section_count = rd32(data.data() + 8);
	if (out.section_count == 0 || out.section_count > 256 || data.size() != 16 + 768 + size_t(out.section_count) * 256)
		return false;
	out.palette.assign(data.begin() + 16, data.begin() + 16 + 768);
	out.lookup.assign(data.begin() + 16 + 768, data.end());
	return true;
}

} // namespace ra::formats
