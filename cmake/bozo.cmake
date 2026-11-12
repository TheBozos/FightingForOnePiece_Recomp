# Fighting for One Piece on BozoEngine: the game's EE code recompiled by bozo-recomp, FFOP's runtime/ on BozoEngine's
# port API, every Sony library native.
#
#   cmake -S . -B build/bozo -G Ninja -DFFOP_ELF=<SLPS_255.45> [-DBOZO_ENGINE_DIR=<BozoEngine>]
#         [-DFFOP_SYMBOL_MAP=<ffop_symbols.txt>]
#
# Targets: ffop-headless (no window, BozoEngine's free host).

set(BOZO_ENGINE_DIR "" CACHE PATH "BozoEngine's source tree (default: the BozoEngine submodule)")
if(NOT BOZO_ENGINE_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/BozoEngine/CMakeLists.txt")
    set(BOZO_ENGINE_DIR "${CMAKE_SOURCE_DIR}/BozoEngine" CACHE PATH "BozoEngine's source tree (default: the BozoEngine submodule)" FORCE)
endif()
set(FFOP_ELF "" CACHE FILEPATH "FFOP's executable (SLPS_255.45), extracted from the disc")
set(FFOP_SYMBOL_MAP "${CMAKE_SOURCE_DIR}/recomp/ffop_symbols.txt" CACHE FILEPATH
    "Names of the stripped ELF's library functions (name 0xADDR lines), given to bozo-externals, bozo-recomp and the free host")
set(FFOP_IRX_DIR "" CACHE PATH "The disc's MODULES folder (default: next to FFOP_ELF)")
set(FFOP_GEN_FILES 32 CACHE STRING "How many .cpp files the generated code is split into")
set(FFOP_GEN_OPT "-O1" CACHE STRING "Optimisation of the generated code")
if(NOT BOZO_ENGINE_DIR OR NOT EXISTS "${BOZO_ENGINE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "FFOP: set BOZO_ENGINE_DIR to BozoEngine's source tree")
endif()
if(NOT FFOP_ELF OR NOT EXISTS "${FFOP_ELF}")
    message(FATAL_ERROR "FFOP: set FFOP_ELF to the game's executable (SLPS_255.45)")
endif()
if(NOT EXISTS "${FFOP_SYMBOL_MAP}")
    message(FATAL_ERROR "FFOP: FFOP_SYMBOL_MAP ${FFOP_SYMBOL_MAP} does not exist")
endif()
if(NOT FFOP_IRX_DIR)
    get_filename_component(ffop_disc "${FFOP_ELF}" DIRECTORY)
    set(FFOP_IRX_DIR "${ffop_disc}/MODULES")
endif()
foreach(m LIBSD SDRDRV MODHSYN MODMSIN STRADPCM SKSOUND SKHSYNTH SKMSIN SKSADPCM)
    if(NOT EXISTS "${FFOP_IRX_DIR}/${m}.IRX")
        message(FATAL_ERROR "FFOP: ${FFOP_IRX_DIR}/${m}.IRX not found (set FFOP_IRX_DIR to the disc's MODULES folder)")
    endif()
endforeach()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(BOZO_TMB_ELF "" CACHE FILEPATH "" FORCE)
set(BOZO_APP OFF CACHE BOOL "" FORCE)
add_subdirectory("${BOZO_ENGINE_DIR}" bozo)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(gen_dir "${CMAKE_BINARY_DIR}/ffop_gen")
set(overrides_list "${gen_dir}/ffop_overrides.txt")
set(externals "${gen_dir}/ffop_externals.txt")
file(GLOB FFOP_RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/*.cpp")

add_custom_command(
    OUTPUT ${overrides_list}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${gen_dir}
    COMMAND Python3::Interpreter -I "${CMAKE_SOURCE_DIR}/tools/ffop_overrides_list.py" "${CMAKE_SOURCE_DIR}/runtime" ${overrides_list}
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/ffop_overrides_list.py" ${FFOP_RUNTIME_SOURCES}
    COMMENT "Listing FFOP's overrides")
add_custom_command(
    OUTPUT ${externals}
    COMMAND bozo-externals ${FFOP_ELF} ${externals} --overrides ${overrides_list} --symbols ${FFOP_SYMBOL_MAP}
    DEPENDS bozo-externals ${overrides_list} ${FFOP_SYMBOL_MAP} ${FFOP_ELF}
    COMMENT "Writing the externals list")

set(gen_files ${gen_dir}/ffop_functions.h ${gen_dir}/ffop_table.cpp)
math(EXPR last "${FFOP_GEN_FILES} - 1")
foreach(i RANGE ${last})
    string(LENGTH "${i}" len)
    if(len EQUAL 1)
        set(n "00${i}")
    elseif(len EQUAL 2)
        set(n "0${i}")
    else()
        set(n "${i}")
    endif()
    list(APPEND gen_files ${gen_dir}/ffop_${n}.cpp)
endforeach()
add_custom_command(
    OUTPUT ${gen_files}
    COMMAND bozo-recomp ${FFOP_ELF} ${gen_dir} --prefix ffop --files ${FFOP_GEN_FILES} --no-comments --overrides-take-jumps --externals ${externals}
            --symbols ${FFOP_SYMBOL_MAP}
    DEPENDS bozo-recomp ${externals} ${FFOP_ELF} ${FFOP_SYMBOL_MAP}
    COMMENT "Recompiling FFOP")
add_custom_target(ffop_recompile DEPENDS ${gen_files})
add_library(ffop_generated OBJECT ${gen_files})
target_link_libraries(ffop_generated PUBLIC bozo_runtime_headers)
target_include_directories(ffop_generated PUBLIC ${gen_dir})
target_compile_options(ffop_generated PRIVATE ${FFOP_GEN_OPT} -w)

set(irx_dir "${gen_dir}/iop")
set(native_irx "${BOZO_ENGINE_DIR}/tools/native_irx")
set(irx_sources)
set(irx_images)
foreach(spec "LIBSD:libsd:0x8000:libsd" "SDRDRV:sdrdrv:0xe000:" "MODHSYN:modhsyn:0x10000:" "MODMSIN:modmsin:0x21000:"
             "STRADPCM:stradpcm:0x22000:" "SKSOUND:sksound:0x26000:" "SKHSYNTH:skhsynth:0x28000:"
             "SKMSIN:skmsin:0x2a000:" "SKSADPCM:sksadpcm:0x2c000:")
    string(REPLACE ":" ";" spec "${spec}")
    list(GET spec 0 module)
    list(GET spec 1 stem)
    list(GET spec 2 base)
    list(GET spec 3 flag)
    add_custom_command(
        OUTPUT ${irx_dir}/${stem}.json ${irx_dir}/${stem}.bin ${irx_dir}/${stem}_iop.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${irx_dir}
        COMMAND Python3::Interpreter -I "${native_irx}/relocate.py" "${FFOP_IRX_DIR}/${module}.IRX" ${base} ${irx_dir}/${stem}
        COMMAND Python3::Interpreter -I "${native_irx}/recompile.py" ${irx_dir}/${stem}.json --out ${irx_dir}
        DEPENDS "${FFOP_IRX_DIR}/${module}.IRX" "${native_irx}/relocate.py" "${native_irx}/recompile.py"
        COMMENT "Compiling ${module}.IRX")
    list(APPEND irx_sources ${irx_dir}/${stem}_iop.cpp)
    if(flag)
        list(APPEND irx_images ${irx_dir}/${stem}.json:${flag})
    else()
        list(APPEND irx_images ${irx_dir}/${stem}.json)
    endif()
endforeach()
add_custom_command(
    OUTPUT ${irx_dir}/ffop_irx_images.cpp
    COMMAND Python3::Interpreter -I "${CMAKE_SOURCE_DIR}/tools/ffop_irx_images.py" "${native_irx}" ${irx_dir}/ffop_irx_images.cpp ${irx_images}
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/ffop_irx_images.py" ${irx_sources}
    COMMENT "Writing FFOP's IOP module images")
add_library(ffop_iop OBJECT ${irx_sources} ${irx_dir}/ffop_irx_images.cpp)
target_include_directories(ffop_iop PRIVATE "${CMAKE_SOURCE_DIR}/runtime")
target_link_libraries(ffop_iop PUBLIC bozo_runtime_headers bozo_libs)
target_compile_options(ffop_iop PRIVATE -w)

add_library(ffop_runtime OBJECT ${FFOP_RUNTIME_SOURCES})
target_include_directories(ffop_runtime PUBLIC "${CMAKE_SOURCE_DIR}/runtime" ${gen_dir})
target_link_libraries(ffop_runtime PUBLIC bozo_port bozo_render bozo_libs bozo_runtime_core)
add_dependencies(ffop_runtime ffop_generated)

add_executable(ffop-headless bozo/ffop_headless.cpp)
target_link_libraries(ffop-headless PRIVATE ffop_runtime ffop_generated ffop_iop bozo_host)
target_compile_definitions(ffop-headless PRIVATE BOZO_RUN_DEFAULT_ELF="${FFOP_ELF}"
    FFOP_DEFAULT_SYMBOL_MAP="${FFOP_SYMBOL_MAP}")
