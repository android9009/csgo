#include "render.hpp"
#include "memory.hpp"
#include "../sdk/interfaces/interfaces.hpp"
#include <windows.h>
#include <string>
#include <vector>
namespace Render {
static unsigned long font = 0;
static void SetDrawColor(Color color) {
    using Fn = void (__thiscall*)(void*, int, int, int, int);
    if (Interfaces::Surface) if (auto fn = MEM::GetVFunc<Fn>(Interfaces::Surface, 15))
        fn(Interfaces::Surface, color.r, color.g, color.b, color.a);
}
void CreateFonts() {
    if (!Interfaces::Surface) return;
    using CreateFont = unsigned long (__thiscall*)(void*);
    using SetGlyph = bool (__thiscall*)(void*, unsigned long, const char*, int, int, int, int, int, int, int);
    auto create = MEM::GetVFunc<CreateFont>(Interfaces::Surface, 71);
    auto glyph = MEM::GetVFunc<SetGlyph>(Interfaces::Surface, 72);
    if (create && glyph) {
        font = create(Interfaces::Surface);
        glyph(Interfaces::Surface, font, "Tahoma", 13, 500, 0, 0, 0x200, 0, 0);
    }
}
void Line(int x1, int y1, int x2, int y2, Color color) {
    if (!Interfaces::Surface) return;
    SetDrawColor(color);
    using Fn = void (__thiscall*)(void*, int, int, int, int);
    if (auto fn = MEM::GetVFunc<Fn>(Interfaces::Surface, 19)) fn(Interfaces::Surface, x1, y1, x2, y2);
}
void FilledRect(int x, int y, int width, int height, Color color) {
    if (!Interfaces::Surface) return;
    SetDrawColor(color);
    using Fn = void (__thiscall*)(void*, int, int, int, int);
    if (auto fn = MEM::GetVFunc<Fn>(Interfaces::Surface, 16)) fn(Interfaces::Surface, x, y, x + width, y + height);
}
void OutlinedRect(int x, int y, int width, int height, Color color) {
    if (!Interfaces::Surface) return;
    SetDrawColor(color);
    using Fn = void (__thiscall*)(void*, int, int, int, int);
    if (auto fn = MEM::GetVFunc<Fn>(Interfaces::Surface, 18)) fn(Interfaces::Surface, x, y, x + width, y + height);
}
void Text(int x, int y, Color color, std::string_view text) {
    if (!Interfaces::Surface || !font || text.empty()) return;
    using SetFont = void (__thiscall*)(void*, unsigned long);
    using SetColor = void (__thiscall*)(void*, int, int, int, int);
    using SetPos = void (__thiscall*)(void*, int, int);
    using Print = void (__thiscall*)(void*, const wchar_t*, int, int);
    if (auto fn = MEM::GetVFunc<SetFont>(Interfaces::Surface, 23)) fn(Interfaces::Surface, font);
    if (auto fn = MEM::GetVFunc<SetColor>(Interfaces::Surface, 25)) fn(Interfaces::Surface, color.r, color.g, color.b, color.a);
    if (auto fn = MEM::GetVFunc<SetPos>(Interfaces::Surface, 26)) fn(Interfaces::Surface, x, y);
    const std::string value(text);
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 1) return;
    std::vector<wchar_t> wide(static_cast<size_t>(size));
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), size);
    if (auto fn = MEM::GetVFunc<Print>(Interfaces::Surface, 28)) fn(Interfaces::Surface, wide.data(), size - 1, 0);
}
}
