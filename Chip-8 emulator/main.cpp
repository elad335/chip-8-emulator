// Chip-8 emulator
//

#include <cstdio>
#include <immintrin.h>
#include "render.h"
#include "emucore.h"
#include "hwtimers.h"
#include "input.h"
#include "InterpreterTableGenereator.h"
#include "ASMJIT/AsmInterpreter.h"
#include <iostream>
#include <thread>
#include <filesystem>

namespace fs = std::filesystem;

static std::wstring ChooseExecutable()
{
	wchar_t display_buf[32 * 65]{};

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

	for (int cur_index = 0;;)
	{
		if (input::TestKeyState(VK_RETURN))
		{
			system("Cls");
			return std::move(files[cur_index]);
		}

		bool update = false;

		if (input::TestKeyState(VK_UP, 0x57))
		{
			display_buf[(line_offset + cur_index) * 65] = ' ';
			update = true;
			cur_index++;
			cur_index %= names.size();
		}

		if (input::TestKeyState(VK_DOWN, 0x53))
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
	g_state.reset();

	// Wait and load executable choice
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

		// Executable load start address is 0x200
		file.read(g_state.ptr<u8>(0x200), length);
	}

	// Open graphics window and close console
	InitWindow();

	// Asm code now takes over
	asm_insts::entry();

	return 0;
}
