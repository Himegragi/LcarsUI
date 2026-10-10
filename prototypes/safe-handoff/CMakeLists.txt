cmake_minimum_required(VERSION 3.25)
project(LcarsSafeHandoffPrototype LANGUAGES CXX)
# Throwaway safe-handoff logic prototype; standalone, no dependencies.
add_executable(safe_handoff main.cpp handoff_model.hpp resume_main.cpp resume_model.hpp)
set_target_properties(safe_handoff PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_definitions(safe_handoff PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(safe_handoff PRIVATE /utf-8 /EHsc)
target_link_libraries(safe_handoff PRIVATE user32)
