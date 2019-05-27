#pragma once

#include "utils.h"

struct alignas(2) time_control_t
{
	u8 sound;
	u8 delay;
};

struct emu_state_t
{
	// The RAM (4k + instruction flow guard)
	alignas(16) u8 memBase[4096 + 2];
	// Video memory (64*32 pixels, see DRW for details)
	alignas(32) u8 gfxMemory[64 * 32 * 2];
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
	// Set when it's time to close the emulator
	volatile bool terminate = false;
	// Timers thread's thread handle
	std::thread* volatile hwtimers;
	// Asmjit/Interpreter: function table
	std::uintptr_t ops[UINT16_MAX + 1];
	// Opcodes simple fallbacks
	void OpcodeFallback();
	// Reset registers
	void reset();
	// VF reference wrapper
	u8& getVF();

	// Framebuffer swizzling constants
	static constexpr size_t y_shift = 128;
	static constexpr size_t x_shift = 1;
	static constexpr size_t xy_mask = (0x1fu * y_shift) | (0x3fu * x_shift);

	// Emulated CPU memory control
	template<typename T>
	void write(u32 addr, T value)
	{
		*reinterpret_cast<std::remove_const_t<T>*>(memBase + addr) = value;
	}

	template<typename T>
	T read(u32 addr)
	{
		return *reinterpret_cast<const T*>(memBase + addr);
	}

	template<typename T>
	T& ref(u32 addr)
	{
		return *reinterpret_cast<T*>(memBase + addr);
	}

	template<typename T>
	T* ptr(u32 addr)
	{
		return reinterpret_cast<T*>(memBase + addr);
	}
};

extern emu_state_t g_state;

template<size_t _index, bool is_be = false>
static inline u8 getField(u16 opcode)
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

// Logic relies on this
static_assert(sizeof(std::atomic<time_control_t>) == sizeof(time_control_t) && std::atomic<time_control_t>::is_always_lock_free);
