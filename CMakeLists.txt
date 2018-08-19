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

set(FTDI_DEVKIT_NAME "libftdi1-1.4git_devkit_x86_x64_14June2018.zip")
set(FTDI_DEVKIT_URL "https://sourceforge.net/projects/picusb/files/${FTDI_DEVKIT_NAME}/download")

option(WithFrameBuffer "Build with framebuffer support" OFF)
option(WithFTDI "Build with FTDI support" ON)
option(StandaloneApp "Install as standalone app" OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug"
        CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
        FORCE
    )
endif(NOT CMAKE_BUILD_TYPE)

# use, i.e. don't skip the full RPATH for the build tree
SET(CMAKE_SKIP_BUILD_RPATH  FALSE)

# when building, don't use the install RPATH already
# (but later on when installing)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if (WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WIN64 YES)
  endif()
elseif (APPLE)
  set(INSTALL_CMD ginstall)
else()
  set(INSTALL_CMD install)
endif()

include(ExternalProject)

set(EXTERNAL_INSTALL_LOCATION ${CMAKE_BINARY_DIR}/external)

# Passing CMAKE_IGNORE_PATH to subbuilds is to avoid git shell being picked up by mingw
# Add in JSON-TOOLS
ExternalProject_Add(
  json-tools
  PREFIX ${CMAKE_BINARY_DIR}/json-tools
  GIT_REPOSITORY https://github.com/GreenWaves-Technologies/json-tools
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/json-tools -DCMAKE_IGNORE_PATH=${CMAKE_IGNORE_PATH}
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
  GIT_TAG martin
  CMAKE_ARGS -DCMAKE_IGNORE_PATH=${CMAKE_IGNORE_PATH} -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
  LOG_DOWNLOAD ON
)

ExternalProject_Get_Property(pulp-configs source_dir)
set(PULP_CONFIGS_BIN ${source_dir}/bin)
set(PULP_CONFIGS_CONFIGS ${source_dir}/configs)

# Create library
add_library(libplpbridge MODULE
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

# Enable all warnings as errors
if(MSVC)
  target_compile_options(libplpbridge PRIVATE /W4 /WX)
else(MSVC)
  if (APPLE)
    target_compile_options(libplpbridge PUBLIC -Wall -Wextra -pedantic -Werror -Wno-unused-private-field -Wno-nested-anon-types)
  else(APPLE)
    target_compile_options(libplpbridge PRIVATE -Wall -Wextra -pedantic -Werror)
  endif(APPLE)
endif(MSVC)


# link windows sockets
if (WIN32)
  target_link_libraries(libplpbridge PRIVATE wsock32 ws2_32)
endif()

# Optionally add in FTDI support
if (WithFTDI)
  if (WIN32)
    if (WIN64)
      message(STATUS "Compiling in FTDI support")
      # Download LibFTDI SDK if on windows
      ExternalProject_Add(
          ftdi
          PREFIX ${CMAKE_BINARY_DIR}/ftdi
          URL ${FTDI_DEVKIT_URL}
          DOWNLOAD_NAME ${FTDI_DEVKIT_NAME}
          CONFIGURE_COMMAND ""
          BUILD_COMMAND ""
          INSTALL_COMMAND ""
          LOG_DOWNLOAD ON
      )
      ExternalProject_Get_Property(ftdi source_dir)
      target_include_directories(libplpbridge
          PRIVATE
              ${CMAKE_CURRENT_SOURCE_DIR}/include/cables/ftdi
              ${source_dir}/include/libftdi
      )

      set(FTDI_BIN_DIR ${source_dir}/bin64)
      target_link_libraries(libplpbridge PRIVATE ${FTDI_BIN_DIR}/libftdi1.dll)
      add_dependencies(libplpbridge ftdi)
    else()
      if (ForceFTDI)
        message(FATAL_ERROR "No Win32 bit support for FTDI")
      else()
        message(STATUS "FTDI is not available for Win32. Not including.")
      endif()
    endif()
  else()
    message(STATUS "Compiling in FTDI support")
    ExternalProject_Add(
        ftdi
        PREFIX ${CMAKE_BINARY_DIR}/ftdi
        GIT_REPOSITORY git://developer.intra2net.com/libftdi
        GIT_TAG v1.4
        CMAKE_ARGS -DSTATICLIBS=OFF -DBUILD_TESTS=OFF -DDOCUMENTATION=OFF -DEXAMPLES=OFF -DFTDIPP=OFF -DFTDI_EEPROM=OFF -DPYTHON_BINDINGS=OFF -DCMAKE_INSTALL_PREFIX=./install
        LOG_DOWNLOAD ON
    )

    ExternalProject_Get_Property(ftdi binary_dir)
    target_include_directories(libplpbridge
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/cables/ftdi
            ${binary_dir}/install/include/libftdi1
    )
    set(FTDI_LIB_DIR ${binary_dir}/install/lib)
    if(APPLE)
      target_link_libraries(libplpbridge PRIVATE ${FTDI_LIB_DIR}/libftdi1.dylib)
    else()
      target_link_libraries(libplpbridge PRIVATE ${FTDI_LIB_DIR}/libftdi1.so)
    endif()

  endif()
  target_sources(libplpbridge PRIVATE src/cables/ftdi/ftdi.cpp)
  target_compile_definitions(libplpbridge PUBLIC __USE_FTDI__)
endif()

# Optionally add in SDL/Framebuffer support
if (WithFrameBuffer)
  if (NOT WIN32)
    find_package(SDL2 REQUIRED)
    if (SDL2_LIBRARY)
      message(STATUS "Compiling with framebuffer support")
      target_compile_definitions(libplpbridge PUBLIC __USE_SDL__)
      target_include_directories(libplpbridge PRIVATE ${SDL2_INCLUDE_DIR})
      target_link_libraries(libplpbridge PRIVATE ${SDL2_LIBRARY})
    else()
      message(FATAL_ERROR "Can't find sdl2 library")
    endif()
  else()
    message(FATAL_ERROR "No Windows support for framebuffer")
  endif()
endif()

if(APPLE)
  set_target_properties(libplpbridge PROPERTIES
    OUTPUT_NAME "pulpdebugbridge" # lib will be added as appropriate
  )
else()
  set_target_properties(libplpbridge PROPERTIES
    VERSION ${PROJECT_VERSION}
    OUTPUT_NAME "pulpdebugbridge" # lib will be added as appropriate
  )
endif()

if(StandaloneApp)
  set(DEST_BIN ".")
  set(DEST_CONFIGS "configs")
  set(DEST_LIBS ".")
  set(DEST_MODULES ".")
  SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}")
