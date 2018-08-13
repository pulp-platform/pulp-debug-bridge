# - Find FTDI installation
# This module tries to find the libftdi installation on your system.
# Once done this will define
#
#  FTDI_FOUND - system has ftdi
#  FTDI_INCLUDE_DIR - ~ the ftdi include directory
#  FTDI_LIBRARY - Link these to use ftdi

include( SelectLibraryConfigurations )
include( FindPackageHandleStandardArgs )

if (FTDI_INCLUDE_DIR AND FTDI_LIBRARIES)

  # in cache already
  set(FTDI_FOUND TRUE)

else (FTDI_INCLUDE_DIR AND FTDI_LIBRARIES)
  if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    if(APPLE)
      pkg_check_modules(FDTI libftdi1)
    else()
      pkg_check_modules(FDTI libftdi)
    endif()
    set(FTDI_VERSION ${PC_FTDI_VERSION})
  endif(NOT WIN32)

  find_path(FTDI_INCLUDE_DIR
    NAMES ftdi.h
    PATHS
      ${PC_FDTI_INCLUDE_DIRS}
      /usr/local/include
      /usr/local/include/libftdi1
      /usr/include
      ${GNUWIN32_DIR}/include
      $ENV{FTDI_INCLUDE_DIR}
      PATH_SUFFIXES ftdi
  )

  find_library(FTDI_LIBRARY
    NAMES ftdi ftdi1
    PATHS
      ${PC_FDTI_INCLUDE_DIRS}
      /usr/lib
      /usr/local/lib
      ${GNUWIN32_DIR}/lib
      $ENV{FTDI_LIBRARY}
  )

  find_package_handle_standard_args(FTDI
    FOUND_VAR FTDI_FOUND
    REQUIRED_VARS
      FTDI_LIBRARY
      FTDI_INCLUDE_DIR
    VERSION_VAR FTDI_VERSION
  )

  if(FTDI_FOUND)
    set(FTDI_LIBRARIES ${FTDI_LIBRARY})
    set(FTDI_INCLUDE_DIRS ${FTDI_INCLUDE_DIR})
    set(FDTI_DEFINITIONS ${PC_FTDI_CFLAGS_OTHER})
  endif()

  mark_as_advanced(FTDI_INCLUDE_DIR FTDI_LIBRARY)

endif (FTDI_INCLUDE_DIR AND FTDI_LIBRARIES)
