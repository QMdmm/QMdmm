# SPDX-License-Identifier: AGPL-3.0-or-later

set(QMDMMCORE_PCH
    src/qmdmmcoreglobal.h
)

set(QMDMMCORE_HEADERS
    ${QMDMMCORE_PCH}
    src/qmdmmprotocol.h
    src/qmdmmroom.h
    src/qmdmmplayer.h
    src/qmdmmlogic.h
    src/qmdmmdebug.h
    src/qmdmmsettings.h
)

set(QMDMMCORE_PRIVATE_HEADERS
    src/qmdmmroom_p.h
    src/qmdmmplayer_p.h
    src/qmdmmlogic_p.h
    src/qmdmmdebug_p.h
    src/qmdmmsettings_p.h
)

set(QMDMMCORE_SOURCES
    src/qmdmmcore.cpp
    src/qmdmmprotocol.cpp
    src/qmdmmroom.cpp
    src/qmdmmplayer.cpp
    src/qmdmmlogic.cpp
    src/qmdmmdebug.cpp
    src/qmdmmsettings.cpp
)

set(QMDMMCORE_PRIVATE_SOURCES
    src/qmdmmdebug_p.cpp
    src/qmdmmlogic_p.cpp
    src/qmdmmplayer_p.cpp
    src/qmdmmsettings_p.cpp
)

set(QMDMMCORE_DOC_FILES ${QMDMMCORE_HEADERS} ${QMDMMCORE_SOURCES} PARENT_SCOPE)

# Two of the three shapes carry the configuration and runtime data directories as definitions,
# and what such a definition holds is a recipe for a directory rather than a directory:
# QMdmmCore::Global turns it into a path at run time, against the machine the programs
# actually run on rather than the one they were built on. The application bundle is the third
# shape and carries neither: it is not installed under a prefix, so there is no recipe to
# write. It asks the platform where an application's own directory goes and names it after the
# bundle's identifier instead -- see Global::configurationDirectory().
if (QMDMM_MACOS_APP_BUNDLE)
    # The two prefixes above are deliberately left unset: in this shape the directory is not
    # derived from a prefix, and the identifier below is what names it. Nothing in this
    # repository's CI configures this shape -- its configure step never passes
    # QMDMM_MACOS_APP_BUNDLE -- so a change made in this branch is not covered by a build
    # there; the shape is exercised by the packaging CI's dmg line instead.
elseif (UNIX AND ( ( CMAKE_INSTALL_PREFIX STREQUAL "/usr" ) OR ( CMAKE_INSTALL_PREFIX STREQUAL "/" ) ) )
    # The executable sits in /usr/bin there while the directories are /etc and /var, so the
    # relative shape would name /usr/etc and /usr/var: the two prefixes that have an FHS
    # answer keep the absolute one.
    set(QMDMM_CONFIGURATION_PREFIX "/etc/${PROJECT_NAME}")
    set(QMDMM_RUNTIME_DATA_PREFIX "/var/${PROJECT_NAME}")
else()
    # Relative to the directory the executable is installed into, computed from the install
    # layout here rather than assumed at run time: the whole prefix can then be moved
    # afterwards, and the paths follow it.
    file(RELATIVE_PATH QMDMM_CONFIGURATION_PREFIX
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}/${PROJECT_NAME}"
    )
    file(RELATIVE_PATH QMDMM_RUNTIME_DATA_PREFIX
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LOCALSTATEDIR}/${PROJECT_NAME}"
    )
endif()

add_library(QMdmmCore INTERFACE)
auto_generate_header_file_for_target(QMdmmCore "${QMDMMCORE_HEADERS}")
install(FILES ${QMDMMCORE_HEADERS}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/QMdmmCore"
    COMPONENT "dev-common"
)
# This INTERFACE library carries the compile definitions and include directories
# that consumers need, and QMdmmCore6 consumes it PUBLIC-ly, so it has to be part
# of the exported package - otherwise install(EXPORT) refuses to write
# QMdmmCore6 into it.
install(TARGETS QMdmmCore
    EXPORT QMdmm6Targets
    COMPONENT "dev-common"
)
if (NOT BUILD_SHARED_LIBS)
    target_compile_definitions(QMdmmCore
        INTERFACE QMDMM_STATIC
    )
endif()
target_compile_definitions(QMdmmCore
    INTERFACE "QT_NO_CAST_FROM_ASCII"
              "QT_DISABLE_DEPRECATED_BEFORE=0x70000"
              "QMDMM_VERSION=\"${QMdmm_VERSION}\""
)

