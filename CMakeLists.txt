cmake_minimum_required(VERSION 3.10)
project(FileScanner LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)

# DLL
add_library(FileScanner SHARED src/FileScanner.cpp src/FileScannerImpl.cpp)
target_include_directories(FileScanner PUBLIC src)
target_link_libraries(FileScanner PRIVATE
    -static-libgcc -static-libstdc++
    -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
)

# 测试程序
option(BUILD_TEST "Build test program" ON)
if(BUILD_TEST)
    add_executable(FileScannerTest test/test_main.cpp)
    target_link_libraries(FileScannerTest PRIVATE FileScanner)
endif()
