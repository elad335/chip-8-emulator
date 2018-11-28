#pragma once

#include "emucore.h"
#include "input.h"

namespace registers
{
	// The RAM
	u8 vmMemory[4096];
	// Video memory (64*32 pixels)
	u8 gfxMemory[2048];
	// Registers
	u8 gpr[16];
	// Stack
	u16 stack[16];
	// Stack pointer
	u16 sp;
	// Current instruction address
	u16 pc;
	// Memory pointer
	u16 index;
	// Time until a beep is made
	std::atomic<u8> sound_timer;
	// Delay timer
	std::atomic<u8> delay_timer;
};

using namespace registers;

// Timers mutex
std::mutex time_m;

// Update screen flag
bool DisplayDirty = false;

// Emulated CPU memory manager
namespace vm
{
	template<typename T>
	void write(u32 addr, T value)
	{
		*reinterpret_cast<T*>(vmMemory + addr) = value;
	}

	template<typename T>
	T read(u32 addr)
	{
		return *reinterpret_cast<T*>(vmMemory + addr);
	}

	template<typename T>
	T& ref(u32 addr)
	{
		return *reinterpret_cast<T*>(vmMemory + addr);
	}

	template<typename T>
	T* ptr(u32 addr)
	{
		return reinterpret_cast<T*>(vmMemory + addr);
	}
};

void resetRegisters()
{
	std::memset(vmMemory, 0, 4096);
	std::memset(gfxMemory, 0, 2048);
	std::memset(gpr, 0, 16 * sizeof(u8));
	std::memset(stack, 0, 16 * sizeof(u16));
	::sp = 0;
	::pc = 0x200;
	::index = 0;
	::sound_timer.store(0);
	::delay_timer.store(0);
}


