# license:BSD-3-Clause
# copyright-holders:David Sexton
# Android build of the standalone DoubleTalk PC emulator core
# (vendored from doubletalk-pc) plus the JNI bridge.
cmake_minimum_required(VERSION 3.22)
project(dtalk CXX)

add_library(dtalk SHARED
    doubletalk/shim/emu.cpp
    doubletalk/mame/i86.cpp
    doubletalk/mame/i186.cpp
    doubletalk/doubletalk_board.cpp
    doubletalk/dtalk.cpp
    dtalk_jni.cpp
)

target_include_directories(dtalk PRIVATE
    doubletalk
    doubletalk/shim
    doubletalk/mame
)

target_compile_features(dtalk PRIVATE cxx_std_20)
target_compile_options(dtalk PRIVATE -O2 -Wall -Wno-unused-variable -fexceptions -frtti)

target_link_libraries(dtalk log)
