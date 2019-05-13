#pragma once
#include "utils.h"
#include "emucore.h"

template<typename Ops>
static void genTable(typename Ops::func_t* table)
{
	// Fill table with all opcodes possible (Ops must contain all of them)
	for (u32 op = 0; op < UINT16_MAX + 1; op++)
	{
		switch (getField<3>(op))
		{
		case 0x0:
		{
			// Class 0 instructions

			if (op == 0x00EE)
			{
				// RET: Return
				table[op] = Ops::RET;
				continue;
			}
			else if (op == 0x00E0)
			{
				// CLS: Display clear
				table[op] = Ops::CLS;
				continue;
			}
			else
			{
				// SYS: TODO
				break;
			}
		}
		case 0x1:
		{
			// JP: Jump
			if (op % 2)
			{
				// Unaligned
				break;
			}

			table[op] = Ops::JP;
			continue;
		}
		case 0x2:
		{
			// CALL
			if (op % 2)
			{
				// Unaligned
				break;
			}

			table[op] = Ops::CALL;
			continue;
		}
		case 0x3:
		{
			// SE(i): Skip instruction conditional using immediate (if equal)
			table[op] = Ops::SEi;
			continue;
		}
		case 0x4:
		{
			// SNE(i): Skip instruction conditional using immediate (if does not equal)
			table[op] = Ops::SNEi;
			continue;
		}
		case 0x5:
		{
			// Class 5 instructions

			if ((op & 0xF) != 0)
			{
				// Unknown instruction
				break;
			}

			// SE: Skip instruction conditional using register (if equal)
			table[op] = Ops::SE;
			continue;

		}
		case 0x6:
		{
			// Write immediate to regsiter
			table[op] = Ops::WRI;
			continue;
		}
		case 0x7:
		{
			// Add immediate to regsiter
			table[op] = Ops::ADDI;
			continue;
		}
		case 0x8:
		{
			// Class 8 instrcutions
			switch (op & 0xF)
			{
			case 0x0:
			{
				// Assign register
				table[op] = Ops::ASS;
				continue;
			}
			case 0x1:
			{
				// OR
				table[op] = Ops::OR;
				continue;
			}
			case 0x2:
			{
				// AND
				table[op] = Ops::AND;
				continue;
			}
			case 0x3:
			{
				// XOR
				table[op] = Ops::XOR;
				continue;
			}
			case 0x4:
			{
				// ADD with carry (sets VF)

				if (getField<2>(op) == 15)
				{
					// VF destination is unsupported as its UB
					break;
				}

				table[op] = Ops::ADD;
				continue;
			}
			case 0x5:
			{
				// SUB with carry (sets VF)
				if (getField<2>(op) == 15)
				{
					break;
				}

				table[op] = Ops::SUB;
				continue;
			}
			case 0x6:
			{
				// Shift right by 1 (store LSB in VF)
				if (getField<2>(op) == 15)
				{
					break;
				}

				table[op] = Ops::SHR;
				continue;
			}
			case 0x7:
			{
				// Reverese SUB with carry (sets VF)

				if (getField<2>(op) == 15)
				{
					break;
				}

				table[op] = Ops::RSB;
				continue;
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
				if (getField<2>(op) == 15)
				{
					break;
				}

				table[op] = Ops::SHL;
				continue;
			}
			case 0xF: break; // Illegal instruction
			}

			break;
		}
		case 0x9:
		{
			// Class 9 instructions

			if ((op & 0xF) != 0)
			{
				// Unknown instruction
				break;
			}

			// SNE: Skip instruction conditional using register (if not equal)
			table[op] = Ops::SNE;
			continue;
		}
		case 0xA:
		{
			// Set memory pointer immediate
			table[op] = Ops::SetIndex;
			continue;
		}
		case 0xB:
		{
			// Jump with offset using register 0
			table[op] = Ops::JPr;
			continue;
		}
		case 0xC:
		{
			// RND: Set random number with bit mask
			table[op] = Ops::RND;
			continue;
		}
		case 0xD:
		{
			// DRW: Draw call sprite
			table[op] = Ops::DRW;
			continue;
		}
		case 0xE:
		{
			// Class 14 instructions

			switch (op & 0xFF)
			{
			case 0x9E:
			{
				// SKP: Skip instruction if specified key is pressed
				table[op] = Ops::SKP;
				continue;
			}
			case 0xA1:
			{
				// SKNP: Skip instruction if specified key is not pressed
				table[op] = Ops::SKNP;
				continue;
			}
			default: break; // Illegal instruction
			}

			break;
		}
		case 0xF:
		{
			// Class 15 instructions
			switch (op & 0xFF)
			{
			case 0x07:
			{
				// Get delay timer
				table[op] = Ops::GetD;
				continue;
			}
			case 0x0A:
			{
				// Get next key press (blocking)
				table[op] = Ops::GetK;
				continue;
			}
			case 0x15:
			{
				// Set dealy timer
				table[op] = Ops::SetD;
				continue;
			}
			case 0x18:
			{
				// Set sound timer
				table[op] = Ops::SetS;
				continue;
			}
			case 0x1E:
			{
				// Add register value to mem pointer
				table[op] = Ops::AddIndex;
				continue;
			}
			case 0x29:
			{
				// Set mem pointer to char
				table[op] = Ops::SetCh;
				continue;
			}
			case 0x33:
			{
				// Convert register view to decimal and store it in memory
				table[op] = Ops::STD;
				continue;
			}
			case 0x55:
			{
				// Reg array store 
				table[op] = Ops::STR;
				continue;
			}
			case 0x65:
			{
				// Reg array load
				table[op] = Ops::LDR;
				continue;
			}
			default: break;
			}
		}
		}

		table[op] = Ops::UNK;
	}

	table[0xFFFF] = Ops::guard;
};