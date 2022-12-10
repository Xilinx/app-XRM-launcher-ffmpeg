# Copyright (C) 2021, Xilinx Inc - All rights reserved
# Xilinx Worker-Launcher Application

# Licensed under the Apache License, Version 2.0 (the "License"). You may
# not use this file except in compliance with the License. A copy of the
# License is located at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations
# under the License.

cmake_minimum_required(VERSION 2.8.11)
set(LAUNCHER_PROJ "xilinx-u30-launcher")
set(LAUNCHER_BINARY_NAME "launcher")
set(LAUNCHER_VERSION "1.1.1")

find_program(UNAME_EXEC uname)
find_program(LSB_RELEASE_EXEC lsb_release)

execute_process(COMMAND ${LSB_RELEASE_EXEC} -is
    OUTPUT_VARIABLE LINUX_FLAVOR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (${LINUX_FLAVOR} MATCHES "^(Ubuntu|Debian)")
        # amd64
        execute_process(
        COMMAND dpkg --print-architecture
        OUTPUT_VARIABLE CPACK_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        )
elseif (${LINUX_FLAVOR} MATCHES "^(RedHat|CentOS|Amazon|Fedora|SUSE)")
        # x86_64
        execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE CPACK_ARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        )
else()
        message( SEND_ERROR "Trying to build on incompatible system \"${LINUX_FLAVOR}\"." )
endif()

# Grab OS release short name
execute_process(COMMAND ${LSB_RELEASE_EXEC} -rs
        OUTPUT_VARIABLE LSB_RELEASE_REL_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE)

if (${LINUX_FLAVOR} MATCHES "^Amazon")
        set(LSB_RELEASE_REL_SHORT "amzn${LSB_RELEASE_REL_SHORT}")
endif()

set(LAUNCHER_PROJ_FULL_NAME "${LAUNCHER_PROJ}_${LAUNCHER_VERSION}_${LSB_RELEASE_REL_SHORT}-${CPACK_ARCH}")

project(${LAUNCHER_PROJ})

# Linking variables
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBXRM REQUIRED libxrm)


install(DIRECTORY ${CMAKE_SOURCE_DIR}/describe_job/
        USE_SOURCE_PERMISSIONS
        DESTINATION ${CMAKE_INSTALL_PREFIX}/scripts/describe_job)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/vvas_only_describe_job/
        USE_SOURCE_PERMISSIONS
        DESTINATION ${CMAKE_INSTALL_PREFIX}/scripts/vvas_only_describe_job)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/run_params/
        USE_SOURCE_PERMISSIONS
        DESTINATION ${CMAKE_INSTALL_PREFIX}/scripts/run_params)

# VVAS GStreamer commands for launcher
install(DIRECTORY ${CMAKE_SOURCE_DIR}/vvas_run_params/
	USE_SOURCE_PERMISSIONS
	DESTINATION ${CMAKE_INSTALL_PREFIX}/scripts/vvas_run_params)

find_package(Threads REQUIRED)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

find_package(Boost COMPONENTS thread)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_FLAGS	"${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -DDSA64 -fpermissive")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_FILE_LICENSE_PATH "/usr/share/doc/xilinx/u30")

add_executable(${LAUNCHER_BINARY_NAME} parser.cpp worker.cpp)

target_compile_options(${LAUNCHER_BINARY_NAME} PUBLIC ${LIBXRM_CFLAGS} "-pthread")

target_include_directories(${LAUNCHER_BINARY_NAME} PUBLIC ${LIBXRM_INCLUDE_DIRS})

target_link_libraries(${LAUNCHER_BINARY_NAME} ${LIBXRM_LDFLAGS}
  		      ${Boost_THREAD_LIBRARY}
		      ${CMAKE_THREAD_LIBS_INIT})

# Set the location for library installation
install(TARGETS ${LAUNCHER_BINARY_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(FILES ${CPACK_RESOURCE_FILE_LICENSE} CONFIGURATIONS Release RUNTIME DESTINATION ${CPACK_FILE_LICENSE_PATH}/${LAUNCHER_PROJ})

# Packaging section
set(CPACK_PACKAGE_NAME ${LAUNCHER_PROJ})
set(CPACK_PACKAGE_VERSION ${LAUNCHER_VERSION})
set(CPACK_PACKAGE_FILE_NAME ${LAUNCHER_PROJ_FULL_NAME})
set(CPACK_PACKAGE_CONTACT "Rohit Athavale <rohit.athavale@xilinx.com>")
set(CPACK_PACKAGE_VENDOR "Xilinx")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Xilinx Worker/Launcher")
set(CPACK_PACKAGE_DESCRIPTION "The Xilinx Worker/Launcher for starting FFmpeg commands using XRM.")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "xrm (>= 1.3), xilinx-u30-xffmpeg (>= 4.4.xlnx.2), libboost-thread-dev")
set(CPACK_RPM_PACKAGE_REQUIRES   "xrm  >= 1.3,  xilinx-u30-xffmpeg  >= 4.4.xlnx.2,  boost-devel")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/opt" "/opt/xilinx" "/usr/lib" "/usr/lib/pkgconfig" "/usr/share/doc/xilinx" "/usr/share/doc/xilinx/u30")
include(CPack)
