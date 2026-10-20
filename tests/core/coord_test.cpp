#include "core/coord.h"

#include <gtest/gtest.h>

using namespace ra;

TEST(Coord, CellOfUsesDivision)
{
	EXPECT_EQ(cell_of({ 0, 0, 0 }), (Cell { 0, 0 }));
	EXPECT_EQ(cell_of({ 255, 256, 0 }), (Cell { 0, 1 }));
	EXPECT_EQ(cell_of({ 25600 + 128, 300 * 256 + 255, 999 }), (Cell { 100, 300 }));
	// Truncation toward zero, not floor.
	EXPECT_EQ(cell_of({ -1, -256, 0 }), (Cell { 0, -1 }));
}

TEST(Coord, CellCenterAndOrigin)
{
	EXPECT_EQ(cell_center({ 10, 20 }), (Coord { 2688, 5248, 0 }));
	EXPECT_EQ(cell_center({ 10, 20 }, 104), (Coord { 2688, 5248, 104 }));
	EXPECT_EQ(cell_origin({ 10, 20 }), (Coord { 2560, 5120, 0 }));
	EXPECT_EQ(cell_of(cell_center({ 37, 81 })), (Cell { 37, 81 }));
}
