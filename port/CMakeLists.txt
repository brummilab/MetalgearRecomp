cmake_minimum_required(VERSION 3.16)
project(MetalgearRecomp C)

# The game keeps pointers in 32-bit fields and patches pointers into data
# loaded from disc, so the port is a 32-bit (x86) program by design.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "The native port must be built as 32-bit. Use a toolchain file:\n"
        "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/linux-i686.cmake\n"
        "  cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake")
endif()
if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "GCC or Clang (MinGW on Windows) is required: the game code relies on GNU C.")
endif()

set(MGS_ROOT     "${CMAKE_CURRENT_SOURCE_DIR}/../upstream/mgs_reversing" CACHE PATH "mgs_reversing checkout")
set(PSYQ_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../upstream/psyq_sdk/psyq_4.4/include" CACHE PATH "PsyQ 4.4 headers")
set(GEN_DIR      "${CMAKE_CURRENT_BINARY_DIR}/gen")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(SDL2 REQUIRED)

# --- source lists, derived from upstream's linker command file -------------
set(SOURCES_CMAKE "${CMAKE_CURRENT_BINARY_DIR}/sources.cmake")
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_sources.py
            ${MGS_ROOT} ${SOURCES_CMAKE} INTEGRAL
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "gen_sources.py failed (is jinja2 installed? pip install jinja2)")
endif()
include(${SOURCES_CMAKE})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${MGS_ROOT}/build/linker_command_file.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_sources.py)

# --- translated copy of the sources (inline asm -> GTE emulator calls) -----
set(ASM2C ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/asm2c.py ${GEN_DIR}
          ${MGS_ROOT}/source:source ?${PSYQ_INCLUDE}:psyq)
execute_process(COMMAND ${ASM2C} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "asm2c.py failed")
endif()
add_custom_target(mgs_translate COMMAND ${ASM2C} COMMENT "Translating inline assembly")

function(mgs_translated out_var)
    set(result)
    foreach(src ${ARGN})
        file(RELATIVE_PATH rel "${MGS_ROOT}/source" "${src}")
        list(APPEND result "${GEN_DIR}/source/${rel}")
    endforeach()
    set(${out_var} ${result} PARENT_SCOPE)
endfunction()

# --- game code -----------------------------------------------------------
set(MGS_GAME_FLAGS
    -std=gnu89
    -funsigned-char         # PsyQ GCC: __CHAR_UNSIGNED__
    -malign-double          # MIPS alignment of 64-bit types
    -fno-strict-aliasing
    -fwrapv
    -fno-delete-null-pointer-checks
    -fcommon
    -w)
set(MGS_GAME_DEFS INTEGRAL MGS_NATIVE LANGUAGE_C _LANGUAGE_C)
set(MGS_GAME_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GEN_DIR}/source/include
    ${GEN_DIR}/source
    ${GEN_DIR}/psyq)

mgs_translated(MGS_MAIN_GEN ${MGS_MAIN_SOURCES})
add_library(mgs_game OBJECT ${MGS_MAIN_GEN})
add_dependencies(mgs_game mgs_translate)
target_compile_options(mgs_game PRIVATE ${MGS_GAME_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/include/mgs_native.h)
target_compile_definitions(mgs_game PRIVATE ${MGS_GAME_DEFS})
target_include_directories(mgs_game PRIVATE ${MGS_GAME_INCLUDES})
# the host provides main(); the game's entry point becomes mgs_game_main()
set_source_files_properties(${GEN_DIR}/source/main/main.c PROPERTIES COMPILE_DEFINITIONS main=mgs_game_main)
