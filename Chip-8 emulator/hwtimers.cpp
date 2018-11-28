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

        // Unfortunatly, this operation cant be handled via atomic op alone
        std::lock_guard lock(time_m);

        if (registers::sound_timer.load(std::memory_order_relaxed))
        {
            if (--registers::sound_timer == 0)
            {
                // Beep
                std::cout << "\a";
            }
        }

        if (registers::delay_timer.load(std::memory_order_relaxed))
        {
            registers::delay_timer--;
        }
    }
}