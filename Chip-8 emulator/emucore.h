#pragma once

#include "utils.h"
#include "ASMJIT/AsmInterpreter.h"

struct alignas(2) time_control_t
{
	u8 sound;
	u8 delay;
};

enum emu_flag : u32
{
	clear_screan = (1 << 0),
	display_update = (1 << 1),
	illegal_operation = (1 << 2),
	fallback = (1 << 3) // Opcode fallback requested
};

struct emu_state_t
{
	// The RAM
	alignas(16) u8 vmMemory[4096];
	// Video memory (64*32 pixels)
	alignas(32) u8 gfxMemory[2048];
	// Registers
	u8 gpr[16];
	// Stack
	u32 stack[16];
	// Stack pointer
	u32 sp;
	// Current instruction address
	u32 pc;
	// Memory pointer
	u32 index;
	// Container for delay timer and sound timer
	std::atomic<time_control_t> timers;
	// Set to reflect certian emu conditions
	u32 emu_flags = {};
	// Asmjit/Interpreter: function table
	std::array<void(*)(void*, u16), UINT16_MAX + 1> ops;
	// Opcodes simple fallbacks
	void OpcodeFallback();
	// Reset registers
	void reset();
};

extern emu_state_t g_state;

// Emulated CPU memory manager
namespace vm
{
	template<typename T>
	void write(u32 addr, T value)
	{
		*reinterpret_cast<T*>(g_state.vmMemory + addr) = value;
	}

	template<typename T>
	T read(u32 addr)
	{
		return *reinterpret_cast<T*>(g_state.vmMemory + addr);
	}

	template<typename T>
	T& ref(u32 addr)
	{
		return *reinterpret_cast<T*>(g_state.vmMemory + addr);
	}

	template<typename T>
	T* ptr(u32 addr)
	{
		return reinterpret_cast<T*>(g_state.vmMemory + addr);
	}
};

template<size_t _index, bool is_be = false>
inline u8 getField(u16 opcode)
{
	// Byteswap fields if specified
	constexpr size_t index = _index ^ (is_be ? 2 : 0);

	if constexpr (index == 0)
	{
		// Optimization
		return opcode & 0xF;
	}

	if constexpr (index == 3)
	{
		// Optimization
		return opcode >> 12;
	}

	return (opcode >> (index * 4)) & 0xF;
}
