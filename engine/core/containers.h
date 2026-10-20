#pragma once

// Container rules for simulation code.
//
// The original keeps objects in arrays and removes them by shifting the
// rest down, so iteration order is always insertion order. Simulation code
// must do the same: use std::vector (or std::map when keyed), remove with
// erase_ordered, and never iterate an unordered container, whose order can
// differ between platforms and runs.

#include <algorithm>
#include <cstddef>
#include <vector>

namespace ra {

// Removes the element at index, keeping the order of the others.
template <typename T>
void erase_ordered(std::vector<T>& v, std::size_t index)
{
	v.erase(v.begin() + static_cast<std::ptrdiff_t>(index));
}

// Removes the first element equal to value, keeping the order of the others.
// Returns false if the value isn't present.
template <typename T, typename U>
bool erase_first(std::vector<T>& v, const U& value)
{
	auto it = std::find(v.begin(), v.end(), value);
	if (it == v.end())
		return false;
	v.erase(it);
	return true;
}

template <typename T, typename U>
bool contains(const std::vector<T>& v, const U& value)
{
	return std::find(v.begin(), v.end(), value) != v.end();
}

} // namespace ra
