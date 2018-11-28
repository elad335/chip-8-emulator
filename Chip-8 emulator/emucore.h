#pragma once
#include <iostream>
#include <sstream>
#include <cstdint>
#include <cstdlib>
#include <stdlib.h>
#include <stdio.h>
#include <utility>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <string>
#include <immintrin.h>

// Do we want to implement linux support?
#ifdef _WIN32
#include  <windows.h>
#else
//
#endif

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

namespace registers
{
	extern u8 vmMemory[4096];
	extern u8 gfxMemory[2048];
	extern u8 gpr[16];
	extern u16 stack[16];
	extern u16 sp;
	extern u16 pc;
	extern u16 index;
	extern std::atomic<u8> sound_timer;
	extern std::atomic<u8> delay_timer;
	//extern u8 keys[8];
};

extern std::mutex time_m;
extern bool DisplayDirty;

void resetRegisters();
void ExecuteOpcode();