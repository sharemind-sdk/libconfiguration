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
PROJECT(SharemindLibConfiguration VERSION 0.6.0 LANGUAGES CXX)

INCLUDE("${CMAKE_CURRENT_SOURCE_DIR}/config.local" OPTIONAL)
INCLUDE("${CMAKE_CURRENT_BINARY_DIR}/config.local" OPTIONAL)

FIND_PACKAGE(SharemindCMakeHelpers 1.6 REQUIRED)

# Note: the library also uses Boost::iostreams but that component is not listed
# here, because only the header-only part of Boost::iostreams is used. We do
# not want to link against libboost_iostreams and libboost_regex.
FIND_PACKAGE(Boost 1.62 COMPONENTS filesystem REQUIRED)
FIND_PACKAGE(SharemindCxxHeaders 0.9.0 REQUIRED)


# LibConfiguration:
FILE(GLOB_RECURSE SharemindLibConfiguration_HEADERS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
FILE(GLOB_RECURSE SharemindLibConfiguration_PRIVATE_HEADERS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*_p.h")
LIST(REMOVE_ITEM SharemindLibConfiguration_HEADERS
     ${SharemindLibConfiguration_PRIVATE_HEADERS})
INSTALL(FILES ${SharemindLibConfiguration_HEADERS}
        DESTINATION "include/sharemind/libconfiguration"
        COMPONENT "dev")
FILE(GLOB_RECURSE SharemindLibConfiguration_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
SharemindAddSharedLibrary(LibConfiguration
    OUTPUT_NAME "sharemind_configuration"
    SOURCES
        ${SharemindLibConfiguration_SOURCES}
        ${SharemindLibConfiguration_HEADERS}
        ${SharemindLibConfiguration_PRIVATE_HEADERS}
)
TARGET_INCLUDE_DIRECTORIES(LibConfiguration
    INTERFACE
        # $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> # TODO
        $<INSTALL_INTERFACE:include>
    )
TARGET_LINK_LIBRARIES(LibConfiguration
    PRIVATE
        "Boost::filesystem"
    PUBLIC
        "Boost::boost"
        "Sharemind::CxxHeaders"
    )
SharemindCreateCMakeFindFilesForTarget(LibConfiguration
    DEPENDENCIES
        "Boost 1.62"
        "SharemindCxxHeaders 0.9.0"
    )


# Tests:
FILE(GLOB LibConfiguration_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/Test*.cpp")
FOREACH(testFile IN LISTS LibConfiguration_TESTS)
    GET_FILENAME_COMPONENT(testName "${testFile}" NAME_WE)
    SharemindAddTest("${testName}" SOURCES "${testFile}")
    TARGET_LINK_LIBRARIES("${testName}" PRIVATE LibConfiguration)
ENDFOREACH()


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
        "libsharemind-configuration (= ${SharemindLibConfiguration_DEB_lib_PACKAGE_VERSION})"
        "libsharemind-cxxheaders-dev (>= 0.9.0)"
        "libboost-dev (>= 1.62)"
        "libc6-dev (>= 2.19)"
        "libstdc++-dev"
)
SharemindAddComponentPackage("debug"
    NAME "libsharemind-configuration-dbg"
    DESCRIPTION "Sharemind configuration library debug symbols"
    DEB_SECTION "debug"
    DEB_DEPENDS
        "libsharemind-configuration (= ${SharemindLibConfiguration_DEB_lib_PACKAGE_VERSION})"
)
SharemindPackagingFinalize()
