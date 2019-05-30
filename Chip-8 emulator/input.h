#pragma once
#include "utils.h"

namespace input
{
	extern u8 keyIDs[16];

	template<typename Args>
	static bool TestKeyStateImpl(Args&& keyids)
	{
		return (!!(::GetKeyState((int)std::forward<Args>(keyids)) & 0x8000));
	}

	template<typename ... Args>
	static bool TestKeyState(Args&&... keyids)
	{
		return (TestKeyStateImpl(keyids) || ...);
	}

	u8 WaitForPress();
};