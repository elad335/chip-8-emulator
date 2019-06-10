#include "emucore.h"
#include "hwtimers.h"
#include <atomic>

void timerJob()
{
	while (!g_state.terminate)
	{
		Sleep(16);

		bool result = false;

		// Decrement sound and delay timers if necessary
		for (u16 old = (u16)g_state.timers.data, state = (std::atomic_thread_fence(std::memory_order_acquire), old);;)
		{
			// Multipliers for delay, sound fields
			static const auto m_delay = [](const u16 val) -> u16 { return val * 0x100; };
			static const auto m_sound = [](const u16 val) -> u16 { return val * 0x1; };

			if (state & m_delay(0xFF))
			{
				state -= m_delay(1);

				if (state & m_sound(0xFF) && 
					((state -= m_sound(1)) & m_sound(0xFF)) == m_sound(1))
				{
					result = true;
				}
			}
			else if (state & m_sound(0xFF))
			{
				if (((state -= m_sound(1)) & m_sound(0xFF)) == m_sound(1))
				{
					result = true;
				}
			}
			else
			{
				// Nothing to do
				break;
			}

			const u16 new_data = (u16)_InterlockedCompareExchange16(&g_state.timers.data, (short)state, (short)old);

			if (new_data == old)
			{
				// Storing success 
				break;
			}

			// Refrash data
			old = state = new_data;
		}

		if (result)
		{
			// Sound timer is zero, beep
			std::cout << "\a";
		}
	}
}
