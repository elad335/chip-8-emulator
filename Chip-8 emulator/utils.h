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
        T old = var.load(std::memory_order_relaxed), state;

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

		T old = var.load(std::memory_order_relaxed), state;

		while (true)
		{
			std::invoke(std::forward<F>(func), (state = old));

			if (var.compare_exchange_strong(old, state))
			{
				return old;
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
			*reinterpret_cast<u32*>(0ull) = 0;
			_mm_sfence();
		}

		return value;
	}
}
