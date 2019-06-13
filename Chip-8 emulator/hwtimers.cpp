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
		for (u16 old = g_state.timers.data, state = old;;)
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

			state = (u16)_InterlockedCompareExchange16((volatile short*)&g_state.timers.data, (short)state, (short)old);

			if (state == old)
			{
				// Storing success 
				break;
			}

			// Refresh data
			old = state;
		}

		if (result)
		{
			// Sound timer is zero, beep
			std::cout << "\a";
		}
	}
}
