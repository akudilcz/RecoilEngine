/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "WorkbenchStats.h"

#include <algorithm>
#include <cmath>
#include <numeric>

static float NearestRank(const std::vector<float>& sorted, float pct)
{
	const size_t n = sorted.size();
	const size_t rank = static_cast<size_t>(std::ceil(pct / 100.0f * n));
	return sorted[std::clamp<size_t>(rank, 1, n) - 1];
}

WorkbenchSummary SummarizeSamples(std::vector<float> samples)
{
	WorkbenchSummary s;
	if (samples.empty())
		return s;

	std::sort(samples.begin(), samples.end());
	s.count = samples.size();
	s.mean = static_cast<float>(std::accumulate(samples.begin(), samples.end(), 0.0) / s.count);
	s.p50 = NearestRank(samples, 50.0f);
	s.p95 = NearestRank(samples, 95.0f);
	s.p99 = NearestRank(samples, 99.0f);
	s.max = samples.back();
	return s;
}
