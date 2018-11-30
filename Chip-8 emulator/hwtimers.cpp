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

        // Decrement sound and delay timers if necessary
        if (atomic::op(registers::timers.time, [](time_control::type& state)
        {
            if (state.delay) state.delay--;

            return state.sound && --state.sound == 0;
        }))
        {
            // Sound timer is zero, beep
            std::cout << "\a";
        }
    }
}