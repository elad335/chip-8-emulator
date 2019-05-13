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
	genTable<asm_insts>(&g_state.ops[0]);
	g_state.reset();

	do
	{
		static const auto failure = []()
		{
			std::printf("Failure opening binary file!");
			std::this_thread::sleep_for(std::chrono::seconds(5));
			return 0;
		};

		std::ifstream file("../pong.rom", std::ifstream::binary);

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

		file.read(g_state.ptr<char>(0x200), length);
	}
	while (0);

	std::thread hwTimers(timerJob);

	while (true)
	{
		asm_insts::entry(&g_state);

		switch (g_state.emu_flags)
		{
		case emu_flag::display_update:
		{
			// TODO
			system("Cls");
			static char buf[sizeof(emu_state_t::gfxMemory) + 32];

			for (u32 i = 0; i < 32; i++)
			{
				for (u32 j = 0; j < 64; j++)
				{
					buf[i * 65 + j] = (g_state.gfxMemory[i * 64 + j] != 0 ? '0' : ' ');
				}

				buf[i * 65 + 64] = '\n';
			}

			buf[65 * 31 + 64] = '\0';
			printf(+buf);

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
