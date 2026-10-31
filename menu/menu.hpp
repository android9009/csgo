#pragma once

#include <Windows.h>
#include "gui.hpp"
#include <memory>

class c_menu {
public:
    bool m_opened = false;
    bool m_block_attack_until_release = false;

    void init();
    void render();
    void draw();
    void toggle() { m_opened = !m_opened; }
    bool is_opened() const { return m_opened; }
    // ФИКС: Блокируем атаки только когда меню открыто ИЛИ ждём отпускания кнопки
    bool blocks_attacks() const { return m_opened || m_block_attack_until_release; }
    void on_create_move() { 
        // Сбрасываем блокировку когда меню закрыто и кнопки не нажаты
        if (!m_opened && m_block_attack_until_release) {
            if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) &&
                !(GetAsyncKeyState(VK_RBUTTON) & 0x8000) &&
                !(GetAsyncKeyState(VK_MBUTTON) & 0x8000)) {
                m_block_attack_until_release = false;
            }
        }
    }

    c_gui& ui() { return ::gui; }

private:
    bool m_initialization_overlay_active = false;
    float m_initialization_overlay_time = 0.f;
    float m_initialization_overlay_load_time = 0.f;
    float m_initialization_overlay_finish_time = 0.f;
    bool m_initialization_preview_ready = false;

    void draw_initialization_overlay();
    void draw_rage();
    void draw_legit();
    void draw_anti_aim();
    void draw_visuals();
    void draw_misc();
    void draw_skins();
    void draw_configs();
};

namespace visuals_ui {
void draw_top_bar(ImVec2 button_pos);
}

inline auto g_menu = std::make_unique<c_menu>();
