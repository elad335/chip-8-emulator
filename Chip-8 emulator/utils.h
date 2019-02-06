#pragma once
#include <iostream>
#include <sstream>
#include <cstdint>
#include <cstdlib>
#include <stdlib.h>
#include <utility>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <string>
#include <iterator>
#include <fstream>
#include <immintrin.h>

#include "Windows.h"

#define force_inline __forceinline
#define never_inline __declspec(noinline)

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

	template<typename T>
	void store(T& var, T value)
	{
		reinterpret_cast<std::atomic<T>*>(&var)->store(value);
	}
};

namespace
{
	template<typename T>
	force_inline T assert(T& value)
	{
		if (!value)
		{
			reinterpret_cast<std::atomic<u32>*>(nullptr)->exchange(0);
		}

		return value;
	}
}

template <typename T>
struct be_t
{
	//be_t(T value)
	//{
	//	static_assert((std::is_integral_v<T> || std::is_enum_v<T>) && sizeof(T) > 1, "Value cannot be converted into be_t");
	//}
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
