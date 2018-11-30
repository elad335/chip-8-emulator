#pragma once
#include "utils.h"

namespace input
{
    bool GetKeyState(u8 keyid);
    u8 WaitForPress();
};