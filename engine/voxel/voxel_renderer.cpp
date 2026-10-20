#include "voxel/voxel_renderer.h"

#include "core/dir.h"
#include "core/fastmath.h"
#include "core/fpu.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ra::voxel {

namespace {

#include "voxel/voxel_normals.inc"

constexpr float kPi = 3.14159265358979323846f;

float sin_f(float a) { return static_cast<float>(ra::fastmath::sin(a)); }
float cos_f(float a) { return static_cast<float>(ra::fastmath::cos(a)); }

// The eight view octants (0x8468C0): {mode, start corner, x end, y end, z end, x flag, y flag, column step x, column step y}.
// Corners 0..3 are the bottom (min z) corners (max,max) (max,min) (min,min) (min,max); 4..7 the top ones in the same order.
struct Octant {
	int mode, start, x_end, y_end, z_end, x_flag, y_flag, dx, dy;
};
constexpr Octant kOctants[8] = {
	{ 0, 0, 3, 1, 4, 1, 1, -1, -1 },
	{ 0, 1, 2, 0, 5, 1, 0, -1, 1 },
	{ 0, 2, 1, 3, 6, 0, 0, 1, 1 },
	{ 0, 3, 0, 2, 7, 0, 1, 1, -1 },
	{ 1, 4, 7, 5, 0, 1, 1, -1, -1 },
	{ 1, 5, 6, 4, 1, 1, 0, -1, 1 },
	{ 1, 6, 5, 7, 2, 0, 0, 1, 1 },
	{ 1, 7, 4, 6, 3, 0, 1, 1, -1 },
};

struct SectionRecord {
	const Section* section = nullptr;
	const ra::formats::VxlLimb* limb = nullptr;
	float corner[8][3] {}; // x, -y, z after the transform
	int octant = 0;        // the corner nearest... farthest from the viewer: the one with the smallest z
	float max_z = -10000.0f;
};

// The 256 x 256 work buffer.
constexpr int kBuf = 256;

// The 32-bit packed position: x in the low 16 bits and y in the high 16 bits, in 8.8 fixed point.
// The original adds them as one 32-bit value, so a carry out of x lands in y; that is kept.
uint32_t pack(uint16_t x, uint16_t y) { return (uint32_t(y) << 16) | x; }

uint16_t ftol16(double v) { return static_cast<uint16_t>(ra::fpu::ftol(v) & 0xFFFF); }

// ((a + 128) - c) * 256
uint16_t start_coord(float a, float c)
{
	return ftol16(ra::fpu::mul(ra::fpu::sub(ra::fpu::add(a, 128.0), c), 256.0));
}
// (b - a) / n * 256
uint16_t step_coord(float b, float a, int n)
{
	return ftol16(ra::fpu::mul(ra::fpu::div(ra::fpu::sub(b, a), double(n)), 256.0));
}

// The shade table for a normal set (0x7586F0): per normal, which of the VPL's 32 light rows to use.
struct ShadeTable {
	uint8_t row[256] {};
};

void build_shade(int mode, const float light[3], const float view[3], ShadeTable& out)
{
	std::memset(out.row, 0, sizeof out.row);
	const float(*normals)[3] = nullptr;
	int count = 0;
	switch (mode) {
	case 1: normals = kNormalSet1; count = int(std::size(kNormalSet1)); break;
	case 2: normals = kNormalSet2; count = int(std::size(kNormalSet2)); break;
	case 3: normals = kNormalSet3; count = int(std::size(kNormalSet3)); break;
	case 4: normals = kNormalSet4; count = int(std::size(kNormalSet4)); break;
	default: return;
	}
	float h[3] = { light[0] + view[0], light[1] + view[1], light[2] + view[2] };
	const double len = ra::fastmath::sqrt(double(h[0]) * h[0] + double(h[1]) * h[1] + double(h[2]) * h[2]);
	if (len != 0.0)
		for (float& c : h)
			c = static_cast<float>(c / len);
	const float k = 3.0f; // the specular parameter (pushed by 0x706ED0)
	for (int n = 0; n < count && n < 253; ++n) {
		float diffuse = normals[n][0] * light[0] + normals[n][1] * light[1] + normals[n][2] * light[2];
		float s = normals[n][0] * h[0] + normals[n][1] * h[1] + normals[n][2] * h[2];
		float specular = s / ((k - s * k) + s);
		if (specular < 0.0f) specular = 0.0f;
		if (diffuse < 0.0f) diffuse = 0.0f;
		const double sum = double(diffuse) + double(specular);
		int row = sum < 0.0 ? 0 : ra::fpu::ftol(ra::fpu::mul(sum, 16.0));
		// The original can index row 32 (one past the table) when both terms are 1; clamp instead.
		out.row[n] = static_cast<uint8_t>(std::clamp(row, 0, 31));
	}
	out.row[253] = out.row[254] = out.row[255] = 16;
}

} // namespace

