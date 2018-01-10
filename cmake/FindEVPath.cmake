# - Find EVPath library
#   https://www.cc.gatech.edu/systems/projects/EVPath
#
# Use this module by invoking find_package with the form:
#   find_package(EVPath
#     [version] [EXACT]     # Minimum or EXACT version, e.g. 1.6.0
#     [REQUIRED]            # Fail with an error if EVPath or a required
#                           #   component is not found
#     [QUIET]               # ...
#     [COMPONENTS <...>]    # Compiled in components: fortran, readonly, 
                            # sequential (all are case insentative) 
#   )
#
# Module that finds the includes and libraries for a working EVPath install.
# This module invokes the `evpath_config` script that should be installed with
# the other EVPath tools.
#
# To provide a hint to the module where to find the EVPath installation,
# set the EVPath_ROOT or EVPath_DIR environment variable.
#
# If this variable is not set, make sure that at least the according `bin/`
# directory of EVPath is in your PATH environment variable.
#
# Set the following CMake variables BEFORE calling find_packages to
# influence this module:
#   EVPath_USE_STATIC_LIBS - Set to ON to force the use of static
#                           libraries.  Default: OFF
#
# This module will define the following variables:
#   EVPath_INCLUDE_DIRS    - Include directories for the EVPath headers.
#   EVPath_LIBRARY_PATH    - Full path of the libevpath library (.a or .so file)
#   EVPath_DEPENDENCY_LIBRARIES       - EVPath dependency libraries.
#   EVPath_FOUND           - TRUE if FindEVPath found a working install
#   EVPath_VERSION         - Version in format Major.Minor.Patch
#
# Not used for now:
#   EVPath_DEFINITIONS     - Compiler definitions you should add with
#                           add_definitions(${EVPath_DEFINITIONS})
#
###############################################################################
#Copyright (c) 2014, Axel Huebl and Felix Schmitt from http://picongpu.hzdr.de
#All rights reserved.

#Redistribution and use in source and binary forms, with or without
#modification, are permitted provided that the following conditions are met:

#1. Redistributions of source code must retain the above copyright notice, this
#list of conditions and the following disclaimer.

#2. Redistributions in binary form must reproduce the above copyright notice,
#this list of conditions and the following disclaimer in the documentation
#and/or other materials provided with the distribution.

#3. Neither the name of the copyright holder nor the names of its contributors
#may be used to endorse or promote products derived from this software without
#specific prior written permission.

#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
#FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
#CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
#OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
###############################################################################


###############################################################################
# EVPath
###############################################################################
# get flags for evpath_config, -s (static) is the default

set(evpath_config_hints)
foreach(PREFIX_VAR IN ITEMS EVPath_ROOT INSTALL_PREFIX)
  if(${PREFIX_VAR})
    list(APPEND evpath_config_hints "${${PREFIX_VAR}}/bin")
  elseif(NOT ("$ENV{${PREFIX_VAR}}" STREQUAL ""))
    list(APPEND evpath_config_hints "$ENV{${PREFIX_VAR}}/bin")
  endif()
endforeach()

# find `evpath_config` program #################################################
find_program(EVPath_CONFIG NAME evpath_config HINTS ${evpath_config_hints})

# check `evpath_config` program ################################################
if(EVPath_CONFIG)
  execute_process(COMMAND ${EVPath_CONFIG} -c 
    OUTPUT_VARIABLE evpath_config_out
    RESULT_VARIABLE evpath_config_ret
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(evpath_config_ret EQUAL 0)
    string(REPLACE " " ";" evpath_match "${evpath_config_out}")
    set(evpath_include_hints)
    set(EVPath_COMPILE_OPTIONS)
    foreach(OPT IN LISTS evpath_match)
      if(OPT MATCHES "^-I(.*)")
        list(APPEND evpath_include_hints "${CMAKE_MATCH_1}")
      else()
        list(APPEND EVPath_COMPILE_OPTIONS ${OPT})
      endif()
    endforeach()
  endif()

  execute_process(COMMAND ${EVPath_CONFIG} -l 
    OUTPUT_VARIABLE evpath_config_out
    RESULT_VARIABLE evpath_config_ret
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(evpath_config_ret EQUAL 0)
    string(REPLACE " " ";" evpath_match "${evpath_config_out}")
    set(evpath_libs)
    set(evpath_lib_hints)
    set(evpath_lib_flags)
    foreach(OPT IN LISTS evpath_match)
      if(OPT MATCHES "^-L(.*)")
        list(APPEND evpath_lib_hints "${CMAKE_MATCH_1}")
      elseif(OPT STREQUAL "-lstdc++")
        list(APPEND evpath_lib_flags "${OPT}")
      elseif(OPT MATCHES "^-l(.*)")
        list(APPEND evpath_libs "${CMAKE_MATCH_1}")
      elseif(OPT MATCHES "/*")
        list(APPEND evpath_libs "${OPT}")
      else()
        list(APPEND evpath_lib_flags "${OPT}")
      endif()
    endforeach()
  endif()

  # add the version string
  execute_process(COMMAND ${EVPath_CONFIG} -v
    OUTPUT_VARIABLE EVPath_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()

# make sure at the very least we find libevpath
if(NOT evpath_libs)
  set(evpath_libs evpath)
endif()

# Search for the actual libs and headers to use based on hints from the config
find_path(EVPath_INCLUDE_DIR evpath.h HINTS ${evpath_include_hints})

set(EVPath_LIBRARY)
set(EVPath_DEPENDENCIES)
foreach(lib IN LISTS evpath_libs)
  find_library(EVPath_${lib}_LIBRARY NAME ${lib} HINTS ${evpath_lib_hints})
  if(EVPath_${lib}_LIBRARY)
    if(lib MATCHES "^evpath")
      set(EVPath_LIBRARY ${EVPath_${lib}_LIBRARY})
    else()
      list(APPEND EVPath_DEPENDENCIES ${EVPath_${lib}_LIBRARY})
    endif()
  else()
    list(APPEND EVPath_DEPENDENCIES ${lib})
  endif()
endforeach()

find_package(Threads REQUIRED)
list(APPEND EVPath_DEPENDENCIES ${evpath_lib_flags} ${CMAKE_THREAD_LIBS_INIT})

###############################################################################
# FindPackage Options
###############################################################################

# handles the REQUIRED, QUIET and version-related arguments for find_package
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EVPath
  REQUIRED_VARS EVPath_LIBRARY EVPath_INCLUDE_DIR
  VERSION_VAR EVPath_VERSION
)

if(EVPath_FOUND)
  set(EVPath_INCLUDE_DIRS ${EVPath_INCLUDE_DIR})
  set(EVPath_LIBRARIES ${EVPath_LIBRARY} ${EVPath_DEPENDENCIES})
  ##########################################################################
  # Add target and dependencies to ADIOS2
  ##########################################################################
  if(NOT TARGET EVPath::EVPath)
    add_library(EVPath::EVPath UNKNOWN IMPORTED)
    set_target_properties(EVPath::EVPath PROPERTIES
      IMPORTED_LOCATION "${EVPath_LIBRARY}"
      INTERFACE_LINK_LIBRARIES "${EVPath_DEPENDENCIES}"
      INTERFACE_INCLUDE_DIRECTORIES "${EVPath_INCLUDE_DIRS}"
      INTERFACE_COMPILE_OPTIONS "${EVPath_COMPILE_OPTIONS}"
    )
  endif()
endif()
