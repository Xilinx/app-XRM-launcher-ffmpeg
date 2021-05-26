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
project(launcher)

# Linking variables
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBXRM REQUIRED libxrm)


install(DIRECTORY ${CMAKE_SOURCE_DIR}/describe_job/
        USE_SOURCE_PERMISSIONS
        DESTINATION ${CMAKE_INSTALL_PREFIX}/scripts/describe_job)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/run_params/
        USE_SOURCE_PERMISSIONS
        DESTINATION ${CMAKE_INSTALL_PREFIX}/scripts/run_params)

find_package(Threads REQUIRED)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

find_package(Boost COMPONENTS thread)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_FLAGS	"${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -O1 -g  -DDSA64 -fpermissive")

add_executable(${PROJECT_NAME} parser.cpp worker.cpp)

target_compile_options(${PROJECT_NAME} PUBLIC ${LIBXRM_CFLAGS} "-pthread")

target_include_directories(${PROJECT_NAME} PUBLIC ${LIBXRM_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} ${LIBXRM_LDFLAGS} 
  		      ${Boost_THREAD_LIBRARY}
		      ${CMAKE_THREAD_LIBS_INIT})

# Set the location for library installation
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

# Packaging section
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_CONTACT "Rohit Athavale <rohit.athavale@xilinx.com>")
set(CPACK_PACKAGE_VENDOR "Xilinx")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Xilinx Worker/Launcher")
set(CPACK_PACKAGE_DESCRIPTION "The Xilinx Worker/Launcher for starting FFmpeg commands using XRM.")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "xrm (>= 1.2.1157), libboost-thread-dev ")
set(CPACK_RPM_PACKAGE_DEPENDS "xrm >= 1.2.1157, libboost-thread1.58.0 ")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/opt" "/opt/xilinx" "/usr/lib" "/usr/lib/pkgconfig")
include(CPack)
