#
# Copyright (C) 2017 Cybernetica
#
# Research/Commercial License Usage
# Licensees holding a valid Research License or Commercial License
# for the Software may use this file according to the written
# agreement between you and Cybernetica.
#
# GNU General Public License Usage
# Alternatively, this file may be used under the terms of the GNU
# General Public License version 3.0 as published by the Free Software
# Foundation and appearing in the file LICENSE.GPL included in the
# packaging of this file.  Please review the following information to
# ensure the GNU General Public License version 3.0 requirements will be
# met: http://www.gnu.org/copyleft/gpl-3.0.html.
#
# For further information, please contact us at sharemind@cyber.ee.
#

CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
PROJECT(SharemindLibConfiguration VERSION 0.2.0 LANGUAGES CXX)

INCLUDE("${CMAKE_CURRENT_SOURCE_DIR}/config.local" OPTIONAL)
INCLUDE("${CMAKE_CURRENT_BINARY_DIR}/config.local" OPTIONAL)

FIND_PACKAGE(SharemindCMakeHelpers REQUIRED)

SharemindSetProjectVersion()


SET(Boost_USE_STATIC_RUNTIME OFF)

# Note: the library also uses Boost::iostreams but that component is not listed
# here, because only the header-only part of Boost::iostreams is used. We do
# not want to link against libboost_iostreams and libboost_regex.
FIND_PACKAGE(Boost COMPONENTS filesystem REQUIRED)
FIND_PACKAGE(SharemindCxxHeaders REQUIRED)


# Headers:
FILE(GLOB_RECURSE SharemindLibConfiguration_HEADERS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
FILE(GLOB_RECURSE SharemindLibConfiguration_PRIVATE_HEADERS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*_p.h")
LIST(REMOVE_ITEM SharemindLibConfiguration_HEADERS
     ${SharemindLibConfiguration_PRIVATE_HEADERS})
INSTALL(FILES ${SharemindLibConfiguration_HEADERS}
        DESTINATION "include/sharemind/libconfiguration"
        COMPONENT "dev")


# The library:
SharemindNewUniqueList(SharemindLibConfiguration_LINK_LIBRARIES
    ${Boost_LIBRARIES}
    ${SharemindCxxHeaders_LIBRARIES}
)
SharemindNewUniqueList(SharemindLibConfiguration_INCLUDE_DIRS
    ${Boost_INCLUDE_DIRS}
    ${SharemindCxxHeaders_INCLUDE_DIRS}
)
SharemindNewUniqueList(SharemindLibConfiguration_DEFINITIONS
    ${SharemindCxxHeaders_DEFINITIONS}
)
SharemindSetCxx11CompileOptions()
FILE(GLOB_RECURSE SharemindLibConfiguration_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
SharemindAddSharedLibrary("libconfiguration"
    OUTPUT_NAME "sharemind_configuration"
    SOURCES
        ${SharemindLibConfiguration_SOURCES}
        ${SharemindLibConfiguration_HEADERS}
        ${SharemindLibConfiguration_PRIVATE_HEADERS}
    INCLUDE_DIRECTORIES
        ${SharemindLibConfiguration_INCLUDE_DIRS}
    LEGACY_DEFINITIONS
        ${SharemindLibConfiguration_DEFINITIONS}
    LINK_LIBRARIES
        ${SharemindLibConfiguration_LINK_LIBRARIES}
)


# Install CMake files:
SharemindCreateCMakeFindFiles(
    INCLUDE_DIRS
        "${CMAKE_INSTALL_PREFIX}/include"
        ${SharemindLibConfiguration_INCLUDE_DIRS}
    DEFINITIONS
        ${SharemindLibConfiguration_DEFINITIONS}
    LIBRARIES
        ${SharemindLibConfiguration_LINK_LIBRARIES}
        "-L${CMAKE_INSTALL_PREFIX}/lib"
        "-lsharemind_configuration"
)


# Tests:
SharemindAddSimpleTests("${CMAKE_CURRENT_SOURCE_DIR}/tests/Test*.cpp"
    INCLUDE_DIRECTORIES
        ${SharemindLibConfiguration_INCLUDE_DIRS}
    LEGACY_DEFINITIONS
        ${SharemindLibConfiguration_DEFINITIONS}
    LINK_LIBRARIES
        ${SharemindLibConfiguration_LINK_LIBRARIES}
        "libconfiguration"
)


# Packaging:
SharemindSetupPackaging()
SET(BV
    "${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION}"
)

SharemindAddComponentPackage("lib"
    NAME "libsharemind-configuration"
    DESCRIPTION "Sharemind configuration library"
    DEB_SECTION "libs"
    DEB_DEPENDS
        "libboost-filesystem${BV}"
        "libboost-system${BV}"
        "libstdc++6 (>= 4.8.0)"
        "libc6 (>= 2.19)"
)
SharemindAddComponentPackage("dev"
    NAME "libsharemind-configuration-dev"
    DESCRIPTION "Sharemind configuration library development headers"
    DEB_SECTION "libdevel"
    DEB_DEPENDS
        "libsharemind-configuration (= ${SharemindLibConfiguration_VERSION})"
        "libsharemind-cxxheaders-dev (>= 0.1.0)"
        "libboost-dev (>= 1.50)"
        "libc6-dev (>= 2.19)"
        "libstdc++-dev"
)
SharemindAddComponentPackage("debug"
    NAME "libsharemind-configuration-dbg"
    DESCRIPTION "Sharemind configuration library debug symbols"
    DEB_SECTION "debug"
    DEB_DEPENDS
        "libsharemind-configuration (= ${SharemindLibConfiguration_VERSION})"
)
SharemindPackagingFinalize()
