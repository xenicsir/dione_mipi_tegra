cmake_minimum_required(VERSION 3.0.0)
project(Calc C)

add_executable(calc main.c ../driver_src/tc358746_calculation.c)
target_compile_definitions(calc PUBLIC TC358746_DEFINE_LOGS)
target_include_directories(calc PUBLIC . include ../driver_src)
