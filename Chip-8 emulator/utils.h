#pragma once
#include <iostream>
#include <sstream>
#include <cstdint>
#include <cstdlib>
#include <stdlib.h>
#include <utility>
#include <thread>
#include <array>
#include <atomic>
#include <string>
#include <iterator>
#include <fstream>
#include <immintrin.h>
#include <functional>
#include <type_traits>

#include "Windows.h"
#undef min // Workaround for asmjit compilation (using std::min)
#undef max // This is techinically windows.h fault and not anyone's else

#define force_inline __forceinline
#define never_inline __declspec(noinline)

#define ASSUME(...) __assume(__VA_ARGS__)
#define UNREACHABLE() ASSUME(0)

#define hwBpx() __debugbreak()

typedef std::uint8_t u8;
typedef std::uint16_t u16;
typedef std::uint32_t u32;
typedef std::uint64_t u64;
typedef std::uintptr_t uptr;
typedef std::uintmax_t umax_t;

typedef std::int8_t s8;
typedef std::int16_t s16;
typedef std::int32_t s32;
typedef std::int64_t s64;
typedef std::intptr_t sptr;
typedef std::intmax_t smax_t;

typedef double f64;
typedef float f32;

namespace
{
	template<typename T>
	force_inline T assert(const T&& value)
	{
		if (!value)
		{
			// Segfault
			static_cast<volatile std::atomic_flag*>(nullptr)->test_and_set();
		}

		return value;
	}

	// Assert if condition provided fails (via lambda)
	template<typename T, typename F>
	force_inline T assert(const T&& value, F&& func)
	{
		if (!std::invoke(std::forward<F>(func), value))
		{
			// Segfault
			static_cast<volatile std::atomic_flag*>(nullptr)->test_and_set();
		}

		return value;
	}
}

// Guarenteed zero extension regardless of the sign of the source and destination types
// Note: can also be used for truncuation 
template<typename To, typename From>
inline constexpr To zext(const From& from)
{
	return static_cast<To>(static_cast<std::make_unsigned_t<To>>(static_cast<std::make_unsigned_t<From>>(from)));
}

// Guarenteed sign extension regardless of the sign of the source and destination types
template<typename To, typename From>
inline constexpr To sext(const From& from)
{
	return static_cast<To>(static_cast<std::make_signed_t<To>>(static_cast<std::make_signed_t<From>>(from)));
}

// A partial equivalent to c++20's std::bit_cast (not constexpr)
template <class To, class From, typename = std::enable_if_t<sizeof(To) == sizeof(From)>>
inline To bitcast(const From& from) noexcept
{
	static_assert(sizeof(To) == sizeof(From), "bitcast: incompatible type size");

	To result;
	std::memcpy(&result, &from, sizeof(From));
	return result;
}

// Get integral/floats data as BE endian data (assume host LE architecture)
template <typename T>
inline T get_be_data(const T& data)
{
	constexpr size_t N = sizeof(T);

	if constexpr (N == 1)
	{
		return data;
	}

	if constexpr (N == 2)
	{
		return bitcast<u16>(_byteswap_ushort(bitcast<u16>(data)));
	}

	if constexpr (N == 4)
	{
		return bitcast<u32>(_byteswap_ulong(bitcast<u32>(data)));
	}

	if constexpr (N == 8)
	{
		return bitcast<u64>(_byteswap_ushort(bitcast<u64>(data)));
	}

	assert(false);
}

// This returns relative offset of member class from 'this' (enhanced version of offsetof macro)
template <typename T, typename T2>
inline u32 offset_of(T T2::*const mptr)
{
	return ::bitcast<u32>(mptr);
}

inline std::array<u32, 4> get_cpuid(u32 func, u32 subfunc)
{
	int regs[4]{};
	__cpuidex(regs, func, subfunc);
	return { 0u + regs[0], 0u + regs[1], 0u + regs[2], 0u + regs[3] };
}

// Check if CPU has AVX support (taken from https://github.com/RPCS3/rpcs3/blob/master/Utilities/sysinfo.cpp#L29)
bool has_avx();
// Check if CPU has MOVBE instruction support
bool has_movbe();

// Bit scanning utils
inline u32 cntlz32(u32 arg, bool nonzero = false)
{
	unsigned long res;
	return _BitScanReverse(&res, arg) || nonzero ? res ^ 31 : 32;
}

inline u64 cntlz64(u64 arg, bool nonzero = false)
{
	unsigned long res;
	return _BitScanReverse64(&res, arg) || nonzero ? res ^ 63 : 64;
}

inline u32 cnttz32(u32 arg, bool nonzero = false)
{
	unsigned long res;
	return _BitScanForward(&res, arg) || nonzero ? res : 32;
}

inline u64 cnttz64(u64 arg, bool nonzero = false)
{
	unsigned long res;
	return _BitScanForward64(&res, arg) || nonzero ? res : 64;
}

// Lightweight log2 functions (doesnt use floating point)
inline u32 flog2(u32 value) // Floor log2
{
	return value <= 1 ? 0 : ::cntlz32(value, true) ^ 31;
}

inline u32 clog2(u32 value) // Ceil log2
{
	return value <= 1 ? 0 : ::cntlz32((value - 1) << 1, true) ^ 31;
}

// Constexpr log2 varients (<3)
template<umax_t value>
inline constexpr u8 flog2()
{
	umax_t value_ = value;

	value_ >>= 1;

	for (u8 i = 0;; i++, value_ >>= 1)
	{
		if (value_ == 0)
		{
			constexpr u8 res = i; // Verify constexpr
			return res;
		}
	}
}

template<umax_t value>
inline constexpr u8 clog2()
{
	umax_t value_ = value;
	constexpr umax_t ispow2 = value & (value - 1); // if power of 2 the result is 0

	value_ >>= 1;

	for (u8 i = 0;; i++, value_ >>= 1)
	{
		if (value_ == 0)
		{
			constexpr u8 res = i + zext<u8>(std::min<umax_t>(ispow2, 1)); // Verify constexpr
			return res;
		}
	}
}