# BUILD_INTERFACE / INSTALL_INTERFACE are correctness, not cosmetics: the
# build-tree include root and the source-relative `src` are machine-local paths,
# and install(EXPORT) either rejects the relative one outright or bakes this
# build directory into the installed package.
target_include_directories(QMdmmCore
    INTERFACE "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/build/include>"
              "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
              # <QMdmmCore/QMdmmPlayer> resolves through the include root;
              # the bare <QMdmmPlayer> through QMdmmCore6's own entry below.
              "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
if (NOT MSVC)
    target_compile_options(QMdmmCore
        INTERFACE "$<$<CONFIG:Debug>:--coverage>"
    )
    target_link_options(QMdmmCore
        INTERFACE "$<$<CONFIG:Debug>:--coverage>"
    )
else()
    if ( ( Debug IN LISTS CMAKE_CONFIGURATION_TYPES ) OR ( CMAKE_BUILD_TYPE STREQUAL Debug ) )
        message(AUTHOR_WARNING "Coverage generation is not supported in MSVC")
    endif()
endif()
get_target_property(QMDMMCORE_GENERATED_FILES QMdmmCore SOURCES)
if (NOT QMDMMCORE_GENERATED_FILES)
    unset(QMDMMCORE_GENERATED_FILES)
endif()

# https://doc.qt.io/qt-6/cmake-qt5-and-qt6-compatibility.html#unicode-support-in-windows
if (WIN32)
    target_compile_definitions(QMdmmCore
        INTERFACE UNICODE
                  _UNICODE
    )
endif()
if (NOT QMDMM_MACOS_APP_BUNDLE)
    target_compile_definitions(QMdmmCore
        INTERFACE QMDMM_CONFIGURATION_PREFIX="${QMDMM_CONFIGURATION_PREFIX}"
                  QMDMM_RUNTIME_DATA_PREFIX="${QMDMM_RUNTIME_DATA_PREFIX}"
    )
else()
    # The identifier is the same string the bundle is identified by, so the directory this
    # shape puts the configuration and the logs in is named after the application itself --
    # one directory for the GUI, the server and the bot, none of which has an identity of its
    # own here.
    target_compile_definitions(QMdmmCore
        INTERFACE QMDMM_MACOS_APP_BUNDLE=1
                  QMDMM_MACOS_BUNDLE_IDENTIFIER="${QMDMM_MACOS_BUNDLE_IDENTIFIER}"
    )
endif()
if (QMDMM_EXPORT_PRIVATE)
    if (BUILD_SHARED_LIBS)
        target_compile_definitions(QMdmmCore
            INTERFACE QMDMM_NEED_EXPORT_PRIVATE
        )
    endif()
    add_library(QMdmmCorePrivate INTERFACE)
    target_sources(QMdmmCorePrivate PRIVATE ${QMDMMCORE_PRIVATE_HEADERS})
    install(FILES ${QMDMMCORE_PRIVATE_HEADERS}
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/QMdmmCore/private/${PROJECT_VERSION}"
        COMPONENT "private-dev-common"
    )
    target_link_libraries(QMdmmCorePrivate
        INTERFACE QMdmmCore
    )
endif()

if (MSVC)
    target_compile_options(QMdmmCore
        INTERFACE "-W4"
                  "-wd4251"
                  "-wd4275"
                  "-we4596" # Needed for standard compliance
    )
else()
    target_compile_options(QMdmmCore
        INTERFACE "-Wall"
                  "-Wextra"
    )
endif()

if (Qt6_FOUND)
    qt6_add_library(QMdmmCore6 ${QMDMMCORE_HEADERS} ${QMDMMCORE_SOURCES} ${QMDMMCORE_PRIVATE_HEADERS} ${QMDMMCORE_PRIVATE_SOURCES})
    add_library(QMdmm6::Core ALIAS QMdmmCore6)

    set_target_properties(QMdmmCore6 PROPERTIES
        # Exported as QMdmm6::Core: the generation lives in the namespace, not in
        # the target name consumers write.
        EXPORT_NAME "Core"
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
        VERSION "${QMdmm_VERSION}"
    )

    target_precompile_headers(QMdmmCore6
        PRIVATE ${QMDMMCORE_PCH}
    )

    if (BUILD_SHARED_LIBS)
        target_compile_definitions(QMdmmCore6
            PRIVATE QMDMMCORE_LIBRARY
        )
    endif()

    target_include_directories(QMdmmCore6
        INTERFACE "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/build/include/QMdmmCore>"
                  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/QMdmmCore>"
    )

    target_link_libraries(QMdmmCore6
        PUBLIC QMdmmCore
               Qt6::Core
    )

    install(TARGETS QMdmmCore6
        EXPORT QMdmm6Targets
        COMPONENT "6"
        RUNTIME
        LIBRARY
        NAMELINK_COMPONENT "dev6"
    )

    # On Linux/macOS with BUILD_SHARED_LIBS=ON, the ARCHIVE rule would install
    # the real .so plus all version symlinks into dev6, duplicating the "6"
    # component. Keep ARCHIVE for Windows DLL import libraries and static libs.
    if (WIN32 OR NOT BUILD_SHARED_LIBS)
        install(TARGETS QMdmmCore6
            EXPORT QMdmm6Targets
            COMPONENT "dev6"
            ARCHIVE
        )
    endif()

    if (QMDMM_EXPORT_PRIVATE)
        add_library(QMdmmCorePrivate6 INTERFACE)
        add_library(QMdmm6::CorePrivate ALIAS QMdmmCorePrivate6)
        target_link_libraries(QMdmmCorePrivate6
            INTERFACE QMdmm6::Core
                      QMdmmCorePrivate
        )
    endif()
endif()

add_subdirectory(test)
