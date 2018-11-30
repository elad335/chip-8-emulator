#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

#include "emucore.h"
#include "hwtimers.h"

void timerJob()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::microseconds( 16 ));

        if (atomic::op(registers::sound_timer, [](u8& state)
        {
            return state && --state == 0;
        }))
        {
            // Beep
            std::cout << "\a";
        }

        atomic::op(registers::delay_timer, [](u8& state)
        {
            if (state) state--;
        });
    }
}