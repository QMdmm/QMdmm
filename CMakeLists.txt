# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.19)

if (DEFINED PROJECT_NAME)
    message(FATAL_ERROR "Please build this project separately. add_subdirectory won't be supported.")
endif()

if (POLICY CMP0177)
    # no influence here
    cmake_policy(SET CMP0177 NEW)
endif()

# https://gitlab.kitware.com/cmake/cmake/-/issues/26692#note_1636912
# CMake decided to not supporting "-std:c++23preview" build flag and use "-std:c++latest"
if (MSVC AND ( CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 19.43.34808 ) )
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std:c++23preview")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "-std:c++23preview")
endif()

project(QMdmm
    VERSION 0.0.1
    LANGUAGES CXX
    DESCRIPTION "A game which is played in primary / high school"
)

get_filename_component(QMDMM_SOURCE_DIR_ABSOLUTE "${CMAKE_SOURCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(QMDMM_BINARY_DIR_ABSOLUTE "${CMAKE_BINARY_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")

file(WRITE "${CMAKE_BINARY_DIR}/casecheck.txt" "casecheck")
file(WRITE "${CMAKE_BINARY_DIR}/CaseCheck.txt" "CaseCheck")
file(READ "${CMAKE_BINARY_DIR}/casecheck.txt" QMDMM_FILESYSTEM_CASE_CHECK_RESULT)

if (QMDMM_FILESYSTEM_CASE_CHECK_RESULT STREQUAL "CaseCheck")
    string(TOLOWER "${QMDMM_SOURCE_DIR_ABSOLUTE}" QMDMM_SOURCE_DIR_ABSOLUTE_CASEFOLD)
    string(TOLOWER "${QMDMM_BINARY_DIR_ABSOLUTE}" QMDMM_BINARY_DIR_ABSOLUTE_CASEFOLD)
else()
    set(QMDMM_SOURCE_DIR_ABSOLUTE_CASEFOLD "${QMDMM_SOURCE_DIR_ABSOLUTE}")
    set(QMDMM_BINARY_DIR_ABSOLUTE_CASEFOLD "${QMDMM_BINARY_DIR_ABSOLUTE}")
endif()

if (QMDMM_SOURCE_DIR_ABSOLUTE_CASEFOLD STREQUAL QMDMM_BINARY_DIR_ABSOLUTE_CASEFOLD)
    message(FATAL_ERROR [[Please!!!!! Do not in-place build QMdmm. Instead make a separate build directory and build from there. See CMake manual for details.
        Delete CMakeCache.txt (Not CMakeLists.txt) file and CMakeFiles directory before you continue.]])
endif()

string(LENGTH "${QMDMM_SOURCE_DIR_ABSOLUTE_CASEFOLD}" QMDMM_SOURCE_DIR_ABSOLUTE_LENGTH)
string(SUBSTRING "${QMDMM_BINARY_DIR_ABSOLUTE_CASEFOLD}" 0 "${QMDMM_SOURCE_DIR_ABSOLUTE_LENGTH}" QMDMM_BINARY_DIR_ABSOLUTE_CASEFOLD_PREFIX)

if (QMDMM_BINARY_DIR_ABSOLUTE_CASEFOLD_PREFIX STREQUAL QMDMM_SOURCE_DIR_ABSOLUTE_CASEFOLD)
    message(AUTHOR_WARNING "It is a bad idea to place the build directory inside the source directory since grep-ing for the source code can accidently match build results. Consider move it out.")
endif()

include(.FsEnv.cmake OPTIONAL)

option(BUILD_SHARED_LIBS "Build shared libraries" true)
if (BUILD_SHARED_LIBS)
    set(CMAKE_POSITION_INDEPENDENT_CODE true)
endif()

option(BUILD_TESTING "Build test binaries" false)

option(QMDMM_EXPORT_PRIVATE "Export private symbols, copy headers of private classes" false)

# macOS has two ways of putting this project's programs on a machine, and they
# are not variations of one another.  One is a self-contained application
# bundle: the GUI sits at the top of the prefix, carries its own Qt and the
# programs it starts, and is what the disk image holds.  The other is the plain
# prefix layout, the programs as siblings in bin/, with Qt provided by the
# machine - the shape a distribution package or a Homebrew bottle is built out
# of.  This switch is read on macOS and nowhere else; the other platforms have
# the plain layout by construction.
if (APPLE)
    set(QMDMM_MACOS_APP_BUNDLE_DEFAULT true)
else()
    set(QMDMM_MACOS_APP_BUNDLE_DEFAULT false)
endif()
option(QMDMM_MACOS_APP_BUNDLE "Install the GUI as a self-contained application bundle on macOS" ${QMDMM_MACOS_APP_BUNDLE_DEFAULT})

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED true)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED true)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN true)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(GNUInstallDirs)
include(AutoGenerateHeader)

