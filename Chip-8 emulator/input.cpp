#include "input.h"

namespace input
{
	// 0-9 keys, A-F keys
	// This should be configurable in the future
	int keyIDs[16] = {0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46};

	inline int loadKeyID(u8 keyid)
	{
		return keyIDs[keyid];
	}

	bool GetKeyState(u8 keyid)
	{
		const int id = loadKeyID(keyid);
		return !!(::GetKeyState(id) & 0x8000);
	}

	u8 WaitForPress()
	{
		// May need perf tuning (use an OS's blocking method)
		while (true)
		{
			for (u32 i = 0; i < std::size(keyIDs); i++)
			{
				if (GetKeyState(i))
				{
					return i;
				}
			}

			std::this_thread::yield();
		}
	}
};