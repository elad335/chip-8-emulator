#pragma once

#include "utils.h"

struct emu_state
{
	// The RAM (4k + instruction flow guard)
	alignas(16) u8 memBase[4096 + 2];
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
	union { volatile short data; struct { volatile u8 sound, delay; }; } timers;
	// Set when it's time to close the emulator
	volatile bool terminate = false;
	// Timers thread's thread handle
	std::thread* volatile hwtimers;
	// DRW pixel decoding lookup table
	u64 DRWtable[UINT8_MAX + 1]; 
	// compatibilty flag (mask) for schip 8 (don't confuse with is_super)
	u32 compatibilty = 0;
	// Video mode
	bool extended = false;
	// Asmjit/Interpreter: function table
	std::uintptr_t ops[UINT16_MAX + 1];
	// Settings section: sleep between instructions in ms
	u64 sleep_period = 16;
	// Is schip 8 boolean
	bool is_super = false;
	// DRW wrapping
	bool DRW_wrap = true;
	// Opcodes simple fallbacks
	void OpcodeFallback();
	// Reset registers
	void reset();
	// Load rom
	void load_exec();
	// VF reference wrapper
	u8& getVF();

	// Framebuffer swizzling constants
	static constexpr size_t y_stride = 256;
	static constexpr size_t y_size_ex = 64;
	static constexpr size_t x_size_ex = 128;
	static constexpr size_t y_size = 32;
	static constexpr size_t x_size = 64;
	static constexpr size_t xy_mask = (0x1f * y_stride) | (0x3f);
	static constexpr size_t xy_mask_ex = (0x3f * y_stride) | (0x7f);

	// Video memory (64*32 pixels, see DRW for details)
	alignas(32) u8 gfxMemory[y_stride * y_size_ex];

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

extern emu_state g_state;

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
