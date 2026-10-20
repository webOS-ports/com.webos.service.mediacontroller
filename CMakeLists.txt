# Copyright (c) 2020-2024 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(com.webos.service.mediacontroller CXX)

# Off unless asked for: neither belongs in what ships.
# MCS_TEST_METHODS registers testKeyEvent, which sends any key event to any
# session and is in no ACG group. It used to come in with a forced
# CMAKE_BUILD_TYPE Debug, overriding whatever the build system asked for.
# MCS_MANUAL_TEST_CLIENTS builds and installs the hand-driven clients in
# test/, along with a test service holding oem trust and "*" outbound.
option(MCS_TEST_METHODS "Register the testKeyEvent bus method" OFF)
option(MCS_MANUAL_TEST_CLIENTS "Build and install the manual test clients in test/" OFF)
# getMediaCoverArtPath: fetching cover art over http(s) into a cache. Off,
# as nothing on LuneOS calls it and a media session service has no need to
# make network requests on a caller's say-so.
option(MCS_COVER_ART "Build getMediaCoverArtPath, the cover art fetcher (needs libcurl)" OFF)
set(MCS_COVER_ART_DIR "/var/cache/mediacontroller" CACHE STRING
    "The cover art cache: systemd's CacheDirectory=mediacontroller")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(webOS/webOS)
webos_modules_init(1 6 3)

webos_component(1 0 0)

include(FindPkgConfig)

if(MCS_COVER_ART)
pkg_check_modules(CURL REQUIRED libcurl>=7.85)
include_directories(${CURL_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${CURL_CFLAGS_OTHER})
add_definitions(-DMCS_COVER_ART=1 -DMCS_COVER_ART_DIR="${MCS_COVER_ART_DIR}")
else()
add_definitions(-DMCS_COVER_ART=0)
endif()

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS_OTHER})

pkg_check_modules(LS2 REQUIRED luna-service2>=3)
include_directories(${LS2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LS2_CFLAGS_OTHER})

if(MCS_MANUAL_TEST_CLIENTS)
# The manual test clients use the C++ wrapper; the service does not.
pkg_check_modules(LS2++ REQUIRED luna-service2++>=3)
include_directories(${LS2++_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LS2++_CFLAGS})
endif()

pkg_check_modules(PMLOGLIB REQUIRED PmLogLib)
include_directories(${PMLOGLIB_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PMLOGLIB_CFLAGS_OTHER})

pkg_check_modules(PBNJSON_CPP REQUIRED pbnjson_cpp)
include_directories(${PBNJSON_CPP_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PBNJSON_CPP_CFLAGS})

if(MCS_TEST_METHODS)
add_definitions(-DUSE_TEST_METHOD)
endif()

webos_add_compiler_flags(ALL -Wall -Wextra -funwind-tables -Werror=return-type)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/files/)
if(${USE_NEW_ACG})
message("USE_NEW_ACG is ${USE_NEW_ACG}")
include_directories(${CMAKE_SOURCE_DIR}/files/sysbus_new/)
else()
include_directories(${CMAKE_SOURCE_DIR}/files/sysbus/)
endif()
include_directories(${CMAKE_SOURCE_DIR}/files/systemd/)

include_directories(${CMAKE_SOURCE_DIR}/include/)

include_directories(${CMAKE_SOURCE_DIR}/src/coverArt/)

if(MCS_MANUAL_TEST_CLIENTS)
add_subdirectory(${CMAKE_SOURCE_DIR}/test/)
endif()

option(BUILD_TESTING "Build the unit tests in tests/" OFF)
if(BUILD_TESTING)
# libstdc++'s own checks (front() of an empty list and the like) abort
# in the tests instead of passing unseen.
add_definitions(-D_GLIBCXX_ASSERTIONS)
endif()

# Everything but main(), so the tests can link the service's own objects.
set(SRC_LIST
    ${CMAKE_SOURCE_DIR}/src/MediaControlService.cpp
    ${CMAKE_SOURCE_DIR}/src/MediaSessionManager.cpp
    ${CMAKE_SOURCE_DIR}/src/MediaControlPrivate.cpp
    ${CMAKE_SOURCE_DIR}/src/RequestReceiver.cpp
    ${CMAKE_SOURCE_DIR}/src/Lsutils.cpp
   )
if(MCS_COVER_ART)
list(APPEND SRC_LIST ${CMAKE_SOURCE_DIR}/src/coverArt/CoverArtFetcher.cpp)
endif()

add_library(mcs-core OBJECT ${SRC_LIST})

add_executable(${PROJECT_NAME}
               $<TARGET_OBJECTS:mcs-core>
               ${CMAKE_SOURCE_DIR}/src/main.cpp)
# -rdynamic, so a backtrace names the daemon's own functions. It used to be
# passed as a compiler flag, where it did nothing.
set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)

target_link_libraries(${PROJECT_NAME}
                      ${GLIB2_LDFLAGS}
                      ${PBNJSON_CPP_LDFLAGS}
                      ${LS2_LDFLAGS}
                      ${PMLOGLIB_LDFLAGS}
                      ${CURL_LDFLAGS}
                      pthread)

if(MCS_MANUAL_TEST_CLIENTS)
#MediaControllerServiceTest project exe
set (SRC_TEST ${CMAKE_SOURCE_DIR}/test/MediaControllerServiceTest.cpp)
add_executable (MCSTestApp ${SRC_TEST})
install(TARGETS MCSTestApp DESTINATION ${WEBOS_INSTALL_TESTSDIR}/${PROJECT_NAME})

#mediacontrollerkeyevent project exe
set (SRC_KEY_TEST ${CMAKE_SOURCE_DIR}/test/MediaControllerKeyEventTest.cpp)
add_executable (MCSKeyEventTestApp ${SRC_KEY_TEST})
install(TARGETS MCSKeyEventTestApp DESTINATION ${WEBOS_INSTALL_TESTSDIR}/${PROJECT_NAME})
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION ${WEBOS_INSTALL_SBINDIR})

# The API files list what the build registers, and nothing else.
if(MCS_COVER_ART)
set(MCS_API_COVER_ART_PATH ",\n    \"com.webos.service.mediacontroller/getMediaCoverArtPath\"")
else()
set(MCS_API_COVER_ART_PATH "")
endif()
if(MCS_TEST_METHODS)
set(MCS_API_TEST_KEY_EVENT ",\n    \"com.webos.service.mediacontroller/testKeyEvent\"")
else()
set(MCS_API_TEST_KEY_EVENT "")
endif()
if(${USE_NEW_ACG})
    message("USE_NEW_ACG is ${USE_NEW_ACG}")
    webos_build_system_bus_files(${CMAKE_SOURCE_DIR}/files/sysbus_new)
else()
    webos_build_system_bus_files(${CMAKE_SOURCE_DIR}/files/sysbus)
endif()

if(BUILD_TESTING)
enable_testing()
add_subdirectory(tests)
endif()
