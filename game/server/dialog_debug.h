#pragma once

#include "cbase.h"

extern ConVar dialog_debug;

#define DIALOG_DEVMSG(...) do { if (dialog_debug.GetBool()) DevMsg(__VA_ARGS__); } while (0)
