// Chip-8 emulator
//
#include "render.h"
#include "emucore.h"
#include "hwtimers.h"
#include "input.h"
#include "ASMJIT/AsmInterpreter.h"
#include <iostream>
#include <thread>
#include <string_view>
#include <filesystem>
#include <cstdio>
#include <immintrin.h>

namespace fs = std::filesystem;

void emu_state::load_exec()
{
	wchar_t display_buf[32 * 65]{};

	std::vector<std::wstring> files;
	std::vector<std::wstring_view> names;

	static const auto failure = []()
	{
		std::printf("Failure opening binary file!");
		std::this_thread::sleep_for(std::chrono::seconds(5));
		exit(0);
	};

	// Dummy error code to prevent exceptions (errors handled as part of files.empty() check)
	static std::error_code ec;

	for (auto& e : fs::directory_iterator("../roms/", ec))
	{
		if (e.is_regular_file())
		{
			files.emplace_back(e.path().native());
		}
	}

	// Min index for super chip 8 images (current index)
	size_t s8_min = files.size();

	for (auto& e : fs::directory_iterator("../roms/super/", ec))
	{
		if (e.is_regular_file())
		{
			files.emplace_back(e.path().native());
		}
	}

	if (files.empty())
	{
		return failure();
	}

	// First line to use
	constexpr u32 line_offset = 2;

	for (const auto& str : files)
	{
		size_t start = str.find_last_of('/');
		names.emplace_back(str.c_str() + start + 1);
	}

	for (u32 j = 0; j < 32; j++)
	{
		std::wmemset(display_buf + (j * 65), ' ', 64);

		if (j >= line_offset && j - line_offset < names.size())
		{
			// Copy file name without null term
			const auto& sv = names[j - line_offset];
			std::wmemcpy(display_buf + (j * 65) + 2, sv.data(), sv.size());
		}

		display_buf[j * 65 + 64] = '\n';
	}

	{
		const std::wstring_view sv = L"*Chip-8 emulator by elad";
		std::wmemcpy(display_buf + 0, sv.data(), sv.size() - 1);
	}
	display_buf[31 * 65 + 64] = '\0';
	display_buf[line_offset * 65] = '>';
	system("Cls");
	wprintf(display_buf);

	const wchar_t* rom = {};
	for (size_t index = 0;;)
	{
		if (input::TestKeyState(VK_RETURN))
		{
			// Enter pressed, rom selected
			rom = files[index].c_str();
			is_super = index >= s8_min;
			break;
		}
		bool update = false;

		if (input::TestKeyState(VK_UP, 0x57))
		{
			Sleep(50); // Hack, simulate key press events
			display_buf[(line_offset + index) * 65] = ' ';
			update = true;

			if (index != 0)
			{
				index--;
			}
			else
			{
				index = names.size() - 1;
			}
		}
		else if (input::TestKeyState(VK_DOWN, 0x53))
		{
			Sleep(50); // Hack, simulate key press events
			display_buf[(line_offset + index) * 65] = ' ';
			update = true;
			index++;
			index %= names.size();
		}

		if (update)
		{
			display_buf[(line_offset + index) * 65] = '>';
			system("Cls");
			wprintf(display_buf);
		}

		Sleep(2);
	}

	system("Cls");
	std::basic_ifstream<u8> file(rom, std::ifstream::binary);

	if (!file) 
	{
		hwBpx();
		return failure();
	}

	file.seekg(0, file.end);
	std::streamoff length = file.tellg();
	file.seekg(0, file.beg);

	// Sanity checks for file size
	if (!length || length > (4096 - 512))
	{
		file.close();
		return failure();
	}

	// Executable load start address is 0x200
	file.read(g_state.ptr<u8>(0x200), zext<std::streamsize>(length));
}

void handle_all_errors()
{
	ShowWindow(GetConsoleWindow(), SW_SHOW);
	printf("%s (last opcode = 0x%04x at 0x%04x)", g_state.last_error, get_be_data<u16>(g_state.read<u16>(g_state.pc)), g_state.pc);
	std::this_thread::sleep_for(std::chrono::seconds(10));
}

int main()
{
	// Load rom, reset state and compile the instruction table
	g_state.reset();

	// Open graphics window and close console
	InitWindow();

	// Asm code now takes over
	asm_insts::entry();

	// Print last error if there is one
	handle_all_errors();
	return 0;
}
