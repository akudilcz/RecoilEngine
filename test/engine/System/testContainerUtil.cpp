/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "System/ContainerUtil.h"

#include <algorithm>
#include <random>
#include <vector>

#include <catch_amalgamated.hpp>

// Reference: what CUnitHandler::DeleteUnit did before batching, one element at a time:
// find, move the slow-update cursor back if the element sat before it, erase.
static size_t EraseOneByOne(std::vector<int>& v, const std::vector<int>& remove, size_t cursor)
{
	for (const int e: remove) {
		const auto it = std::find(v.begin(), v.end(), e);
		if (it == v.end())
			continue;
		if (cursor > static_cast<size_t>(std::distance(v.begin(), it)))
			--cursor;
		v.erase(it);
	}
	return cursor;
}

TEST_CASE("VectorEraseSorted matches erasing one element at a time")
{
	std::mt19937 rng(1234);

	for (int round = 0; round < 500; ++round) {
		const int n = std::uniform_int_distribution<int>(0, 60)(rng);
		std::vector<int> v(n);
		for (int i = 0; i < n; ++i) v[i] = i * 3; // unique, like unit pointers
		std::shuffle(v.begin(), v.end(), rng);

		std::vector<int> remove;
		for (const int e: v)
			if (std::uniform_int_distribution<int>(0, 3)(rng) == 0) remove.push_back(e);
		remove.push_back(-7); // not present: must be ignored
		std::shuffle(remove.begin(), remove.end(), rng);

		const size_t cursor = std::uniform_int_distribution<size_t>(0, v.size())(rng);

		std::vector<int> expected = v;
		const size_t expectedCursor = EraseOneByOne(expected, remove, cursor);

		std::vector<int> sorted = remove;
		std::sort(sorted.begin(), sorted.end());
		const size_t moved = spring::VectorEraseSorted(v, sorted, cursor);

		REQUIRE(v == expected);
		REQUIRE(cursor - moved == expectedCursor);
	}
}

TEST_CASE("VectorEraseSorted edge cases")
{
	std::vector<int> v = {5, 1, 4};
	CHECK(spring::VectorEraseSorted(v, {}, 3) == 0);
	CHECK(v == std::vector<int>{5, 1, 4});

	CHECK(spring::VectorEraseSorted(v, {1, 4, 5}, 2) == 2);
	CHECK(v.empty());

	std::vector<int> empty;
	CHECK(spring::VectorEraseSorted(empty, {1}, 0) == 0);
	CHECK(empty.empty());
}
