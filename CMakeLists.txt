cmake_minimum_required(VERSION 3.20)
project(csgo_clean_base LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Build this x86 DLL with Visual Studio 2022 (MSVC).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "CS:GO is a 32-bit process. Configure with -A Win32.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Base sources (everything except vendored third-party code).
file(GLOB_RECURSE BASE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER BASE_SOURCES EXCLUDE REGEX "/third_party/")
list(FILTER BASE_SOURCES EXCLUDE REGEX "/меню фатала/")

# Vendored: Dear ImGui + MinHook.
set(THIRDPARTY_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_demo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_impl_dx9.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_impl_win32.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_stdlib.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/buffer.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/hook.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/trampoline.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/hde/hde32.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/hde/hde64.c
)

add_library(csgo_base SHARED ${BASE_SOURCES} ${THIRDPARTY_SOURCES})
target_include_directories(csgo_base PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json"
)
target_compile_definitions(csgo_base PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS IMGUI_DISABLE_ASSERT)
target_compile_options(csgo_base PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(csgo_base PRIVATE d3d9 user32 gdi32 shell32 imm32 dwmapi winmm)
set_target_properties(csgo_base PROPERTIES PREFIX "" OUTPUT_NAME "csgo_base" SUFFIX ".dll")

# The original GUI and DX9 renderer are built as a C++20 library.
  file(GLOB_RECURSE FATALITY_GUI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fatality/src/gui/*.cpp")
  set(FATALITY_RENDERER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fatality/deps/renderer")
  set(FATALITY_RENDERER_SOURCES
    ${FATALITY_RENDERER_DIR}/src/adapters/adapter_dx9.cpp
    ${FATALITY_RENDERER_DIR}/src/buffers/cmd_buffer.cpp
    ${FATALITY_RENDERER_DIR}/src/buffers/ib_dx9.cpp
    ${FATALITY_RENDERER_DIR}/src/buffers/vb_dx9.cpp
    ${FATALITY_RENDERER_DIR}/src/layer.cpp
    ${FATALITY_RENDERER_DIR}/src/renderer.cpp
    ${FATALITY_RENDERER_DIR}/src/types/animated_texture.cpp
    ${FATALITY_RENDERER_DIR}/src/types/font.cpp
    ${FATALITY_RENDERER_DIR}/src/types/shader.cpp
    ${FATALITY_RENDERER_DIR}/src/types/texture.cpp)
  file(GLOB FATALITY_EXTRA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fatality/extra/*.cpp")
  add_library(fatality_port_check STATIC ${FATALITY_EXTRA_SOURCES} ${FATALITY_GUI_SOURCES} ${FATALITY_RENDERER_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fatality/port.cpp")
  set_target_properties(fatality_port_check PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  target_include_directories(fatality_port_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fatality"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fatality/src"
    "${FATALITY_RENDERER_DIR}/include")
  target_compile_definitions(fatality_port_check PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(fatality_port_check PRIVATE d3d9 d3dcompiler gdi32 user32)
  target_link_libraries(csgo_base PRIVATE fatality_port_check d3dcompiler)