set(QT6_EXTRA_COMPONENTS_NEEDED)

if (BUILD_TESTING)
    list(APPEND QT6_EXTRA_COMPONENTS_NEEDED Test QuickTest)
endif()

# Note: Qt suggest users use versionless targets and functions, but if multiple Qt versions are mixed in one CMakeLists.txt, it won't be an option and versioned ones should be used.
# Previously this project is mixed with Qt 5.15 and 6.5, so we need to disable the versionless targets and functions.
set(QT_NO_CREATE_VERSIONLESS_FUNCTIONS 1)
set(QT_NO_CREATE_VERSIONLESS_TARGETS 1)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Network WebSockets Gui Qml Quick Widgets QuickWidgets ${QT6_EXTRA_COMPONENTS_NEEDED})

if (Qt6_FOUND)
    qt6_standard_project_setup(REQUIRES 6.7)
    # we don't need them to be set by default
    unset(CMAKE_AUTOMOC)
    unset(CMAKE_AUTOUIC)
    unset(CMAKE_AUTORCC)
    if (QT_KNOWN_POLICY_QTP0003)
        # Consider the BUILD_SHARED_LIBS value when creating Qt libraries.
        qt6_policy(SET QTP0003 NEW)
    endif()
    if (QT_KNOWN_POLICY_QTP0004)
        # Extra directories with QML files in a QML module need extra qmldir files.
        # Migrate as long as possible!
        qt6_policy(SET QTP0004 NEW)
    endif()
endif()

# modify the default output location
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/${CMAKE_INSTALL_BINDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build/${CMAKE_INSTALL_LIBDIR}")

# Where the runtime programs are installed.  In the bundle shape the GUI has to
# sit at the top of the prefix, because Qt's deploy commands address it as
# <name>.app relative to the prefix, and the command line programs it starts as
# child processes are installed inside it - so that the installed bundle is what
# carries them.  In the plain shape all three are siblings in bin/, which is
# also the directory the GUI has to look for them in.  The number in the bundle
# name is the Qt major version, as in the target names, so anything else
# installed inside the bundle goes through this variable rather than spelling
# the name out again.
if (APPLE AND QMDMM_MACOS_APP_BUNDLE)
    set(QMDMM6_INSTALL_BUNDLE_DIR "QMdmm6.app")
    set(QMDMM6_INSTALL_BUNDLE_DESTINATION ".")
    set(QMDMM6_INSTALL_RUNTIME_DIR "${QMDMM6_INSTALL_BUNDLE_DIR}/Contents/MacOS")
else()
    set(QMDMM6_INSTALL_BUNDLE_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    set(QMDMM6_INSTALL_RUNTIME_DIR "${CMAKE_INSTALL_BINDIR}")
endif()

if (APPLE AND NOT QMDMM_MACOS_APP_BUNDLE)
    # Nothing carries this project's libraries in the plain shape, so the
    # programs reach them through the libdir of the prefix, the way they do on
    # every other platform.  Qt is not on the target machine either, by
    # definition of this shape, so the place it was found is kept in the rpath
    # as well; a package manager that knows where it will relocate the install
    # overrides both through CMAKE_INSTALL_RPATH.
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH true)
endif()

if (WIN32)
    set(CMAKE_SHARED_MODULE_PREFIX "")
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
    if (NOT MSVC)
        # match behavior of qt6_add_library
        set(CMAKE_IMPORT_LIBRARY_SUFFIX ".a")
    endif()
endif()

include(CTest)

add_subdirectory(3rdparty)
add_subdirectory(QMdmmCore)
add_subdirectory(QMdmmNetworking)
add_subdirectory(QMdmmServer)
# QMdmmGui comes after every directory whose programs are installed inside its
# bundle: the deploy step signs the bundle once it is in place, and a file
# added afterwards leaves the signature sealing a bundle it does not describe.
add_subdirectory(QMdmmBot)
add_subdirectory(QMdmmGui)
add_subdirectory(smoke) # Networked local-game playability regression test
add_subdirectory(doc) # MAKE SURE this is the last one in the add_subdirectory list

# ---------------------------------------------------------------------------
# find_package(QMdmm6) support
# ---------------------------------------------------------------------------
#
# Responsibilities are split the way Qt splits its own CMake packages:
#
#   QMdmm6Targets.cmake            generated by install(EXPORT) - never edit
#   QMdmm6Config.cmake             hand-written, expanded from .cmake.in
#   QMdmm6ConfigVersion.cmake      hand-written shell
#   QMdmm6ConfigVersionImpl.cmake  hand-written, holds the version policy
#   QMdmm6ConfigExtras.cmake       hand-written, expanded from .cmake.in
#   QMdmm6Macros.cmake             hand-written, installed verbatim
#
# The "6" is the Qt generation this build links against, not the QMdmm product
# version: it lives in the package directory and in the QMdmm6:: namespace, so a
# Qt 7 flavour can be installed next to it as QMdmm7 without a single clash.
include(CMakePackageConfigHelpers)

set(QMDMM6_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/QMdmm6")
set(QMDMM6_CMAKE_BINARY_DIR "${CMAKE_BINARY_DIR}/cmake")
file(MAKE_DIRECTORY "${QMDMM6_CMAKE_BINARY_DIR}")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QMdmm6Config.cmake.in"
    "${QMDMM6_CMAKE_BINARY_DIR}/QMdmm6Config.cmake"
    INSTALL_DESTINATION "${QMDMM6_CMAKE_INSTALL_DIR}"
)
# Plain configure_file for the version pair: they need the product version and
# nothing else, and staying simple keeps them readable in the source tree.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QMdmm6ConfigVersion.cmake.in"
    "${QMDMM6_CMAKE_BINARY_DIR}/QMdmm6ConfigVersion.cmake"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QMdmm6ConfigVersionImpl.cmake.in"
    "${QMDMM6_CMAKE_BINARY_DIR}/QMdmm6ConfigVersionImpl.cmake"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QMdmm6ConfigExtras.cmake.in"
    "${QMDMM6_CMAKE_BINARY_DIR}/QMdmm6ConfigExtras.cmake"
    @ONLY
)

