cmake_minimum_required(VERSION 3.20)

if(NOT WIN32)
  message(FATAL_ERROR "airflow_shell is a Windows DLL and must be built for Win32")
endif()

project(airflow_shell LANGUAGES C CXX RC)

add_library(airflow_shell SHARED
  dllmain.cpp
  features/config/config.cpp
  features/legit/legit.cpp
  features/misc/misc.cpp
  features/rage/anti_aim.cpp
  features/rage/rage.cpp
  features/skins/skins.cpp
  menu/agent_preview.cpp
  menu/binds.cpp
  menu/blur.cpp
  menu/effects.cpp
  menu/gui.cpp
  menu/menu.cpp
  menu/menu_shared.cpp
  menu/weapon_icons.cpp
  menu/weapon_presets.cpp
  menu/visuals.cpp
  runtime/d3d9_runtime.cpp
  runtime/debug_console.cpp
  runtime/fonts.cpp
  runtime/steam_avatar.cpp
  sdk/config_system/config_system.cpp
  sdk/includes/imgui/imgui.cpp
  sdk/includes/imgui/imgui_demo.cpp
  sdk/includes/imgui/imgui_draw.cpp
  sdk/includes/imgui/imgui_impl_dx9.cpp
  sdk/includes/imgui/imgui_impl_win32.cpp
  sdk/includes/imgui/imgui_stdlib.cpp
  sdk/includes/imgui/imgui_tables.cpp
  sdk/includes/imgui/imgui_widgets.cpp
  sdk/includes/minhook/buffer.c
  sdk/includes/minhook/hde/hde32.c
  sdk/includes/minhook/hde/hde64.c
  sdk/includes/minhook/hook.c
  sdk/includes/minhook/trampoline.c
  menu_fonts.rc
)

target_compile_features(airflow_shell PRIVATE cxx_std_20)
target_compile_definitions(airflow_shell PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)
target_include_directories(airflow_shell PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/menu"
  "${CMAKE_CURRENT_SOURCE_DIR}/sdk/includes/imgui"
  "${CMAKE_CURRENT_SOURCE_DIR}/sdk/includes/minhook"
  "${CMAKE_CURRENT_SOURCE_DIR}/sdk/includes/json"
)
target_link_libraries(airflow_shell PRIVATE
  d3d9
  shell32
  imm32
  dwmapi
  gdi32
  user32
  windowscodecs
  ole32
)

set_target_properties(airflow_shell PROPERTIES
  PREFIX ""
  OUTPUT_NAME "airflow_shell"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
  target_compile_options(airflow_shell PRIVATE
    /W4
    /utf-8
    $<$<COMPILE_LANGUAGE:CXX>:/Zc:char8_t->
  )
  set_property(TARGET airflow_shell PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
endif()
