cmake_minimum_required(VERSION 3.16)
project(minisoc CXX)

set(CMAKE_CXX_STANDARD 17)          # must match the C++ standard libsystemc was built with
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)       # -g by default: debug models with gdb
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

option(MINISOC_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
if(MINISOC_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(TCL)  # only TCL_LIBRARY + TCL_INCLUDE_PATH are used (Tk not needed)
if(NOT TCL_LIBRARY OR NOT TCL_INCLUDE_PATH)
  message(FATAL_ERROR "Tcl development files not found (apt install tcl-dev)")
endif()
find_library(SYSTEMC_LIB systemc REQUIRED)

# Register map: spec/minisoc.yaml -> generated/regmap.{h,tcl,md}
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
set(SPEC ${CMAKE_SOURCE_DIR}/spec/minisoc.yaml)
add_custom_command(
  OUTPUT ${GEN_DIR}/regmap.h ${GEN_DIR}/regmap.tcl ${GEN_DIR}/regmap.md
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/regmap_gen.py ${SPEC} ${GEN_DIR}
  DEPENDS ${SPEC} ${CMAKE_SOURCE_DIR}/tools/regmap_gen.py
  COMMENT "Generating register map from spec")
add_custom_target(regmap DEPENDS ${GEN_DIR}/regmap.h)

add_executable(minisoc src/main.cpp src/cpu.cpp firmware/firmware.cpp)
add_dependencies(minisoc regmap)
target_include_directories(minisoc PRIVATE ${GEN_DIR} src firmware ${TCL_INCLUDE_PATH})
target_compile_definitions(minisoc PRIVATE MINISOC_REGMAP_TCL="${GEN_DIR}/regmap.tcl")
target_link_libraries(minisoc PRIVATE ${SYSTEMC_LIB} ${TCL_LIBRARY})
