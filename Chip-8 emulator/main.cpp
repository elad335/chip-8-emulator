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
#include <filesystem>

namespace fs = std::filesystem;
static wchar_t display_buf[32 * 65]{};

static std::wstring ChooseExecutable()
{
	std::vector<std::wstring> files;
	std::vector<const wchar_t*> names;

	for (auto& e : fs::directory_iterator("../roms/"))
	{
		if (e.is_regular_file())
		{
			files.emplace_back(e.path().native());
		}
	}

	if (files.empty())
	{
		return {};
	}

	// First line to use
	constexpr u32 line_offset = 2;

	for (const auto& str : files)
	{
		size_t index = str.find_last_of('/');
		names.emplace_back(str.c_str() + index + 1);
	}

	for (int j = 0; j < 32; j++)
	{
		std::wmemset(display_buf + (j * 65), ' ', 64);

		if (j > 1 && j - 2 < names.size())
		{
			wcsncpy(display_buf + (j * 65) + 2, names[j - line_offset], 62);
		}

		display_buf[j * 65 + 64] = '\n';
	}

	display_buf[31 * 65 + 64] = '\0';
	display_buf[line_offset * 65] = '>';
	system("Cls");
	wprintf(+display_buf);

	constexpr u16 test_bit = 0x8000;

	for (int cur_index = 0;;)
	{
		if (::GetKeyState(VK_RETURN) & test_bit)
		{
			system("Cls");
			return std::move(files[cur_index]);
		}

		bool update = false;

		if (::GetKeyState(VK_UP) & test_bit || ::GetKeyState(0x57) & test_bit)
		{
			display_buf[(line_offset + cur_index) * 65] = ' ';
			update = true;
			cur_index++;
			cur_index %= names.size();
		}

		if (::GetKeyState(VK_DOWN) & test_bit || ::GetKeyState(0x53) & test_bit)
		{
			display_buf[(line_offset + cur_index) * 65] = ' ';
			update = true;

			if (cur_index != 0)
			{
				cur_index--;
			}
			else
			{
				cur_index = names.size() - 1;
			}
		}

		if (update)
		{
			display_buf[(line_offset + cur_index) * 65] = '>';
			system("Cls");
			wprintf(+display_buf);
		}
	}
}

int main()
{
	genTable<asm_insts>(g_state.ops);
	g_state.reset();

	do
	{
		static const auto failure = []()
		{
			std::printf("Failure opening binary file!");
			std::this_thread::sleep_for(std::chrono::seconds(5));
			return 0;
		};

		std::wstring path = ChooseExecutable();

		if (!path.size())
		{
			return failure();
		}

		std::basic_ifstream<u8> file(path.c_str(), std::ifstream::binary);

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

		file.read(g_state.ptr<u8>(0x200), length);
	}
	while (0);

	std::thread hwTimers(timerJob);

	while (true)
	{
		asm_insts::entry();

		switch (g_state.emu_flags)
		{
		case emu_flag::display_update:
		{
			// TODO
			system("Cls");

			for (u32 i = 0; i < 32; i++)
			{
				for (u32 j = 0; j < 64; j++)
				{
					display_buf[i * 65 + j] = (g_state.gfxMemory[i * emu_state_t::y_shift + j] != 0 ? '0' : ' ');
				}

				display_buf[i * 65 + 64] = '\n';
			}

			display_buf[65 * 31 + 64] = '\0';
			wprintf(+display_buf);

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
