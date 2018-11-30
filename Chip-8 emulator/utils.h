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
    // Atomic operation PowerPC/ARM/TSX/PS3 style
    template <typename T, typename F, typename RT = std::invoke_result_t<F, T&>>
	__forceinline RT op(std::atomic<T>& var, F&& func)
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
};