else()
  set(DEST_BIN "bin")
  set(DEST_CONFIGS "configs")
  set(DEST_LIBS "lib")
  set(DEST_MODULES "python")
  SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif()

# # python tool binaries

install(
  DIRECTORY ${PULP_CONFIGS_BIN}/ DESTINATION ${DEST_BIN}
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  FILES_MATCHING PATTERN "*.py"
)

install(
  DIRECTORY ${PULP_CONFIGS_BIN}/ DESTINATION ${DEST_BIN}
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
  FILES_MATCHING PATTERN "pulp_*"
)

install(
  DIRECTORY bin/ DESTINATION ${DEST_BIN}
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# IP configs

install(
  DIRECTORY ${PULP_CONFIGS_CONFIGS}/ DESTINATION ${DEST_CONFIGS}
  FILES_MATCHING PATTERN "*.json"
)

# PULP bridge library

install(
  TARGETS libplpbridge
  DESTINATION ${DEST_LIBS}
)

# ftdi libs on Linux/MAC

if(DEFINED FTDI_LIB_DIR)
  install(
    DIRECTORY ${FTDI_LIB_DIR}/ DESTINATION ${DEST_LIBS}
    FILES_MATCHING PATTERN "libftdi1\.so*"
    PATTERN cmake EXCLUDE
    PATTERN pkgconfig EXCLUDE
  )
endif()
# Python module files

install(
  DIRECTORY python/ DESTINATION ${DEST_MODULES}
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ WORLD_EXECUTE
  FILES_MATCHING PATTERN "*.py"
)

install(
  DIRECTORY ${JSON_TOOLS_PYDIR}/ DESTINATION ${DEST_MODULES}
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ WORLD_EXECUTE
  FILES_MATCHING PATTERN "*.py"
)

# FTDI deps in bin dir if on windows

if(WithFTDI AND WIN32)
  install(
    DIRECTORY ${FTDI_BIN_DIR}/ DESTINATION ${DEST_BIN}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    FILES_MATCHING PATTERN "*.dll"
  )
endif()
