/* This file is part of the Recoil engine (GPL v2 or later), see LICENSE.html */

#include "WorkbenchMemory.h"

#if defined(_WIN32)
	#include <windows.h>
	#include <psapi.h>
#else
	#include <cstdio>
	#include <unistd.h>
#endif

size_t GetProcessResidentBytes()
{
#if defined(_WIN32)
	// K32 variant lives in kernel32, so no psapi import library is needed
	PROCESS_MEMORY_COUNTERS pmc;
	if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
		return static_cast<size_t>(pmc.WorkingSetSize);
	return 0;
#else
	FILE* f = std::fopen("/proc/self/statm", "r");
	if (f == nullptr)
		return 0;
	unsigned long size = 0, resident = 0;
	const int n = std::fscanf(f, "%lu %lu", &size, &resident);
	std::fclose(f);
	if (n != 2)
		return 0;
	return static_cast<size_t>(resident) * static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
}
