#include "input.h"

namespace input
{
	// See README.md for key mappings guide
	u8 keyIDs[16] = 
	{
		0x58, 0x31, 0x32, 0x33,
		0x51, 0x57, 0x45, 0x41,
		0x53, 0x44, 0x5A, 0x43,
		0x34, 0x52, 0x46, 0x56
	};

	static inline int loadKeyID(u8 keyid)
	{
		return keyIDs[keyid];
	};

	u8 WaitForPress()
	{
		// May need perf tuning (use an OS's blocking method)
		while (true)
		{
			for (size_t i = 0; i < std::size(keyIDs); i++)
			{
				if (TestKeyState(i))
				{
					return zext<u8>(i);
				}
			}

			std::this_thread::yield();
		}
	};
};