install(EXPORT QMdmm6Targets
    FILE "QMdmm6Targets.cmake"
    NAMESPACE "QMdmm6::"
    DESTINATION "${QMDMM6_CMAKE_INSTALL_DIR}"
    COMPONENT "dev6"
)
install(FILES
    "${QMDMM6_CMAKE_BINARY_DIR}/QMdmm6Config.cmake"
    "${QMDMM6_CMAKE_BINARY_DIR}/QMdmm6ConfigVersion.cmake"
    "${QMDMM6_CMAKE_BINARY_DIR}/QMdmm6ConfigVersionImpl.cmake"
    "${QMDMM6_CMAKE_BINARY_DIR}/QMdmm6ConfigExtras.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/QMdmm6Macros.cmake"
    DESTINATION "${QMDMM6_CMAKE_INSTALL_DIR}"
    COMPONENT "dev6"
)

set(CPACK_GENERATOR 7Z TGZ TXZ TZST ZIP)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_SOURCE_GENERATOR 7Z TGZ TXZ TZST ZIP)
set(CPACK_SOURCE_IGNORE_FILES
    [[\\.git/]]
    [[\\.DS_Store]]
    [[.*~$]]
    [[.*\\.autosave$]]
    [[build/]]
    [[/\\.FsEnv\\.cmake$]]
    [[CMakeLists\\.txt\\.user]]
)
set(CPACK_RPM_PACKAGE_LICENSE "AGPLv3")
set(CPACK_PACKAGE_CONTACT "Neve M. Nguyen <nemn9852@agent.qq.com>")

