// Chip-8 emulator
//

#include <cstdio>
#include <immintrin.h>
#include "emucore.h"
#include "hwtimers.h"
#include "input.h"
#include "InterpreterTableGenereator.h"
#include "ASMJIT/AsmInterpreter.h"
#include <iostream>
#include <thread>

int main()
{
	g_state.reset();
	genTable<asm_insts>(&g_state.ops[0]);

	{
		std::ifstream file("../pong.rom", std::ifstream::binary);

		static auto failure = [&]()
		{
			std::printf("Failure opening binary file!");
			std::this_thread::sleep_for(std::chrono::seconds(5));
			return 0;
		};

		if (!file) 
		{
			return failure();
		}

		file.seekg(0, file.end);
		int length = file.tellg();
		file.seekg(0, file.beg);

		// Sanity checks for file size
		if (!length || length > 4096 - 512)
		{
			file.close();
			return failure();
		}

		file.read(vm::ptr<char>(0x200), length);
		file.close();
	}

	std::thread hwTimers(timerJob);

	while (true)
	{
		asm_insts::entry((void*)&g_state);

		check_label: // I can put here a loop instead but it's uglier
 
		switch (g_state.emu_flags)
		{
		case emu_flag::fallback:
		{
			g_state.emu_flags &= ~emu_flag::fallback;
			g_state.OpcodeFallback();

			if (g_state.emu_flags)
			{
				goto check_label; // Intrunction can change flags, recheck
			}

			continue;
		}
		case emu_flag::display_update:
		{
			// TODO
			system("Cls");
			for (u32 i = 0; i < 32; i++)
			{
				for (u32 j = 0; j < 64; j++)
				{
					printf(g_state.gfxMemory[i * 64 + j] != 0 ? "{}" : " ");
				}
				printf("\n");
			}

			g_state.emu_flags &= ~emu_flag::display_update;
			continue;
		}
		case emu_flag::clear_screan:
		{
			system("Cls");
			g_state.emu_flags &= ~emu_flag::clear_screan;
			continue;
		}
		case emu_flag::illegal_operation:
		{
			hwBpx();
		}
		default:
		{
			UNREACHABLE();
		}
		}

	}

	// Unreachable at the moment
	hwTimers.join();
	return 0;
}