Mat34 identity()
{
	return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
}

Mat34 multiply(const Mat34& a, const Mat34& b)
{
	Mat34 r {};
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 4; ++j) {
			float v = a[size_t(i * 4)] * b[size_t(j)] + a[size_t(i * 4 + 1)] * b[size_t(4 + j)] + a[size_t(i * 4 + 2)] * b[size_t(8 + j)];
			if (j == 3)
				v += a[size_t(i * 4 + 3)];
			r[size_t(i * 4 + j)] = v;
		}
	}
	return r;
}

Mat34 rotate_x(const Mat34& m, float radians)
{
	const float s = sin_f(radians), c = cos_f(radians);
	Mat34 r = m;
	for (int i = 0; i < 3; ++i) {
		const float c1 = m[size_t(i * 4 + 1)], c2 = m[size_t(i * 4 + 2)];
		r[size_t(i * 4 + 1)] = c2 * s + c1 * c;
		r[size_t(i * 4 + 2)] = c2 * c - c1 * s;
	}
	return r;
}

Mat34 rotate_y(const Mat34& m, float radians)
{
	const float s = sin_f(radians), c = cos_f(radians);
	Mat34 r = m;
	for (int i = 0; i < 3; ++i) {
		const float c0 = m[size_t(i * 4)], c2 = m[size_t(i * 4 + 2)];
		r[size_t(i * 4)] = c0 * c - c2 * s;
		r[size_t(i * 4 + 2)] = c0 * s + c2 * c;
	}
	return r;
}

Mat34 rotate_z(const Mat34& m, float radians)
{
	const float s = sin_f(radians), c = cos_f(radians);
	Mat34 r = m;
	for (int i = 0; i < 3; ++i) {
		const float c0 = m[size_t(i * 4)], c1 = m[size_t(i * 4 + 1)];
		r[size_t(i * 4)] = c0 * c + c1 * s;
		r[size_t(i * 4 + 1)] = c1 * c - c0 * s;
	}
	return r;
}

Mat34 translate_local_x(const Mat34& m, float amount)
{
	Mat34 r = m;
	for (int i = 0; i < 3; ++i)
		r[size_t(i * 4 + 3)] = amount * m[size_t(i * 4)] + m[size_t(i * 4 + 3)];
	return r;
}

const Mat34& view_matrix()
{
	static const Mat34 view = rotate_z(rotate_x(identity(), -kPi / 3.0f), -kPi / 4.0f);
	return view;
}

int facing32(uint16_t facing16)
{
	return static_cast<int>(ra::Dir16(facing16).facing<32>());
}

Mat34 body_matrix(uint16_t facing16)
{
	const float angle = static_cast<float>(facing32(facing16) - 8) * -0.19634955f;
	return rotate_z(identity(), angle);
}

double units_from_leptons(double leptons)
{
	// _DAT_00b1d008 = 1 / (sqrt(2 * 256^2) / 60)
	const double per_unit = ra::fpu::div(ra::fpu::sqrt(ra::fpu::mul(2.0, 65536.0)), 60.0);
	return ra::fpu::mul(leptons, ra::fpu::div(1.0, per_unit));
}

Mat34 hva_matrix(const ra::formats::HvaFile& hva, uint32_t frame, uint32_t section, float tailer_scale)
{
	Mat34 m = identity();
	if (hva.frame_count() == 0 || section >= hva.section_count())
		return m;
	const float* src = hva.matrix(frame % hva.frame_count(), section);
	std::copy(src, src + 12, m.begin());
	m[3] *= tailer_scale;
	m[7] *= tailer_scale;
	m[11] *= tailer_scale;
	return m;
}

