// Chip-8 emulator
//

#include <cstdio>
#include <immintrin.h>
#include "emucore.h"
#include "hwtimers.h"

#include <iostream>
#include <thread>

int main()
{
	::resetRegisters();
	_mm_mfence();

	std::thread hwTimers(timerJob); hwTimers.detach();

    while (true)
	{
		::ExecuteOpcode();
	}

	return 0;
}