if (CMAKE_SYSTEM_NAME MATCHES "[Ll]inux")
    list(APPEND CPACK_GENERATOR RPM DEB)
    # Write cpack_add_component DEPENDS (dev6 -> 6 + dev-common) into DEB Depends.
    set(CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS ON)

    # Development packages are named after each distribution's own convention:
    # Debian and Ubuntu suffix "-dev", Fedora and RHEL suffix "-devel". The CPack
    # component names ("6", "dev6", "dev-common", "doc") are internal install()
    # labels and stay as they are - only the package name a component produces is
    # overridden here. CPackDEB lowercases the value itself, CPackRPM keeps it as
    # written.
    #
    # "private-dev-common" keeps its default name on purpose: it only exists when
    # QMDMM_EXPORT_PRIVATE is on, nothing consumes it and it has never shipped, so
    # its final name is still an open question rather than something to guess.
    #
    # The runtime component has to be named explicitly, even though "qmdmm-6" is
    # exactly what the default would produce: CPackDeb resolves a component's
    # dependencies through get_component_package_name(), whose fallback
    # interpolates the *current* CPACK_DEBIAN_PACKAGE_NAME. While it writes the
    # dev component's Depends that variable already holds the dev package's own
    # name, so the runtime component came out as "qmdmm-6-dev-6" and the dev
    # package was uninstallable. Naming it here keeps the emitted
    # "Depends: qmdmm-6 (= 0.0.1)" correct.
    set(CPACK_DEBIAN_6_PACKAGE_NAME "qmdmm-6")
    set(CPACK_DEBIAN_DEV6_PACKAGE_NAME "qmdmm-6-dev")
    set(CPACK_DEBIAN_DEV-COMMON_PACKAGE_NAME "qmdmm-common-dev")
    set(CPACK_RPM_DEV6_PACKAGE_NAME "qmdmm-6-devel")
    set(CPACK_RPM_DEV-COMMON_PACKAGE_NAME "qmdmm-common-devel")
    # Runtime Qt6 dependencies for the "6" (runtime) component.
    # DEB names omit the "t64" suffix: on 64-bit trixie the t64 packages
    # "Provide" the plain names, so they resolve across bookworm/trixie.
    set(CPACK_DEBIAN_6_PACKAGE_DEPENDS
        "libqt6core6, libqt6gui6, libqt6network6, libqt6websockets6, libqt6qml6, libqt6quick6, libqt6quickwidgets6, libqt6widgets6, qml6-module-qtquick")
    set(CPACK_RPM_6_PACKAGE_REQUIRES
        "qt6-qtbase, qt6-qtwebsockets, qt6-qtdeclarative")
    # Dev dependencies for "dev6": what a consumer needs to build against the
    # installed QMdmm. Both sides declare the same set - the Qt 6 development
    # modules (WebSockets and the QML side included, plus Qt's own tools, which
    # qt6_add_qml_module() and qt6_add_translations() invoke while configuring
    # and building) and CMake, without which find_package(QMdmm6) is unreachable.
    # doxygen is gone from the RPM list: it builds the doc component, it is not
    # a dependency of a development package.
    set(CPACK_DEBIAN_DEV6_PACKAGE_DEPENDS
        "qt6-base-dev, qt6-declarative-dev, qt6-websockets-dev, qt6-tools-dev, cmake")
    # CPackRPM does not derive inter-component Requires the way CPackDEB does
    # through CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS, so qmdmm-6-devel has to name
    # its runtime and headers packages itself. Without those two, installing the
    # devel RPM leaves the shared library, the headers and the CMake package
    # config behind.
    set(CPACK_RPM_DEV6_PACKAGE_REQUIRES
        "cmake, qt6-qtbase-devel, qt6-qtdeclarative-devel, qt6-qtwebsockets-devel, qt6-qttools-devel, qmdmm-6, qmdmm-common-devel")
    # dev-common / doc intentionally carry no Qt dependencies: dev-common is
    # version-agnostic headers only. CPack emits one harmless
    # "CPACK_DEBIAN_PACKAGE_DEPENDS not set" note for dev-common - expected.
endif()

if (APPLE AND QMDMM_MACOS_APP_BUNDLE)
    # A disk image is the macOS form of the runtime artefact, and it exists to
    # carry the bundle.  What the image holds, and the adjustment its staging
    # tree needs, are set per generator in cmake/CPackProjectConfig.cmake, so
    # that the tar archives keep installing every component.
    list(APPEND CPACK_GENERATOR DragNDrop)
    set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPackProjectConfig.cmake")
endif()

include(CPack)
cpack_add_component("6")
cpack_add_component("dev-common")
cpack_add_component("dev6" DEPENDS "6" "dev-common")
cpack_add_component("private-dev-common" DEPENDS "dev-common")
cpack_add_component("doc" DEPENDS "dev-common")
