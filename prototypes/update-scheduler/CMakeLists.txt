cmake_minimum_required(VERSION 3.25)
project(LcarsUpdateSchedulerPrototype LANGUAGES CXX)
# Throwaway logic prototype; standalone, no dependencies.
add_executable(update_scheduler main.cpp scheduler_model.hpp)
set_target_properties(update_scheduler PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_definitions(update_scheduler PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(update_scheduler PRIVATE /utf-8)
target_link_libraries(update_scheduler PRIVATE user32)
