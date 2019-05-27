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

typedef std::int8_t s8;
typedef std::int16_t s16;
typedef std::int32_t s32;
typedef std::int64_t s64;
typedef std::intptr_t sptr;

typedef long double f80;
typedef double f64;
typedef float f32;

namespace atomic
{
	// Atomic operation
	template <typename T, typename F, typename RT = std::invoke_result_t<F, T&>>
	force_inline RT op(std::atomic<T>& var, F&& func)
	{
		T old = var.load(std::memory_order_acquire), state;

		while (true)
		{
			state = old;

			if constexpr (std::is_void_v<RT>)
			{
				std::invoke(std::forward<F>(func), state);

				if (var.compare_exchange_strong(old, state))
				{
					return;
				}
			}
			else
			{
				RT result = std::invoke(std::forward<F>(func), state);

				if (var.compare_exchange_strong(old, state))
				{
					return result;
				}
			}
		}
	}

	// Atomic operation (returns previous value)
	template <typename T, typename F, typename RT = std::invoke_result_t<F, T&>>
	force_inline T fetch_op(std::atomic<T>& var, F&& func)
	{
		static_assert(std::is_void_v<RT>, "Unsupported function return type passed to fetch_op");

		T old = var.load(std::memory_order_acquire), state;

		while (true)
		{
			std::invoke(std::forward<F>(func), (state = old));

			if (var.compare_exchange_strong(old, state))
			{
				return old;
			}
		}
	}

	// Atomic operation (cancelable, returns false if cancelled)
	template<typename T, typename F, typename RT = std::invoke_result_t<F, T&>>
	force_inline bool cond_op(std::atomic<T>& var, F&& func)
	{
		// TODO: detect bool conversation existence
		static_assert(std::is_same_v<RT, void> == false, "Unsupported function return type passed to cond_op");

		T old = var.load(std::memory_order_acquire), state;

		while (true)
		{
			const RT ret = std::invoke(std::forward<F>(func), (state = old));

			if (!ret || var.compare_exchange_strong(old, state))
			{
				return ret;
			}
		}
	}
};

namespace
{
	template<typename T>
	force_inline T assert(const T&& value)
	{
		if (!value)
		{
			// Segfault
			std::launder(static_cast<volatile std::atomic<u32>*>(nullptr))->load();
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
			std::launder(static_cast<volatile std::atomic<u32>*>(nullptr))->load();
		}

		return value;
	}
}

// Current implementation only allows usage of unsigned types
template <typename T>
struct be_t
{
	be_t()
	{
		static_assert(false, "Type does not meet the requirements of BE storage");
	}
};

// Specailization
template<>
struct be_t<u8>
{
	u16 m_data;

	constexpr be_t(const u8 value)
		: m_data(value)
	{
	}

	// A single byte doesnt need byteswapping
	operator u8()
	{
		return m_data;
	}
};

template<>
struct be_t<u16>
{
	u16 m_data;

	constexpr be_t(const u16 value)
		: m_data(value)
	{
	}

	operator u16()
	{
		return _byteswap_ushort(m_data);
	}
};

template<>
struct be_t<u32>
{
	u32 m_data;

	constexpr be_t(const u32 value)
		: m_data(value)
	{
	}

	operator u32()
	{
		return _byteswap_ulong(m_data);
	}
};

template<>
struct be_t<u64>
{
	u64 m_data;

	constexpr be_t(const u64 value)
		: m_data(value)
	{
	}

	operator u64()
	{
		return _byteswap_uint64(m_data);
	}
};

// This returns relative offset of member class from 'this'
template <typename T, typename T2>
static inline u32 offset32(T T2::*const mptr)
{
#ifdef _MSC_VER
	static_assert(sizeof(mptr) == sizeof(u32), "Invalid pointer-to-member size");
	return reinterpret_cast<const u32&>(mptr);
#elif __GNUG__
	static_assert(sizeof(mptr) == sizeof(std::size_t), "Invalid pointer-to-member size");
	return static_cast<u32>(reinterpret_cast<const std::size_t&>(mptr));
#else
	static_assert(sizeof(mptr) == 0, "Invalid pointer-to-member size");
#endif
}

