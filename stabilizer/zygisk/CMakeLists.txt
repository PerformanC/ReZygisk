cmake_minimum_required(VERSION 3.10)
project(zygisk_stabilizer C)
set(CMAKE_C_STANDARD 11)
add_library(zygisk_stabilizer SHARED stabilizer.c)
