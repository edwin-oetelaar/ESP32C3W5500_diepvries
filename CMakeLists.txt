cmake_minimum_required(VERSION 3.16)
# Voeg deze regel toe om de specifieke warning te negeren in wireguard
add_compile_options("-Wno-unterminated-string-initialization")
add_compile_options("-Wno-unterminated-string-initialization")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vibe_diepvries)
