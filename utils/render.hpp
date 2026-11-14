#pragma once
#include "../math/vector.hpp"
#include "../math/color.hpp"
#include <string_view>
namespace Render {
    void CreateFonts();
    void Line(int x1, int y1, int x2, int y2, Color color);
    void FilledRect(int x, int y, int width, int height, Color color);
    void OutlinedRect(int x, int y, int width, int height, Color color);
    // Vertical (top->bottom) or horizontal (left->right) gradient built from 1px strips.
    void GradientRect(int x, int y, int width, int height, Color from, Color to, bool horizontal = false);
    void Text(int x, int y, Color color, std::string_view text);
}
