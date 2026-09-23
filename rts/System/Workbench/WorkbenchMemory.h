/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */
#pragma once

#include <cstddef>

// Resident set size (Windows: working set) of this process in bytes, 0 if unknown.
size_t GetProcessResidentBytes();