Image Renderer::render(const Mat34& body, std::span<const Section> sections) const
{
	Image image;
	if (sections.empty())
		return image;
	const Mat34 view_body = multiply(view_matrix(), body);

	// The light in model space: the fixed light rotated back through view * body. The view vector is
	// not rotated (the original passes the identity camera for it).
	// RotY(0.785375) applied to (-0.70710677, -0.70710677, 0): the fixed light, in view space.
	float light_view[3];
	{
		const Mat34 ry = rotate_y(identity(), 0.785375f);
		const float p[3] = { -0.70710677f, -0.70710677f, 0.0f };
		for (int i = 0; i < 3; ++i)
			light_view[i] = ry[size_t(i * 4)] * p[0] + ry[size_t(i * 4 + 1)] * p[1] + ry[size_t(i * 4 + 2)] * p[2];
	}
	float light[3];
	for (int j = 0; j < 3; ++j) // transpose of the rotation part
		light[j] = view_body[size_t(j)] * light_view[0] + view_body[size_t(4 + j)] * light_view[1] + view_body[size_t(8 + j)] * light_view[2];
	const float view_vec[3] = { 0.0f, 0.0f, 1.0f };
	ShadeTable shade_tables[5];
	bool shade_built[5] = {};

	// Transform the section bounding boxes and find the overall extent.
	std::vector<SectionRecord> records;
	float min_v[3] = { 10000.0f, 10000.0f, 10000.0f };
	float max_v[3] = { -10000.0f, -10000.0f, -10000.0f };
	for (const Section& s : sections) {
		if (!s.vxl || s.limb >= s.vxl->limbs().size())
			continue;
		SectionRecord r;
		r.section = &s;
		r.limb = &s.vxl->limbs()[s.limb];
		const Mat34 m = multiply(view_body, s.relative);
		const float* mn = r.limb->min;
		const float* mx = r.limb->max;
		const float corners[8][3] = {
			{ mx[0], mx[1], mn[2] }, { mx[0], mn[1], mn[2] }, { mn[0], mn[1], mn[2] }, { mn[0], mx[1], mn[2] },
			{ mx[0], mx[1], mx[2] }, { mx[0], mn[1], mx[2] }, { mn[0], mn[1], mx[2] }, { mn[0], mx[1], mx[2] },
		};
		float min_z = 10000.0f;
		for (int c = 0; c < 8; ++c) {
			float p[3];
			for (int i = 0; i < 3; ++i)
				p[i] = m[size_t(i * 4)] * corners[c][0] + m[size_t(i * 4 + 1)] * corners[c][1] + m[size_t(i * 4 + 2)] * corners[c][2] + m[size_t(i * 4 + 3)];
			r.corner[c][0] = p[0];
			r.corner[c][1] = -p[1];
			r.corner[c][2] = p[2];
			for (int i = 0; i < 3; ++i) {
				min_v[i] = std::min(min_v[i], r.corner[c][i]);
				max_v[i] = std::max(max_v[i], r.corner[c][i]);
			}
			if (p[2] < min_z) {
				min_z = p[2];
				r.octant = c;
			}
			r.max_z = std::max(r.max_z, p[2]);
		}
		records.push_back(r);
	}
	if (records.empty())
		return image;
	const float center[3] = { (min_v[0] + max_v[0]) * 0.5f, (min_v[1] + max_v[1]) * 0.5f, (min_v[2] + max_v[2]) * 0.5f };

	// Back to front: the original's exchange sort on each section's largest z.
	std::vector<SectionRecord*> order;
	for (auto& r : records)
		order.push_back(&r);
	for (size_t i = 0; i + 1 < order.size(); ++i)
		for (size_t j = i + 1; j < order.size(); ++j)
			if (order[j]->max_z < order[i]->max_z)
				std::swap(order[i], order[j]);

	std::vector<uint8_t> buffer(size_t(kBuf) * kBuf, 0);
	const uint8_t* vpl = vpl_->lookup.data();
	const size_t vpl_rows = vpl_->section_count;

	std::vector<ra::formats::Voxel> voxels;
	for (const SectionRecord* rec : order) {
		const ra::formats::VxlLimb& limb = *rec->limb;
		if (limb.xsize == 0 || limb.ysize == 0 || limb.zsize == 0)
			continue;
		if (!rec->section->vxl->decode_limb(rec->section->limb, voxels))
			continue;
		const int xs = limb.xsize, ys = limb.ysize, zs = limb.zsize;
		const Octant& oct = kOctants[rec->octant];

		// Normal lighting: one shade table per normal set.
		const int mode = limb.normal_mode;
		const uint8_t* shade = nullptr;
		if (mode >= 1 && mode <= 4) {
			if (!shade_built[mode]) {
				build_shade(mode, light, view_vec, shade_tables[mode]);
				shade_built[mode] = true;
			}
			shade = shade_tables[mode].row;
		}

		// Group the voxels by column, in z order (the decoder returns them that way).
		std::vector<std::vector<const ra::formats::Voxel*>> columns(size_t(xs) * ys);
		for (const auto& v : voxels)
			columns[size_t(v.y) * xs + v.x].push_back(&v);

		const float(*c)[3] = rec->corner;
		const uint16_t start_x = start_coord(c[oct.start][0], center[0]);
		const uint16_t start_y = start_coord(c[oct.start][1], center[1]);
		const uint16_t step_cx = step_coord(c[oct.x_end][0], c[oct.start][0], xs);
		const uint16_t step_cy = step_coord(c[oct.x_end][1], c[oct.start][1], xs);
		const uint16_t step_rx = step_coord(c[oct.y_end][0], c[oct.start][0], ys);
		const uint16_t step_ry = step_coord(c[oct.y_end][1], c[oct.start][1], ys);
		const uint16_t step_zx = step_coord(c[oct.z_end][0], c[oct.start][0], zs);
		const uint16_t step_zy = step_coord(c[oct.z_end][1], c[oct.start][1], zs);
		const uint32_t col_step = pack(step_cx, step_cy);
		const uint32_t row_step = pack(step_rx, step_ry);
		const uint32_t z_step = pack(step_zx, step_zy);

		// tbl[k]: the packed offset of k slices along z, built from 16-bit sums.
		uint32_t skip_tbl[257];
		{
			uint32_t ax = 0, ay = 0;
			for (int k = 0; k < 257; ++k) {
				skip_tbl[k] = pack(uint16_t(ax & 0xFFFF), uint16_t(ay & 0xFFFF));
				ax += step_zx;
				ay += step_zy;
			}
		}

		auto put = [&](uint32_t pos, const ra::formats::Voxel& v) {
			const uint32_t idx = ((pos >> 16) & 0xFF00u) | ((pos & 0xFFFFu) >> 8);
			uint8_t color = v.color;
			if (shade) {
				const size_t row = std::min<size_t>(shade[v.normal], vpl_rows ? vpl_rows - 1 : 0);
				color = vpl[row * 256 + v.color];
			}
			buffer[idx] = color;
		};

		auto draw_column = [&](const std::vector<const ra::formats::Voxel*>& col, uint32_t pos) {
			if (col.empty())
				return;
			if (oct.mode == 0) {
				int next = 0; // the next z that has not been passed
				size_t i = 0;
				while (i < col.size()) {
					const int z0 = col[i]->z;
					pos += skip_tbl[z0 - next];
					size_t j = i;
					while (j < col.size() && col[j]->z == col[i]->z + int(j - i)) {
						put(pos, *col[j]);
						pos += z_step;
						++j;
					}
					next = col[j - 1]->z + 1;
					i = j;
				}
			} else {
				// From the top down, with the empty slices above the topmost voxel skipped first.
				int top = zs; // one past the highest slice not yet passed
				size_t end = col.size();
				while (end > 0) {
					size_t begin = end - 1;
					while (begin > 0 && col[begin - 1]->z == col[begin]->z - 1)
						--begin;
					pos += skip_tbl[top - 1 - col[end - 1]->z];
					for (size_t k = end; k > begin; --k) {
						put(pos, *col[k - 1]);
						pos += z_step;
					}
					top = col[begin]->z;
					end = begin;
				}
			}
		};

		uint32_t row_pos = pack(start_x, start_y);
		int row_col = oct.y_flag * (ys - 1) * xs + oct.x_flag * (xs - 1);
		for (int j = 0; j < ys; ++j) {
			uint32_t pos = row_pos;
			int col = row_col;
			for (int i = 0; i < xs; ++i) {
				draw_column(columns[size_t(col)], pos);
				pos += col_step;
				col += oct.dx;
			}
			row_pos += row_step;
			row_col += oct.dy * xs;
		}
	}

	// The result rectangle (0x754510): the buffer area around the model, and where it goes.
	const int w0 = ra::fpu::ftol(ra::fpu::sub(max_v[0], min_v[0]));
	const int h0 = ra::fpu::ftol(ra::fpu::sub(max_v[1], min_v[1]));
	const int width = w0 + 8, height = h0 + 8;
	const int left = 128 - w0 / 2 - 4;
	const int top = 128 - h0 / 2 - 4;
	image.width = width;
	image.height = height;
	image.offset_x = ra::fpu::ftol(center[0]) - width / 2;
	image.offset_y = ra::fpu::ftol(center[1]) - height / 2;
	image.pixels.assign(size_t(width) * height, 0);
	for (int y = 0; y < height; ++y) {
		const int by = top + y;
		if (by < 0 || by >= kBuf)
			continue;
		for (int x = 0; x < width; ++x) {
			const int bx = left + x;
			if (bx >= 0 && bx < kBuf)
				image.pixels[size_t(y) * width + x] = buffer[size_t(by) * kBuf + bx];
		}
	}
	return image;
}

} // namespace ra::voxel
