#include "input.h"
#include "emucore.h"

emu_state_t g_state;

static const u8 fontset[80] =
{ 
	0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
	0x20, 0x60, 0x20, 0x20, 0x70, // 1
	0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
	0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
	0x90, 0x90, 0xF0, 0x10, 0x10, // 4
	0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
	0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
	0xF0, 0x10, 0x20, 0x40, 0x40, // 7
	0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
	0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
	0xF0, 0x90, 0xF0, 0x90, 0x90, // A
	0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
	0xF0, 0x80, 0x80, 0x80, 0xF0, // C
	0xE0, 0x90, 0x90, 0x90, 0xE0, // D
	0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
	0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

void emu_state_t::reset()
{
	std::memset(memBase, 0xAA, sizeof(memBase));
	std::memcpy(memBase, fontset, sizeof(fontset));
	std::memset(gfxMemory, 0, sizeof(gfxMemory));
	std::memset(gpr, 0, sizeof(gpr));
	std::memset(stack, 0, sizeof(stack));
	this->ref<u16>(4096) = 0xFFFF; // Instruction flow guard
	sp = 0;
	pc = 0x200;
	index = 0;
	timers.store({});
}

u8& emu_state_t::getVF()
{
	// The 15 register
	return gpr[0xF];
}

// Execute one instruction for fallback (debugging only)
void emu_state_t::OpcodeFallback()
{
	// Procced normally
	const auto Procceed = [this](const bool jump = false)
	{
		if (!jump) pc += 2;
		std::this_thread::yield();
	};

	// Big Endian architecture, swap bytes
	const u16 opcode = this->read<be_t<u16>>(pc);

	switch (getField<3>(opcode))
	{
	case 0x0:
	{
		// Class 0 instructions

		if (opcode == 0x00EE)
		{
			// RET: Return
			if (sp == 0)
			{
				break;
			}

			pc = stack[--sp];
			return Procceed(true);
		}
		else if (opcode == 0x00E0)
		{
			// CLS: Display clear
			std::memset(&gfxMemory[0], 0, 2048);
			emu_flags |= emu_flag::clear_screan;
			_mm_sfence();
			return Procceed();
		}
		else
		{
			// SYS
			printf("Unimplemented RCA-1802 program call");
			break;
		}
	}
	case 0x1:
	{
		// JP: Jump
		if (opcode % 2)
		{
			break;
		}

		pc = opcode & 0x0FFF;
		return Procceed(true);
	}
	case 0x2:
	{
		// CALL
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

		stack[sp++] = std::exchange(pc, opcode & 0x0FFF) + 2;
		return Procceed(true);
	}
	case 0x3:
	{
		// SE(i): Skip instruction conditional using immediate (if equal)
		const u8 reg = getField<2>(opcode);

		if (gpr[reg] == (opcode & 0xFF))
		{
			pc += 4;
			return Procceed(true);
		}

		return Procceed();
	}
	case 0x4:
	{
		// SNE(i): Skip instruction conditional using immediate (if does not equal)
		const u8 reg = getField<2>(opcode);

		if (gpr[reg] != (opcode & 0xFF))
		{
			pc += 4;
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

		// SE: Skip instruction conditional using register (if equal)
		const u8 reg = getField<2>(opcode);
		const u8 reg2 = getField<1>(opcode);

		if (gpr[reg] == gpr[reg2])
		{
			pc += 4;
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
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);
			const u16 result = (u16)gpr[reg] + (u16)gpr[reg2];

			// Set carry as if greater than max
			if (reg == 15) __debugbreak();
			getVF() = (u8)(result >> 8);
			gpr[reg] = (u8)result;
			return Procceed();
		}
		case 0x5:
		{
			// SUB with carry (sets VF)
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);
			const u16 result = (u16)gpr[reg] - (u16)gpr[reg2];

			// Set sign as VF
			if (reg == 15) __debugbreak();
			getVF() = (u8)(result >> 15);
			gpr[reg] = (u8)result;
			return Procceed();
		}
		case 0x6:
		{
			// Shift right by 0 (store LSB in VF)
			const u8 reg = getField<2>(opcode);
			u8 result = gpr[reg];

			if (reg == 15) __debugbreak();
			getVF() = result & 1;
			gpr[reg] = result >>= 1;
			return Procceed();
		}
		case 0x7:
		{
			// Reverse SUB with carry (sets VF)
			const u8 reg = getField<2>(opcode);
			const u8 reg2 = getField<1>(opcode);
			const u16 result = (u16)gpr[reg2] - (u16)gpr[reg];

			// Set sign as VF
			if (reg == 15) __debugbreak();
			getVF() = (u8)(result >> 15) ^ 1;
			gpr[reg] = (u8)result;
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
			// Shift left by 1 (store MSB in VF)
			// TODO: Optimize with assembly
			const u8 reg = getField<2>(opcode);
			const u8 result = gpr[reg];

			if (reg == 15) __debugbreak();
			getVF() = result >> 7;
			gpr[reg] = result << 1;
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

		// SNE: Skip instruction conditional using register (if not equal)
		const u8 reg = getField<2>(opcode);
		const u8 reg2 = getField<1>(opcode);

		if (gpr[reg] != gpr[reg2])
		{
			pc += 4;
			return Procceed(true);
		}

		return Procceed();
	}
	case 0xA:
	{
		// Set memory pointer immediate
		index = opcode & 0xFFF;
		return Procceed();
	}
	case 0xB:
	{
		// Jump with offset using register 0
		pc = ((opcode + gpr[0]) & 0xFFF);
		return Procceed(true);
	}
	case 0xC:
	{
		// RND: Set random number with bit mask
		const u8 reg = getField<2>(opcode);
		gpr[reg] = std::rand() & (opcode & 0xFF);
		return Procceed();
	}
	case 0xD:
	{
		// DRW: Draw call sprite
		//NOTE: framebuffer layout: swizzled buffer
		// offset 15 bits long : [1bit cleared][5 bits y][1bit cleared][6 bits x]

		//const u8 reg = getField<2>(opcode);
		//const u8 reg2 = getField<1>(opcode);
		const u8 size = getField<0>(opcode);

		// Get the start of sprite location in vram
		size_t offset = (gpr[getField<2>(opcode)] & 0x3f) + ((gpr[getField<1>(opcode)] & 0x1f) * y_shift);

#ifdef  DEBUG_INSTS
		if ((gpr[getField<2>(opcode)] & ~0x3f) || (gpr[getField<1>(opcode)] & ~0x1f))
		{
			hwBpx();
		}
#endif

		// Get the start of the sprite in ram
		auto& src = this->ref<u8[]>(index);

		// Packed row of pixels
		u8 pvalue;

		// Assume no pixels unset at start
		getVF() = 0;

		//NOTE: This draws in XOR mode! - meaning the pixel color is flipped anytime any bit is 1

		for (u32 row = 0; row < size; row++, offset += y_shift)
		{
			pvalue = src[row];

			if (pvalue)
			{
				// Unpack bits into pixels
				for (u32 i = 0; i < 8; i++, offset++)
				{
					// Do nothing if zero
					if ((pvalue >> (7 - i)) & 0x1)
					{
						// Wrap around x and y axis
						offset &= xy_mask;

						// Obtain pointer to the pixel dst
						auto& pix = gfxMemory[offset];

						if (pix != 0)
						{
							// Pixel unset
							getVF() = 1;
						}

						// Update pixel
						pix ^= 0xff;
					}
				}

				offset -= 8;
			}
		}

		// Update the screen
		emu_flags |= emu_flag::display_update;
		return Procceed();
	}
	case 0xE:
	{
		// Class 14 instructions

		switch (opcode & 0xFF)
		{
		case 0x9E:
		{
			// SKP: Skip instruction if specified key is pressed
			const u8 reg = getField<2>(opcode);

			const bool pressed = input::GetKeyState(gpr[reg] & 0xf);

			if (pressed)
			{
				pc += 4;
				return Procceed(true);
			}

			return Procceed();
		}
		case 0xA1:
		{
			// SKNP: Skip instruction if specified key is not pressed
			const u8 reg = getField<2>(opcode);

			const bool pressed = input::GetKeyState(gpr[reg] & 0xf);

			if (!pressed)
			{
				pc += 4;
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
			gpr[reg] = timers.load().delay;
			return Procceed();
		}
		case 0x0A:
		{
			// Get next key press (blocking)
			u8 keyid = input::WaitForPress();

			const u8 reg = getField<2>(opcode);
			gpr[reg] = keyid;
			return Procceed();
		}
		case 0x15:
		{
			// Set dealy timer
			const u8 value = gpr[getField<2>(opcode)];

			atomic::op(timers, [&value](time_control_t& state)
			{
				state.delay = value;
			});

			return Procceed();
		}
		case 0x18:
		{
			// Set sound timer
			const u8 value = gpr[getField<2>(opcode)];

			atomic::op(timers, [&value](time_control_t& state)
			{
				state.sound = value;
			});

			return Procceed();
		}
		case 0x1E:
		{
			// Add register value to mem pointer
			const u8 reg = getField<2>(opcode);
			index += (u32)gpr[reg];
			return Procceed();
		}
		case 0x29:
		{
			// Set mem pointer to char
			const u8 reg = getField<2>(opcode);
			index = (gpr[reg] & 0xF) * 5;
			return Procceed();
		}
		case 0x33:
		{
			// Convert register view to decimal and store it in memory
			const u8 rvalue = gpr[getField<2>(opcode)];
			auto& out = this->ref<u8[]>(index);

			// Try to extract the first digit using optimized path
			// Knowing it can only be 2,1,0
			if (rvalue < 200)
			{
				out[0] = rvalue < 100 ? 0 : 1;
			}
			else
			{
				out[0] = 2;
			}

			out[1] = (rvalue % 100) / 10;
			out[2] = rvalue % 10;
			return Procceed();
		}
		case 0x55:
		{
			// Reg array store 
			const u8 max_reg = getField<2>(opcode);
			std::memcpy(this->ptr<u8>(index), &gpr[0], max_reg);
			return Procceed();
		}
		case 0x65:
		{
			// Reg array load
			const u8 max_reg = getField<2>(opcode);
			std::memcpy(&gpr[0], this->ptr<u8>(index), max_reg);
			return Procceed();
		}
		default: break;
		}
		break;
	}
	}

	if (opcode == 0xFFFF && pc == 0x1000)
	{
		// Handle potential instruction address overflow
		// TODO: logging of such event
		pc = 0x0;
		return;
	}

	emu_flags |= emu_flag::illegal_operation;
}
