# 32-bit Windows build with MinGW-w64 (cross-compiling from Linux, or MSYS2's
# "mingw32" environment on Windows). Point SDL2_DIR at the SDL2 MinGW
# development package, e.g.
#   -DSDL2_DIR=/path/SDL2-2.30.9/i686-w64-mingw32/lib/cmake/SDL2
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

if(NOT CMAKE_HOST_WIN32)
    set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
    set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
    set(CMAKE_NM i686-w64-mingw32-nm)
    set(CMAKE_OBJCOPY i686-w64-mingw32-objcopy)
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
