#pragma once
#include "utils.h"

namespace input
{
	extern int keyIDs[16];
	bool GetKeyState(u8 keyid);
	u8 WaitForPress();
};