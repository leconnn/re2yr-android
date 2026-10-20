#include "core/containers.h"

#include <gtest/gtest.h>

TEST(Containers, EraseKeepsOrder)
{
	std::vector<int> v { 1, 2, 3, 4, 5 };
	ra::erase_ordered(v, 1);
	EXPECT_EQ(v, (std::vector<int> { 1, 3, 4, 5 }));
	EXPECT_TRUE(ra::erase_first(v, 4));
	EXPECT_EQ(v, (std::vector<int> { 1, 3, 5 }));
	EXPECT_FALSE(ra::erase_first(v, 42));
	EXPECT_TRUE(ra::contains(v, 5));
	EXPECT_FALSE(ra::contains(v, 2));
}
