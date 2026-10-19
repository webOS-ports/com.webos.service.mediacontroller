# Copyright (c) 2020 LG Electronics, Inc.
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

IF(NOT DEFINED SERVICE_NAME)
set(SERVICE_NAME com.webos.service.mediacontrollertest)
endif(NOT DEFINED SERVICE_NAME)
add_definitions(-DSERVICE_NAME="${SERVICE_NAME}")

project(com.webos.service.mediacontrollertest CXX)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/files/)
include_directories(${CMAKE_SOURCE_DIR}/files/sysbus/)

set(SRC_LIST MCSClient.cpp)

configure_file("files/sysbus/com.webos.service.mediacontrollertest.role.json.in" "${SERVICE_NAME}.role.json")
configure_file("files/sysbus/com.webos.service.mediacontrollertest.perm.json.in" "${SERVICE_NAME}.perm.json")
configure_file("files/sysbus/com.webos.service.mediacontrollertest.service.in" "${SERVICE_NAME}.service")
configure_file("files/sysbus/com.webos.service.mediacontrollertest.manifest.json.in" "${SERVICE_NAME}.manifest.json")

install (
  FILES "${CMAKE_CURRENT_BINARY_DIR}/${SERVICE_NAME}.role.json"
  DESTINATION "${WEBOS_INSTALL_SYSBUS_ROLESDIR}"
)

install (
  FILES "${CMAKE_CURRENT_BINARY_DIR}/${SERVICE_NAME}.service"
  DESTINATION "${WEBOS_INSTALL_SYSBUS_SERVICESDIR}"
)

install (
  FILES "${CMAKE_CURRENT_BINARY_DIR}/${SERVICE_NAME}.perm.json"
  DESTINATION "${WEBOS_INSTALL_SYSBUS_PERMISSIONSDIR}"
)

install (
  FILES "${CMAKE_CURRENT_BINARY_DIR}/${SERVICE_NAME}.manifest.json"
  DESTINATION "${WEBOS_INSTALL_SYSBUS_MANIFESTSDIR}"
)

add_executable(${PROJECT_NAME} ${SRC_LIST})

target_link_libraries(${PROJECT_NAME}
                      pthread
                      ${GLIB2_LDFLAGS}
                      ${PBNJSON_CPP_LDFLAGS}
                      ${LS2++_LDFLAGS})

install(TARGETS ${PROJECT_NAME} DESTINATION ${WEBOS_INSTALL_TESTSDIR}/com.webos.service.mediacontroller)
webos_build_system_bus_files()
