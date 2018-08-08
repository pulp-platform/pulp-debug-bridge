#
# Copyright (C) 2018 ETH Zurich and University of Bologna and GreenWaves Technologies SAS
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Authors: Martin Croome, GreenWaves Technologies

cmake_minimum_required(VERSION 3.5)
project(plpbridge VERSION 1.0.0)
set (CMAKE_CXX_STANDARD 11)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

set(FTDI_DEVKIT_NAME, "libftdi1-1.4git_devkit_x86_x64_14June2018")
set(FTDI_DEVKIT_URL, "https://sourceforge.net/projects/picusb/files/${FTDI_DEVKIT_NAME}.zip/download")

option(WithFrameBuffer "Build with framebuffer support" OFF)
option(WithFTDI "Build with FTDI support" ON)

include(ExternalProject)

set(EXTERNAL_INSTALL_LOCATION ${CMAKE_BINARY_DIR}/external)

# Download LibFTDI SDK if on windows

if(WIN32)
    ExternalProject_Add(
        ftdi
        PREFIX ${CMAKE_BINARY_DIR}/ftdi
        URL "https://sourceforge.net/projects/picusb/files/libftdi1-1.4git_devkit_x86_x64_14June2018.zip/download"
        DOWNLOAD_NAME "libftdi1-1.4git_devkit_x86_x64_14June2018.zip"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        LOG_DOWNLOAD ON
    )
    ExternalProject_Get_Property(ftdi source_dir)
    set(FTDI_LIB_SEARCH ${source_dir})    
endif()


# Add in JSON-TOOLS
ExternalProject_Add(
    json-tools
    PREFIX ${CMAKE_BINARY_DIR}/json-tools
    GIT_REPOSITORY https://github.com/GreenWaves-Technologies/json-tools
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/json-tools
    LOG_DOWNLOAD ON
)

ExternalProject_Get_Property(json-tools install_dir)
set(JSON_TOOLS_INCLUDE_DIR ${install_dir}/include)
set(JSON_TOOLS_LIB ${install_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}json${CMAKE_STATIC_LIBRARY_SUFFIX})
set(JSON_TOOLS_PYDIR ${install_dir}/python)

# Add in PULP CONFIGS
ExternalProject_Add(
    pulp-configs
    PREFIX ${CMAKE_BINARY_DIR}/pulp-configs
    GIT_REPOSITORY https://github.com/pulp-platform/pulp-configs.git
    BUILD_IN_SOURCE ON
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make
    INSTALL_COMMAND ""
    LOG_DOWNLOAD ON
)

ExternalProject_Get_Property(pulp-configs source_dir)
set(PULP_CONFIGS_BIN ${source_dir}/bin)
set(PULP_CONFIGS_CONFIGS ${source_dir}/configs)

# Create library
add_library(libplpbridge SHARED
    src/cables/adv_dbg_itf/adv_dbg_itf.cpp
    src/cables/jtag-proxy/jtag-proxy.cpp
    src/cables/jtag.cpp
    src/python_wrapper.cpp
    src/reqloop.cpp
    src/gdb-server/gdb-server.cpp
    src/gdb-server/rsp.cpp
    src/gdb-server/target.cpp
    src/gdb-server/Tcp_listener.cpp
    src/gdb-server/breakpoints.cpp
    src/ioloop.cpp
    ${JSON_TOOLS_SRC_DIR}
)

# All include directories for libplpbridge
target_include_directories(libplpbridge PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cables/jtag-proxy
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cables/adv_dbg_itf
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cables/cables
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gdb-server
    ${JSON_TOOLS_INCLUDE_DIR}
)

# link with json-tools
target_link_libraries(libplpbridge PRIVATE ${JSON_TOOLS_LIB})
add_dependencies(libplpbridge json-tools)

# Compile configs
add_dependencies(libplpbridge pulp-configs)

if(WIN32)
  add_dependencies(libplpbridge ftdi)
endif()

# Enable all warnings as errors
if(MSVC)
    target_compile_options(libplpbridge PRIVATE /W4 /WX)
else(MSVC)
    if (APPLE)
        target_compile_options(libplpbridge PUBLIC -Wall -Wextra -pedantic -Werror -Wno-unused-private-field)
    else(APPLE)
        target_compile_options(libplpbridge PRIVATE -Wall -Wextra -pedantic -Werror)
    endif(APPLE)
endif(MSVC)

# Optionally add in FTDI support
if (WithFTDI)
    find_package(FTDI REQUIRED PATHS ${FTDI_LIB_SEARCH})
    if(FTDI_FOUND)
        message(STATUS "Compiling in FTDI support")
        target_compile_definitions(libplpbridge PUBLIC __USE_FTDI__)
        target_sources(libplpbridge PRIVATE src/cables/ftdi/ftdi.cpp)
        target_include_directories(libplpbridge
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/cables/ftdi
                    ${FTDI_INCLUDE_DIR})
        target_link_libraries(libplpbridge PRIVATE ${FTDI_LIBRARY})
    else()
        message(FATAL_ERROR "Can't find ftdi library")
    endif()
endif()

# Optionally add in SDL/Framebuffer support
if (WithFrameBuffer)
    find_package(SDL2 REQUIRED)
    if (SDL2_LIBRARY)
        message(STATUS "Compiling with framebuffer support")
        target_compile_definitions(libplpbridge PUBLIC __USE_SDL__)
        target_include_directories(libplpbridge PRIVATE ${SDL2_INCLUDE_DIR})
        target_link_libraries(libplpbridge PRIVATE ${SDL2_LIBRARY})
    else()
        message(FATAL_ERROR "Can't find sdl2 library")
    endif()
endif()

set_target_properties(libplpbridge PROPERTIES
    VERSION ${PROJECT_VERSION}
    OUTPUT_NAME "pulpdebugbridge" # lib will be added as appropriate
)

# python tool binaries

install(
    DIRECTORY ${PULP_CONFIGS_BIN} DESTINATION .
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    FILES_MATCHING PATTERN "*.py"
)

install(
    DIRECTORY ${PULP_CONFIGS_BIN} DESTINATION .
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    FILES_MATCHING PATTERN "pulp_*"
)

install(
    DIRECTORY bin DESTINATION .
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# IP configs

install(
    DIRECTORY ${PULP_CONFIGS_CONFIGS} DESTINATION .
    FILES_MATCHING PATTERN "*.json"
)

# PULP bridge library

install(
    TARGETS libplpbridge
    DESTINATION lib
)

# Python module files

install(
    DIRECTORY python DESTINATION .
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    FILES_MATCHING PATTERN "*.py"
)

install(
    DIRECTORY ${JSON_TOOLS_PYDIR} DESTINATION .
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    FILES_MATCHING PATTERN "*.py"
)