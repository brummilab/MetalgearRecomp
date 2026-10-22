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
    -fno-pie
    -fno-stack-protector
    -O1 -g
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

# --- stage overlays --------------------------------------------------------
# Every source used by a fully decompiled overlay is compiled once (as
# upstream does); each overlay is then prelinked from its subset of objects
# with its symbols made local (tools/link_overlay.py).
option(MGS_WITH_OVERLAYS "Link the natively available stage overlays" ON)

set(MGS_OVERLAY_ALL_SOURCES)
if(MGS_WITH_OVERLAYS)
    foreach(ov ${MGS_OVERLAYS})
        list(APPEND MGS_OVERLAY_ALL_SOURCES ${MGS_OVERLAY_${ov}})
    endforeach()
    list(REMOVE_DUPLICATES MGS_OVERLAY_ALL_SOURCES)
endif()

if(WIN32)
    set(MGS_SYM_PREFIX "_")
else()
    set(MGS_SYM_PREFIX "")
endif()

set(MGS_OVERLAY_OBJECTS)
if(MGS_OVERLAY_ALL_SOURCES)
    mgs_translated(MGS_OVERLAY_GEN ${MGS_OVERLAY_ALL_SOURCES})
    add_library(mgs_overlay_code OBJECT ${MGS_OVERLAY_GEN})
    add_dependencies(mgs_overlay_code mgs_translate)
    target_compile_options(mgs_overlay_code PRIVATE ${MGS_GAME_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/include/mgs_native.h)
    target_compile_definitions(mgs_overlay_code PRIVATE ${MGS_GAME_DEFS})
    target_include_directories(mgs_overlay_code PRIVATE ${MGS_GAME_INCLUDES})

    set(objdir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/mgs_overlay_code.dir")
    file(RELATIVE_PATH gen_rel "${CMAKE_CURRENT_BINARY_DIR}" "${GEN_DIR}")
    foreach(ov ${MGS_OVERLAYS})
        set(objs)
        foreach(src ${MGS_OVERLAY_${ov}})
            file(RELATIVE_PATH rel "${MGS_ROOT}/source" "${src}")
            list(APPEND objs "${objdir}/${gen_rel}/source/${rel}${CMAKE_C_OUTPUT_EXTENSION}")
        endforeach()
        set(out "${CMAKE_CURRENT_BINARY_DIR}/overlays/${ov}${CMAKE_C_OUTPUT_EXTENSION}")
        add_custom_command(
            OUTPUT ${out}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/overlays
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/link_overlay.py
                    "${CMAKE_C_COMPILER};${CMAKE_C_FLAGS}" ${CMAKE_NM} ${CMAKE_OBJCOPY}
                    "${MGS_SYM_PREFIX}" ${ov} ${out} ${objs}
            DEPENDS mgs_overlay_code ${objs} ${CMAKE_CURRENT_SOURCE_DIR}/tools/link_overlay.py
            COMMENT "Prelinking overlay ${ov}"
            VERBATIM)
        list(APPEND MGS_OVERLAY_OBJECTS ${out})
    endforeach()
    set(MGS_LINKED_OVERLAYS ${MGS_OVERLAYS})
endif()

# --- recompiled overlays ---------------------------------------------------------
# Overlays that still contain assembly are recompiled from their original
# binary (tools/recomp.py), which the PS1 matching build (scripts/build.sh)
# leaves in ${MGS_ROOT}/obj. Without that build they stay unavailable.
option(MGS_RECOMP_OVERLAYS "Recompile the overlays that are not fully decompiled from the PS1 build" ON)
set(MGS_RECOMP_SKIP "rank" CACHE STRING
    "Overlays not to recompile (rank drives timer and pad hardware registers directly)")
set(MGS_RECOMPILED)
if(MGS_RECOMP_OVERLAYS AND MGS_WITH_OVERLAYS)
    foreach(ov ${MGS_OVERLAYS_INCOMPLETE})
        if(EXISTS "${MGS_ROOT}/obj/${ov}.bin" AND EXISTS "${MGS_ROOT}/obj/asm_${ov}_lhs.map"
           AND NOT ov IN_LIST MGS_RECOMP_SKIP)
            list(APPEND MGS_RECOMPILED ${ov})
        endif()
    endforeach()
    list(LENGTH MGS_RECOMPILED n)
    message(STATUS "Recompiled overlays (${n}): ${MGS_RECOMPILED}")
endif()

set(OVERLAY_TABLE "${CMAKE_CURRENT_BINARY_DIR}/overlay_table.c")
add_custom_command(
    OUTPUT ${OVERLAY_TABLE}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_overlay_table.py ${OVERLAY_TABLE}
            ${MGS_LINKED_OVERLAYS} --recomp ${MGS_RECOMPILED}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_overlay_table.py
    VERBATIM)

# glue code compiled like game code (PsyQ headers)
add_library(mgs_glue OBJECT src/game/native_overlay.c src/game/native_options.c ${OVERLAY_TABLE})
add_dependencies(mgs_glue mgs_translate)
target_compile_options(mgs_glue PRIVATE ${MGS_GAME_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/include/mgs_native.h)
target_compile_definitions(mgs_glue PRIVATE ${MGS_GAME_DEFS})
target_include_directories(mgs_glue PRIVATE ${MGS_GAME_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/src/game)

# --- PlayStation runtime + platform ----------------------------------------------
add_library(psx_runtime STATIC
    src/psx/os.c
    src/psx/mem.c
    src/psx/kernel.c
    src/psx/gte.c
    src/psx/libgte.c
    src/psx/gpu.c
    src/psx/libgpu.c
    src/psx/libcd.c
    src/psx/libpad.c
    src/psx/spu.c
    src/psx/card.c
    src/psx/mdec.c
    src/psx/xa.c
    src/psx/ws.c
    src/psx/interp.c
    src/psx/recomp_rt.c
    src/platform_sdl.c)
target_compile_options(psx_runtime PRIVATE -O2 -fno-strict-aliasing -Wall)
target_include_directories(psx_runtime PRIVATE src)
target_link_libraries(psx_runtime PRIVATE SDL2::SDL2)

# Symbol lists of the native program for the recompiler: what recompiled code
# may reference, and which PsyQ library functions the runtime replaces.
set(RC_DIR "${CMAKE_CURRENT_BINARY_DIR}/recomp")
set(RC_NATIVE "${RC_DIR}/native_syms.txt")
set(RC_RUNTIME "${RC_DIR}/runtime_syms.txt")
set(RC_FLAGS -O1 -fno-builtin -fno-stack-protector -fno-pie -fno-strict-aliasing -w)
set(MGS_RECOMP_OBJECTS)
set(MGS_HAVE_PS1_EXE OFF)
if(EXISTS "${MGS_ROOT}/obj/_mgsi.exe" AND EXISTS "${MGS_ROOT}/obj/asm.map")
    set(MGS_HAVE_PS1_EXE ON)
endif()
if(MGS_RECOMPILED OR MGS_HAVE_PS1_EXE)
    add_custom_command(
        OUTPUT ${RC_NATIVE} ${RC_RUNTIME}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RC_DIR}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/native_syms.py
                ${CMAKE_NM} --prefix=${MGS_SYM_PREFIX} ${RC_NATIVE} ${RC_RUNTIME} $<TARGET_FILE:psx_runtime>
                $<TARGET_OBJECTS:mgs_game> $<TARGET_OBJECTS:mgs_glue> ${MGS_OVERLAY_OBJECTS}
        DEPENDS psx_runtime mgs_game mgs_glue $<TARGET_OBJECTS:mgs_game> $<TARGET_OBJECTS:mgs_glue>
                ${MGS_OVERLAY_OBJECTS} ${CMAKE_CURRENT_SOURCE_DIR}/tools/native_syms.py
        COMMENT "Collecting native symbols for the recompiler"
        COMMAND_EXPAND_LISTS
        VERBATIM)
endif()
if(MGS_RECOMPILED)
    set(rc_sources)
    foreach(ov ${MGS_RECOMPILED})
        add_custom_command(
            OUTPUT ${RC_DIR}/${ov}.c
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/recomp.py overlay ${ov}
                    ${MGS_ROOT}/obj/${ov}.bin ${MGS_ROOT}/obj/asm_${ov}_lhs.map ${RC_NATIVE} ${RC_RUNTIME}
                    ${MGS_ROOT}/source ${RC_DIR}/${ov}.c
            DEPENDS ${MGS_ROOT}/obj/${ov}.bin ${MGS_ROOT}/obj/asm_${ov}_lhs.map ${RC_NATIVE} ${RC_RUNTIME}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/recomp.py
            COMMENT "Recompiling overlay ${ov}"
            VERBATIM)
        list(APPEND rc_sources ${RC_DIR}/${ov}.c)
    endforeach()
    add_library(mgs_recomp OBJECT ${rc_sources})
    target_compile_options(mgs_recomp PRIVATE ${RC_FLAGS})
    set(MGS_RECOMP_OBJECTS $<TARGET_OBJECTS:mgs_recomp>)
endif()

# --- executable ----------------------------------------------------------------
add_executable(mgs src/main.c
    $<TARGET_OBJECTS:mgs_game> $<TARGET_OBJECTS:mgs_glue> ${MGS_OVERLAY_OBJECTS} ${MGS_RECOMP_OBJECTS})
set_source_files_properties(${MGS_OVERLAY_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
target_include_directories(mgs PRIVATE src)
target_link_libraries(mgs PRIVATE psx_runtime SDL2::SDL2 m)

# --- self-tests (no game data needed) ---------------------------------------------
add_executable(test_runtime tests/test_runtime.c src/psx/gte.c src/psx/libgte.c src/psx/gpu.c src/psx/libgpu.c
    src/psx/libcd.c src/psx/mdec.c src/psx/xa.c src/psx/spu.c src/psx/ws.c src/psx/interp.c)
target_include_directories(test_runtime PRIVATE src)
target_link_libraries(test_runtime PRIVATE m)
enable_testing()
add_test(NAME runtime COMMAND test_runtime)

# The recompiler checked against the compiler: functions of the original
# executable, recompiled from the PS1 build, must behave like their natively
# compiled decompiled source.
set(MGS_TEST_TARGETS mgs test_runtime)
if(MGS_HAVE_PS1_EXE)
    set(RC_TEST_FUNCS GV_StrCode GV_AddVec3 GV_SubVec3 GV_VecLen3 GV_DiffVec3 GV_VecDir2
        GV_DiffDirU GV_DiffDirS GV_DiffDirAbs GV_NearExp2 GV_NearExp4)
    add_custom_command(
        OUTPUT ${RC_DIR}/exe_funcs.c
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/recomp.py exe
                ${MGS_ROOT}/obj/_mgsi.exe ${MGS_ROOT}/obj/asm.map ${RC_NATIVE} ${MGS_ROOT}/source
                ${RC_DIR}/exe_funcs.c ${RC_TEST_FUNCS}
        DEPENDS ${MGS_ROOT}/obj/_mgsi.exe ${MGS_ROOT}/obj/asm.map ${RC_NATIVE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/recomp.py
        COMMENT "Recompiling test functions of the original executable"
        VERBATIM)
    set_source_files_properties(${RC_DIR}/exe_funcs.c PROPERTIES COMPILE_OPTIONS "${RC_FLAGS}")
    add_executable(test_recomp tests/test_recomp.c ${RC_DIR}/exe_funcs.c
        $<TARGET_OBJECTS:mgs_game> $<TARGET_OBJECTS:mgs_glue> ${MGS_OVERLAY_OBJECTS} ${MGS_RECOMP_OBJECTS})
    target_include_directories(test_recomp PRIVATE src)
    target_compile_definitions(test_recomp PRIVATE MGS_OBJ_DIR="${MGS_ROOT}/obj")
    target_link_libraries(test_recomp PRIVATE psx_runtime SDL2::SDL2 m)
    add_test(NAME recomp COMMAND test_recomp)
    list(APPEND MGS_TEST_TARGETS test_recomp)
endif()

# Memory layout (see src/psx/psx.h): the image must stay below 16 MiB so the
# GPU's 24-bit ordering-table links can address it, the PS1 RAM window at
# 0x80000000 must be reachable, and _bss_orgend marks the overlay area.
foreach(t ${MGS_TEST_TARGETS})
    if(WIN32)
        target_link_options(${t} PRIVATE -Wl,--image-base=0x00400000 -Wl,--large-address-aware
                            -Wl,--disable-dynamicbase -Wl,--defsym=__bss_orgend=0x800C3208)
    else()
        target_compile_options(${t} PRIVATE -fno-pie)
        target_link_options(${t} PRIVATE -no-pie -Wl,-Ttext-segment=0x00400000 -Wl,--defsym=_bss_orgend=0x800C3208)
    endif()
endforeach()
