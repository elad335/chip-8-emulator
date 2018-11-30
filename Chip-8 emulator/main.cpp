// Chip-8 emulator
//

#include <cstdio>
#include <immintrin.h>
#include "emucore.h"
#include "hwtimers.h"
#include "input.h"

#include <iostream>
#include <thread>

int main()
{
	resetRegisters();
	{
		std::ifstream file("C:/Users/elad/Desktop/pong.bin", std::ifstream::binary);

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
		::ExecuteOpcode();
	}

	return 0;
}
