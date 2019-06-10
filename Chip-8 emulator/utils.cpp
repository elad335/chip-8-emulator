#include "utils.h"

// Check if CPU has AVX support (taken from https://github.com/RPCS3/rpcs3/blob/master/Utilities/sysinfo.cpp#L29)
bool has_avx()
{
	static const bool g_value = get_cpuid(0, 0)[0] >= 0x1 && get_cpuid(1, 0)[2] & 0x10000000 && (get_cpuid(1, 0)[2] & 0x0C000000) == 0x0C000000 && (get_xgetbv(0) & 0x6) == 0x6;
	return g_value;
}

// Check if CPU has MOVBE instruction support
bool has_movbe()
{
	static const bool g_value = (get_cpuid(1, 0)[2] & 0x400000) != 0;
	return g_value;
}
