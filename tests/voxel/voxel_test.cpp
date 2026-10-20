#include "voxel/voxel_renderer.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace ra::voxel;

namespace {

// Where the model's +x axis points on the screen: x to the right, y down (the view's y is negated).
std::pair<float, float> screen_of_x(const Mat34& body)
{
	const Mat34 m = multiply(view_matrix(), body);
	return { m[0], -m[4] };
}

} // namespace

TEST(Voxel, ViewMatrixIsRotXThenRotZ)
{
	const Mat34& v = view_matrix();
	const float expected[12] = { 0.7071f, 0.7071f, 0.0f, 0.0f, -0.3536f, 0.3536f, 0.866f, 0.0f, 0.6124f, -0.6124f, 0.5f, 0.0f };
	for (int i = 0; i < 12; ++i)
		EXPECT_NEAR(v[size_t(i)], expected[i], 2e-3f) << "entry " << i;
}

TEST(Voxel, FacingsTurnTheModelAroundTheScreen)
{
	// Facing 0 is north: the model's front points to the top-right. East is the bottom-right.
	auto north = screen_of_x(body_matrix(0x0000));
	auto east = screen_of_x(body_matrix(0x4000));
	auto south = screen_of_x(body_matrix(0x8000));
	auto west = screen_of_x(body_matrix(0xC000));
	EXPECT_GT(north.first, 0.5f);
	EXPECT_LT(north.second, -0.2f);
	EXPECT_GT(east.first, 0.5f);
	EXPECT_GT(east.second, 0.2f);
	EXPECT_LT(south.first, -0.5f);
	EXPECT_GT(south.second, 0.2f);
	EXPECT_LT(west.first, -0.5f);
	EXPECT_LT(west.second, -0.2f);
}

TEST(Voxel, FacingRoundsToThirtyTwoSteps)
{
	EXPECT_EQ(facing32(0x0000), 0);
	EXPECT_EQ(facing32(0x0800), 1);   // exactly one step (65536 / 32)
	EXPECT_EQ(facing32(0x0400), 1);   // half a step rounds up
	EXPECT_EQ(facing32(0x03FF), 0);
	EXPECT_EQ(facing32(0x8000), 16);
	EXPECT_EQ(facing32(0xFFFF), 0);   // wraps around
}

TEST(Voxel, LeptonsToModelUnits)
{
	// 256 leptons (one cell) is 60 / sqrt(2) pixels along an axis of the diamond.
	EXPECT_NEAR(units_from_leptons(256.0), 60.0 / std::sqrt(2.0), 1e-9);
	EXPECT_NEAR(units_from_leptons(0.0), 0.0, 0.0);
}

TEST(Voxel, MatrixHelpers)
{
	const Mat34 t = translate_local_x(rotate_z(identity(), 3.14159265f / 2.0f), 10.0f);
	// After a quarter turn the local x axis is the parent's y axis (the rotation is RotZ).
	EXPECT_NEAR(t[3], 0.0f, 1e-3f);
	EXPECT_NEAR(std::fabs(t[7]), 10.0f, 1e-2f);
	const Mat34 m = multiply(identity(), t);
	for (size_t i = 0; i < 12; ++i)
		EXPECT_FLOAT_EQ(m[i], t[i]);
}