template<size_t index>
inline u8 getField(u16 opcode)
{
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

constexpr u8& getVF()
{
	// The 15 register
	return gpr[0xF];
}

void ExecuteOpcode()
{
	// Procced normally
	auto Procceed = [&](const bool jump = false)
	{
		if (!jump) ::pc += 2;
		std::this_thread::yield();
	};

	// Big Endian architecture, swap bytes
	const u16 opcode = _byteswap_ushort(vm::read<u16>(::pc));

	switch (getField<3>(opcode))
	{
	case 0x0:
	{
		// Class 0 instructions

		if (opcode == 0x00EE)
		{
			// Return
			if (sp == 0)
			{
				break;
			}

			::pc = ::stack[--sp];
			return Procceed(true);
		}
		else if (opcode == 0x00E0)
		{
			// Display clear
			std::memset(&gfxMemory[0], 0, 2048);
			DisplayDirty = true;
			_mm_mfence();
			return Procceed();
		}
		else
		{
			printf("Unimplemented RCA-1802 program call");
			break;
		}
	}
	case 0x1:
	{
		// Jump
		if (opcode % 2)
		{
			break;
		}

		::pc = opcode & 0x0FFF;
		return Procceed(true);
	}
	case 0x2:
	{
		// Call
		if (sp > 15)
		{
			// Overflow
			break;
		}

		if (opcode % 2)
		{
			// Unaligned operation
			break;
		}

		::stack[sp++] = std::exchange(pc, opcode & 0x0FFF) + 2;
		return Procceed(true);
	}
	case 0x3:
	{
		// Skip instruction conditional using immediate (if equal)
		const u8 reg = getField<2>(opcode);

		if (gpr[reg] == (opcode & 0xFF))
		{
			::pc += 4;
			return Procceed(true);
		}

		return Procceed();
	}
	case 0x4:
	{
		// Skip instruction conditional using immediate (if does not equal)
		const u8 reg = getField<2>(opcode);

		if (gpr[reg] != (opcode & 0xFF))
		{
			::pc += 4;
			return Procceed(true);
		}

		return Procceed();
	}
	case 0x5:
	{
		// Class 5 instructions

		if ((opcode & 0xF) != 0)
		{
			// Unknown instruction
			break;
		}

		// Skip instruction conditional using register (if equal)
		const u8 reg = getField<2>(opcode);
		const u8 reg2 = getField<1>(opcode);

		if (gpr[reg] == gpr[reg2])
		{
			::pc += 4;
			return Procceed(true);
		}

		return Procceed();

	}
	case 0x6:
	{
		// Write immediate to regsiter
		const u8 reg = getField<2>(opcode);
		gpr[reg] = opcode & 0xFF;
		return Procceed();
	}
	case 0x7:
	{
		// Add immediate to regsiter
		const u8 reg = getField<2>(opcode);
		gpr[reg] += opcode & 0xFF;
		return Procceed();
	}
	case 0x8:
	{
		// Class 8 instrcutions
		switch (opcode & 0xF)
		{
		case 0x0:
		{
			// Assign register
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);

			gpr[reg] = gpr[reg2];
			return Procceed();
		}
		case 0x1:
		{
			// OR
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);

			gpr[reg] |= gpr[reg2];
			return Procceed();
		}
		case 0x2:
		{
			// AND
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);
			gpr[reg] &= gpr[reg2];
			return Procceed();
		}
		case 0x3:
		{
			// XOR
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);

			gpr[reg] ^= gpr[reg2];
			return Procceed();
		}
		case 0x4:
		{
			// ADD with carry (sets VF)
			// TODO: Optimize with assembly?
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);
			const u16 result = (u16)gpr[reg] + (u16)gpr[reg2];

			gpr[reg] = (u8)result;
			getVF() = (u8)((result >> 8) != 0);
			return Procceed();
		}
		case 0x5:
		{
			// SUB with carry (sets VF)
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);
			const u16 result = (u16)gpr[reg] - (u16)gpr[reg2];

			gpr[reg] = (u8)result;
			getVF() = (u8)((result >> 8) != 0);
			return Procceed();
		}
		case 0x6:
		{
			// Shift right by 0 (store LSB in VF)
			// TODO: Optimize with assembly
			const u8 reg = getField<2>(opcode);
			const u8 result = gpr[reg];
			gpr[reg] = result >> 1;
			getVF() = result & 1;
			return Procceed();
		}
		case 0x7:
		{
			// SUB with carry (opposite) (sets VF)
			// TODO: Optimize with assembly
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);
			const u16 result = (u16)gpr[reg2] - (u16)gpr[reg];

			gpr[reg] = (u8)result;
			getVF() = (u8)((result >> 8) != 0);
			return Procceed();
		}
		case 0x8:
		case 0x9:
		case 0xA:
		case 0xB:
		case 0xC:
		case 0xD: break; // Illegal instruction
		case 0xE:
		{
			// Shift left by 0 (store MSB in VF)
			// TODO: Optimize with assembly
			const u8 reg = getField<2>(opcode);
			const u8 result = gpr[reg];

			gpr[reg] = result << 1;
			getVF() = (result & (1 << 7)) >> 7;
			return Procceed();
		}
		case 0xF: break; // Illegal instruction
		}

		break;
	}
	case 0x9:
	{
		// Class 9 instructions

		if ((opcode & 0xF) != 0)
		{
			// Unknown instruction
			break;
		}

		// Skip instruction conditional using register (if not equal)
		const u8 reg = getField<2>(opcode);
		const u8 reg2 = getField<1>(opcode);

		if (gpr[reg] != gpr[reg2])
		{
			::pc += 4;
			return Procceed(true);
		}

		return Procceed();
	}
	case 0xA:
	{
		// Set memory pointer immediate
		::index = opcode & 0xFFF;
		return Procceed();
	}
	case 0xB:
	{
		// Jump with offset using register 0
		::pc = ((opcode + gpr[0]) & 0xFFF);
		return Procceed(true);
	}
	case 0xC:
	{
		// Set random number with bit mask
		const u8 reg = getField<2>(opcode);
		gpr[reg] = std::rand() & (opcode & 0xFF);
		return Procceed();
	}
	case 0xD:
	{
		// Draw call sprite
		const u8 reg = getField<2>(opcode);
		const u8 reg2 = getField<1>(opcode);
		const u8 size = getField<0>(opcode);

		// Get the start of sprite location in vram
		u8* vbuffer = ::gfxMemory + (gpr[reg] & 0x3f) + ((gpr[reg2] & 0x1f) * 32);

		// Get the start of the sprite in ram
		u8* src = vm::ptr<u8>(::index);

		// Packed row of pixels
		u8 pvalue;

		// Assume no pixels unset at start
		getVF() = 0;

		//NOTE: This draws in XOR mode! - meaning the pixel color is flipped anytime any bit is 1

		for (u32 row = 0; row < size; row++)
		{
			pvalue = *(src + row);

			if (pvalue)
			{
				// Update the screen in case any pixel is flipped
				DisplayDirty = true;
			}

			// Unpack bits into pixels
			for (u32 i = 0; i < 8; i++)
			{
				// Trick: substruct from 0 to obtain grayscale
				const u8 pixel = 0u - ((pvalue >> i) & 0x1);

				// Obtain pointer to the pixel dst
				auto ptr = vbuffer + i + (row * 64);
			
				// Perform tests related to its color
				if (pixel)
				{
					if (*ptr != 0)
					{
						// Pixel unset
						getVF() = 1;
					}

					// Update pixel
					*ptr ^= pixel;
				}
			}
		}

		// Stores must be visible to all threads after this instruction 
		_mm_mfence();

		return Procceed();
	}
	case 0xE:
	{
		// Class 14 instructions

		switch (opcode & 0xFF)
		{
		case 0x9E:
		{
			// Skip instruction if specified key is pressed
			const u8 reg = getField<2>(opcode);

			const bool pressed = input::GetKeyState(gpr[reg] & 0xf);

			if (pressed)
			{
				::pc += 4;
				return Procceed(true);
			}

			return Procceed();
		}
		case 0xA1:
		{
			// Skip instruction if specified key is not pressed
			const u8 reg = getField<2>(opcode);

			const bool pressed = input::GetKeyState(gpr[reg] & 0xf);

			if (!pressed)
			{
				::pc += 4;
				return Procceed(true);
			}

			return Procceed();
		}
		default: break; // Illegal instruction
		}

		break;
	}
	case 0xF:
	{
		// Class 15 instructions
		switch (opcode & 0xFF)
		{
		case 0x07:
		{
			// Get delay timer
			const u8 reg = getField<2>(opcode);
			gpr[reg] = ::delay_timer;
			return Procceed();
		}
		case 0x0A:
		{
			// Get next key press (blocking)
			//char n;
			//u8 keyid;

			//do
			//{ 
			//	std::cin >> n;
			//} while (!input::getKeyPress(n, keyid));
			__debugbreak();

			//const u8 reg = getField<2>(opcode);
			//gpr[reg] = keyid;
			return Procceed();
		}
		case 0x15:
		{
			// Set dealy timer
			const u8 reg = getField<2>(opcode);
			time_m.lock();
			::delay_timer = gpr[reg];
			time_m.unlock();
			return Procceed();
		}
		case 0x18:
		{
			// Set sound timer
			const u8 reg = getField<2>(opcode);
			time_m.lock();
			::sound_timer = gpr[reg];
			time_m.unlock();
			return Procceed();
		}
		case 0x1E:
		{
			// Add register value to mem pointer
			const u8 reg = getField<2>(opcode);
			::index += gpr[reg];
			return Procceed();
		}
		case 0x33:
		{
			// Convert register view to decimal and store it in memory
			struct decimal_t
			{
				u8 i[3];
			};

			const u8 rvalue = gpr[getField<2>(opcode)];
			auto out = vm::ref<decimal_t>(::index);

			// Try to extract the first digit using optimized path
			// Knowing it can only be 2,1,0
			if (rvalue >= 200)
			{
				out.i[0] = 2;
			}
			else if (rvalue >= 100)
			{
				out.i[0] = 1;
			}
			else
			{
				out.i[0] = 0;
			}

			out.i[1] = (rvalue % 100) / 10;
			out.i[2] = rvalue % 10;
			return Procceed();
		}
		case 0x55:
		{
			// Reg array store 
			const u8 max_reg = getField<2>(opcode);
			std::memcpy(vm::ptr<u8>(::index), &gpr[0], max_reg);
			return Procceed();
		}
		case 0x65:
		{
			// Reg array load
			const u8 max_reg = getField<2>(opcode);
			std::memcpy(&gpr[0], vm::ptr<u8>(::index), max_reg);
			return Procceed();
		}
		default: break;
		}
		break;
	}
	}

	std::printf("Unimplemented/invalid instruction:0x%x", opcode);
	//Exit();
}