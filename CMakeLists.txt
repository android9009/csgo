cmake_minimum_required(VERSION 3.20)
project(airflow_shell LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "airflow_shell is a Windows DLL and must be built for Win32")
endif()

add_library(airflow_shell SHARED dllmain.cpp)
target_compile_features(airflow_shell PRIVATE cxx_std_17)
target_compile_definitions(airflow_shell PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set_target_properties(airflow_shell PROPERTIES
  PREFIX ""
  OUTPUT_NAME "airflow_shell"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
  target_compile_options(airflow_shell PRIVATE /W4)
  set_property(TARGET airflow_shell PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
endif()