inline std::array<u32, 4> get_cpuid(u32 func, u32 subfunc)
{
	int regs[4];
#ifdef _MSC_VER
	__cpuidex(regs, func, subfunc);
#else
	__asm__ volatile("cpuid" : "=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3]) : "a" (func), "c" (subfunc));
#endif
	return { 0u + regs[0], 0u + regs[1], 0u + regs[2], 0u + regs[3] };
}

inline u64 get_xgetbv(u32 xcr)
{
#ifdef _MSC_VER
	return _xgetbv(xcr);
#else
	u32 eax, edx;
	__asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(xcr));
	return eax | (u64(edx) << 32);
#endif
}

// Check if CPU has AVX support (taken from https://github.com/RPCS3/rpcs3/blob/master/Utilities/sysinfo.cpp#L29)
static bool has_avx()
{
	static const bool g_value = get_cpuid(0, 0)[0] >= 0x1 && get_cpuid(1, 0)[2] & 0x10000000 && (get_cpuid(1, 0)[2] & 0x0C000000) == 0x0C000000 && (get_xgetbv(0) & 0x6) == 0x6;
	return g_value;
}

// Check if CPU has MOVBE instruction support
static bool has_movbe()
{
	static const bool g_value = (get_cpuid(1, 0)[2] & 0x400000) != 0;
	return g_value;
}

// Bit scanning utils
static inline u32 cntlz32(u32 arg, bool nonzero = false)
{
	unsigned long res;
	return _BitScanReverse(&res, arg) || nonzero ? res ^ 31 : 32;
}

static inline u64 cntlz64(u64 arg, bool nonzero = false)
{
	unsigned long res;
	return _BitScanReverse64(&res, arg) || nonzero ? res ^ 63 : 64;
}

static inline u32 cnttz32(u32 arg, bool nonzero = false)
{
	unsigned long res;
	return _BitScanForward(&res, arg) || nonzero ? res : 32;
}

static inline u64 cnttz64(u64 arg, bool nonzero = false)
{
	unsigned long res;
	return _BitScanForward64(&res, arg) || nonzero ? res : 64;
}

// Lightweight log2 functions (doesnt use floating point)
static inline u32 flog2(u32 value) // Floor log2
{
	return value <= 1 ? 0 : ::cntlz32(value, true) ^ 31;
}

static inline u32 clog2(u32 value) // Ceil log2
{
	return value <= 1 ? 0 : ::cntlz32((value - 1) << 1, true) ^ 31;
}

// Constexpr log2 varients (<3)
template<typename T, T value>
static inline constexpr T flog2()
{
	std::make_unsigned_t<T> value_ = static_cast<std::make_unsigned_t<T>>(value);

	for (size_t i = (sizeof(T) * 8) - 1; i >= 0; i--, value_ <<= 1)
	{
		if (value_ & (static_cast<std::make_unsigned_t<T>>(std::numeric_limits<std::make_signed_t<T>>::min())))
		{
			return i;
		}
	}

	return 0;
}

template<typename T, T value>
static inline constexpr T clog2()
{
	std::make_unsigned_t<T> value_ = static_cast<std::make_unsigned_t<T>>(value);

	for (size_t i = (sizeof(T) * 8) - 1; i >= 0; i--, value_ <<= 1)
	{
		if (value_ & (static_cast<std::make_unsigned_t<T>>(std::numeric_limits<std::make_signed_t<T>>::min())))
		{
			return i + T(value_ & static_cast<std::make_unsigned_t<T>>(std::numeric_limits<std::make_signed_t<T>>::max()) != 0);
		}
	}

	return 0;
}