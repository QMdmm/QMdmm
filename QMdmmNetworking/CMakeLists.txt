# SPDX-License-Identifier: AGPL-3.0-or-later

set(QMDMMNETWORKING_PCH
    src/qmdmmnetworkingglobal.h
)

set(QMDMMNETWORKING_HEADERS
    ${QMDMMNETWORKING_PCH}
    src/qmdmmserver.h
    src/qmdmmagent.h
    src/qmdmmclient.h
    src/qmdmmlogicrunner.h
    src/qmdmmsocket.h
)

set(QMDMMNETWORKING_PRIVATE_HEADERS
    src/qmdmmserver_p.h
    src/qmdmmagent_p.h
    src/qmdmmclient_p.h
    src/qmdmmlogicrunner_p.h
    src/qmdmmsocket_p.h
    src/qmdmmserverconnection_p.h
)

set(QMDMMNETWORKING_SOURCES
    src/qmdmmnetworking.cpp
    src/qmdmmserver.cpp
    src/qmdmmagent.cpp
    src/qmdmmclient.cpp
    src/qmdmmlogicrunner.cpp
    src/qmdmmsocket.cpp
)

set(QMDMMNETWORKING_PRIVATE_SOURCES
    src/qmdmmclient_p.cpp
    src/qmdmmlogicrunner_p.cpp
    src/qmdmmserverconnection_p.cpp
    src/qmdmmserver_p.cpp
    src/qmdmmsocket_p.cpp
)

set(QMDMMNETWORKING_DOC_FILES ${QMDMMNETWORKING_HEADERS} ${QMDMMNETWORKING_SOURCES} PARENT_SCOPE)

add_library(QMdmmNetworking INTERFACE)
auto_generate_header_file_for_target(QMdmmNetworking "${QMDMMNETWORKING_HEADERS}")
install(FILES ${QMDMMNETWORKING_HEADERS}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/QMdmmNetworking"
    COMPONENT "dev-common"
)
# See QMdmmCore: the INTERFACE library travels with the package because
# QMdmmNetworking6 consumes it PUBLIC-ly.
install(TARGETS QMdmmNetworking
    EXPORT QMdmm6Targets
    COMPONENT "dev-common"
)
# No INSTALL_INTERFACE entry here: the installed bare-name include directory is
# published by QMdmmNetworking6 below, exactly as in the build tree.
target_include_directories(QMdmmNetworking
    INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
if (QMDMM_EXPORT_PRIVATE)
    add_library(QMdmmNetworkingPrivate INTERFACE)
    target_sources(QMdmmNetworkingPrivate PRIVATE ${QMDMMNETWORKING_PRIVATE_HEADERS})
    install(FILES ${QMDMMNETWORKING_PRIVATE_HEADERS}
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/QMdmmNetworking/private/${PROJECT_VERSION}"
        COMPONENT "private-dev-common"
    )
    target_link_libraries(QMdmmNetworkingPrivate
        INTERFACE QMdmmNetworking
    )
endif()

if (Qt6_FOUND)
    qt6_add_library(QMdmmNetworking6 ${QMDMMNETWORKING_HEADERS} ${QMDMMNETWORKING_SOURCES} ${QMDMMNETWORKING_PRIVATE_HEADERS} ${QMDMMNETWORKING_PRIVATE_SOURCES})
    add_library(QMdmm6::Networking ALIAS QMdmmNetworking6)

    set_target_properties(QMdmmNetworking6 PROPERTIES
        # Exported as QMdmm6::Networking.
        EXPORT_NAME "Networking"
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
        VERSION "${QMdmm_VERSION}"
    )

    target_precompile_headers(QMdmmNetworking6
        PRIVATE ${QMDMMNETWORKING_PCH}
    )

    # See QMdmmCore for why the export macro is generated, why the header lands in the
    # forwarding headers' directory, and why STATIC_DEFINE is pinned to QMDMM_STATIC.
    generate_export_header(QMdmmNetworking6
        BASE_NAME "QMdmmNetworking"
        EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/build/include/QMdmmNetworking/qmdmmnetworking_export.h"
        STATIC_DEFINE "QMDMM_STATIC"
    )
    install(FILES "${CMAKE_BINARY_DIR}/build/include/QMdmmNetworking/qmdmmnetworking_export.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/QMdmmNetworking"
        COMPONENT "dev-common"
    )

    target_include_directories(QMdmmNetworking6
        INTERFACE "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/build/include/QMdmmNetworking>"
                  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/QMdmmNetworking>"
    )

    # QMdmmCore6 by target name rather than the QMdmm6::Core alias: install(EXPORT)
    # resolves what it is given, and naming the concrete target keeps the exported
    # INTERFACE_LINK_LIBRARIES unambiguous.
    target_link_libraries(QMdmmNetworking6
        PUBLIC QMdmmCore6
               QMdmmNetworking
               Qt6::Network
               Qt6::WebSockets
    )

    # ARCHIVE takes a component of its own instead of the "6" above: it is the
    # linker-facing artifact -- the static library, or the import library of a
    # Windows DLL -- and belongs beside the namelink in dev6. It is named in
    # this call rather than in a second one because install(EXPORT) refuses a
    # target that appears in the same export set twice; a shared library on
    # Linux/macOS has no archive at all, so the kind adds nothing there.
    install(TARGETS QMdmmNetworking6
        EXPORT QMdmm6Targets
        COMPONENT "6"
        RUNTIME
        LIBRARY
        NAMELINK_COMPONENT "dev6"
        ARCHIVE COMPONENT "dev6"
    )

    if (QMDMM_EXPORT_PRIVATE)
        add_library(QMdmmNetworkingPrivate6 INTERFACE)
        add_library(QMdmm6::NetworkingPrivate ALIAS QMdmmNetworkingPrivate6)
        target_link_libraries(QMdmmNetworkingPrivate6
            INTERFACE QMdmm6::Networking
                      QMdmmNetworkingPrivate
        )
    endif()
endif()

add_subdirectory(test)
