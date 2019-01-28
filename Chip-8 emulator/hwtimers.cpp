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

		bool result = false;

        // Decrement sound and delay timers if necessary
        atomic::cond_op(registers::timers, [&result](time_control_t& state)
        {
			if (state.delay)
			{
				--state.delay;

				if (state.sound && --state.sound == 0)
				{
					result = true;
				}

				return true;
			}
			
			if (state.sound)
			{
				if (--state.sound == 0)
				{
					result = true;
				}

				return true;
			}

			// Cancel operation, no changes were done
			return false;

		});

		if (result)
        {
            // Sound timer is zero, beep
            std::cout << "\a";
        }
    }
}
