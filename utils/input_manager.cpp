#include "input_manager.hpp"
#include <windows.h>
namespace InputManager {
static std::array<bool, 256> current{};
static std::array<bool, 256> previous{};
void Update() {
    previous = current;
    for (int key = 0; key < 256; ++key) current[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
}
bool Down(int key) { return key >= 0 && key < 256 && current[key]; }
bool Pressed(int key) { return key >= 0 && key < 256 && current[key] && !previous[key]; }
}
