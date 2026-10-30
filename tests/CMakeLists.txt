# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

# The service's own objects, linked with an in-process luna-service2
# (fakes/fakebus.cpp) instead of the real library.

find_package(GTest REQUIRED)

set(MCS_SANITIZE "" CACHE STRING
    "Sanitizers for the test build, as -fsanitize= takes them (address,undefined or thread)")
set(MCS_NO_SANITIZE "" CACHE STRING
    "Checks to leave out of MCS_SANITIZE, as -fno-sanitize= takes them")

set(MCS_TEST_SOURCES
    fakes/fakebus.cpp
    unit/test_lifecycle.cpp
    unit/test_sessions.cpp
    unit/test_focus.cpp
    unit/test_replies.cpp
    unit/test_bluetooth.cpp
    unit/test_coverart.cpp)

add_executable(mcs-tests
    $<TARGET_OBJECTS:mcs-core>
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${MCS_TEST_SOURCES})

target_include_directories(mcs-tests PRIVATE fakes unit)
target_compile_definitions(mcs-tests PRIVATE
    MCS_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    MCS_NO_MAIN)

# No luna-service2 here: the fake is the only one.
target_link_libraries(mcs-tests
    GTest::gtest_main
    GTest::gtest
    ${GLIB2_LDFLAGS}
    ${PBNJSON_CPP_LDFLAGS}
    ${PMLOGLIB_LDFLAGS}
    ${CURL_LDFLAGS}
    pthread)

if (MCS_SANITIZE)
    set(MCS_SANITIZE_FLAGS -fsanitize=${MCS_SANITIZE})
    if (MCS_NO_SANITIZE)
        list(APPEND MCS_SANITIZE_FLAGS -fno-sanitize=${MCS_NO_SANITIZE})
    endif()
    foreach(t mcs-core mcs-tests)
        target_compile_options(${t} PRIVATE ${MCS_SANITIZE_FLAGS} -fno-omit-frame-pointer
                               -fno-sanitize-recover=all)
    endforeach()
    target_link_options(mcs-tests PRIVATE ${MCS_SANITIZE_FLAGS})
endif()

add_test(NAME mcs-tests COMMAND mcs-tests)
