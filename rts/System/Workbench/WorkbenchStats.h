/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */
#pragma once

#include <cstddef>
#include <vector>

struct WorkbenchSummary {
	size_t count = 0;
	float mean = 0.0f;
	float p50 = 0.0f;
	float p95 = 0.0f;
	float p99 = 0.0f;
	float max = 0.0f;
};

// nearest-rank percentiles; takes a copy because it sorts
WorkbenchSummary SummarizeSamples(std::vector<float> samples);
