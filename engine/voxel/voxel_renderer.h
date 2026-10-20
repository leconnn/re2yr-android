#pragma once

// Software voxel renderer: draws a voxel model (VXL sections with HVA animation matrices)
// into a palette-indexed sprite, the way the original does (P3 section 5.1).
//
// The sprite is drawn once per (model, facing, animation frame) and cached by the caller; the
// map renderer then draws it like any other sprite. This module has no GL code and works in
// "model units", where one unit is one screen pixel after the view rotation.

#include "formats/voxel.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ra::voxel {

// A 3x4 affine matrix, row-major: out = M * (x, y, z, 1). Same layout as the HVA files.
using Mat34 = std::array<float, 12>;

Mat34 identity();
Mat34 multiply(const Mat34& a, const Mat34& b); // a * b
// m * Rotation: the rotations post-multiply, as the original's Matrix3D does.
Mat34 rotate_x(const Mat34& m, float radians);
Mat34 rotate_y(const Mat34& m, float radians);
Mat34 rotate_z(const Mat34& m, float radians);
// Moves the origin along the matrix's own x axis by 'amount' model units.
Mat34 translate_local_x(const Mat34& m, float amount);

// The fixed view: RotX(-60 degrees) * RotZ(-45 degrees).
const Mat34& view_matrix();

// The body matrix on flat ground for a 16-bit facing: RotZ((f - 8) * -pi/16) where f is the
// facing in 32 steps. Facing 0 (north) turns the model's +x toward the top-right of the screen.
Mat34 body_matrix(uint16_t facing16);

// The body facing in 32 steps, as the original rounds it.
int facing32(uint16_t facing16);

// How many model units a distance in leptons is (60 / sqrt(2 * 256^2)).
double units_from_leptons(double leptons);

// One part of a model: a VXL limb and its matrix relative to the body (an HVA frame matrix with
// its translation already scaled to model units, and for a turret the offset and rotation).
struct Section {
	const ra::formats::VxlFile* vxl = nullptr;
	size_t limb = 0;
	Mat34 relative = identity();
};

// The scale from an HVA translation to model units: the limb's tailer scale (1/12 in every file).
Mat34 hva_matrix(const ra::formats::HvaFile& hva, uint32_t frame, uint32_t section, float tailer_scale);

// A rendered sprite. Pixels are palette indices after the VPL lookup (0 = empty). The sprite's
// top-left corner goes at (offset_x, offset_y) from the object's screen point.
struct Image {
	int width = 0, height = 0;
	int offset_x = 0, offset_y = 0;
	std::vector<uint8_t> pixels; // width * height

	bool empty() const { return width <= 0 || height <= 0; }
};

class Renderer {
public:
	// 'vpl' must outlive the renderer.
	explicit Renderer(const ra::formats::VplFile& vpl) : vpl_(&vpl) {}

	// Draws the sections. 'body' is the body matrix without the view (see body_matrix); the view
	// is applied here. Sections are drawn back to front by their nearest-to-the-viewer depth.
	Image render(const Mat34& body, std::span<const Section> sections) const;

private:
	const ra::formats::VplFile* vpl_;
};

} // namespace ra::voxel
