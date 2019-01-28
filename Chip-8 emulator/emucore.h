#pragma once

#include "utils.h"

struct alignas(2) time_control_t
{
	u8 sound;
	u8 delay;
};

namespace registers
{
	extern u8 vmMemory[4096];
	extern u8 gfxMemory[2048];
	extern u8 gpr[16];
	extern u16 stack[16];
	extern u16 sp;
	extern u16 pc;
	extern u16 index;
	extern std::atomic<time_control_t> timers;
};

// Emulated CPU memory manager
namespace vm
{
	template<typename T>
	void write(u32 addr, T value)
	{
		*reinterpret_cast<T*>(registers::vmMemory + addr) = value;
	}

	template<typename T>
	T read(u32 addr)
	{
		return *reinterpret_cast<T*>(registers::vmMemory + addr);
	}

	template<typename T>
	T& ref(u32 addr)
	{
		return *reinterpret_cast<T*>(registers::vmMemory + addr);
	}

	template<typename T>
	T* ptr(u32 addr)
	{
		return reinterpret_cast<T*>(registers::vmMemory + addr);
	}
};

extern bool DisplayDirty;

void resetRegisters();
void ExecuteOpcode();
