#pragma once
// Base of the cheat, taken from the main branch (Live build): memory helpers,
// module list, patterns/offsets, netvars, interfaces and entity classes.
// No feature code lives here; features include this header and use HACKS.
class c_game_color;
class c_base_entity;
class c_cs_player;
class c_hacks;

#define INLINE __forceinline

// STL
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Platform
#include <windows.h>
#include <d3d9.h>
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

// main obfuscates literals and constants at compile time; this base keeps the
// same call sites but leaves the values in the clear.
#define XOR(s) std::string(s)
#define CXOR(s) (s)
#define XORN(n) (n)
#define WINCALL(func) func

// SDK
#include "hash.hpp"
#include "color.hpp"
#include "memory.hpp"
#include "math.hpp"
#include "defines.hpp"
#include "sdk_misc.hpp"
#include "misc_structs.hpp"
#include "structs.hpp"
#include "interfaces.hpp"
#include "hacks.hpp"
#include "netvars.hpp"
#include "offsets.hpp"
#include "entities.hpp"

#undef min
#undef max

#define TIME_TO_TICKS(t) ((int)(0.5f + (float)(t) / HACKS->global_vars->interval_per_tick))
#define TICKS_TO_TIME(t) (HACKS->global_vars->interval_per_tick * (t))
