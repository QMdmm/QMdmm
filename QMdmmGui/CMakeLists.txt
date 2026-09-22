# SPDX-License-Identifier: AGPL-3.0-or-later

# This GUI will be Qt 6 only
# Qt 5 and Qt 6 QML CMake API is incompatible (qt6_add_qml_module) so ...

set(QMDMMGUI_SOURCES
    src/main.cpp
    src/gameclient.h
    src/gameclient.cpp
    src/mainwindow.h
    src/mainwindow.cpp
)

set(QMDMMGUI_QML_FILES
    qml/main.qml
    qml/Button.qml
    qml/StartScene.qml
    qml/Window.qml
    qml/RootItem.qml
    qml/ConnectScene.qml
    qml/GameScene.qml
    qml/PlayerCard.qml
    qml/StatusBar.qml
)
set(QMDMMGUI_RESOURCES
    assets/1.jpg
    assets/btn.png
    assets/horse.png
    assets/knife.png
)

if (Qt6_FOUND)
    find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

    qt6_add_executable(QMdmm6 WIN32 MACOSX_BUNDLE ${QMDMMGUI_SOURCES})

    set(QMDMMGUI_TARGET_PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )
    if (APPLE)
        # The bundle is the artefact on macOS and it carries its version in
        # Info.plist.  Versioning the executable inside it as well (QMdmm6-0.0.1,
        # with QMdmm6 a symlink to it) would make Qt's deploy commands look for a
        # bundle named after that executable rather than after the bundle, and
        # CMake's own install-time rpath fixup is skipped for a bundle whose
        # executable is reached through a symlink.
        list(APPEND QMDMMGUI_TARGET_PROPERTIES
            MACOSX_BUNDLE_BUNDLE_VERSION "${QMdmm_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${QMdmm_VERSION}"
        )
        # Where the bundle looks for the Qt frameworks and for this project's
        # own libraries.  The deploy step moves both into the bundle, and it
        # needs the installed libdir while it does so.  Set on its own because
        # the value contains the list separator.
        set_target_properties(QMdmm6 PROPERTIES
            INSTALL_RPATH "@executable_path/../Frameworks;${CMAKE_INSTALL_FULL_LIBDIR}"
        )
    else()
        list(APPEND QMDMMGUI_TARGET_PROPERTIES
            VERSION "${QMdmm_VERSION}"
        )
    endif()
    set_target_properties(QMdmm6 PROPERTIES ${QMDMMGUI_TARGET_PROPERTIES})

    # QMdmm6::* rather than the bare target names: these are the names an
    # installed dev package provides, so the GUI builds unchanged both inside
    # this tree and from a consumer project that only did find_package(QMdmm6).
    target_link_libraries(QMdmm6
        PUBLIC QMdmm6::Networking
               QMdmm6::Core
               Qt6::QuickWidgets
               Qt6::Quick
               Qt6::Qml
               Qt6::Widgets
               Qt6::Gui
               Qt6::Core
    )
    target_link_libraries(QMdmm6 PRIVATE Qt6::Widgets)
    qt6_add_qml_module(QMdmm6
        URI QMdmm.Gui
        VERSION ${QMdmm_VERSION}
        QML_FILES ${QMDMMGUI_QML_FILES}
        RESOURCES ${QMDMMGUI_RESOURCES}
    )

    qt6_add_translations(QMdmm6
        TS_FILES translations/qmdmm_zh_CN.ts
    )

    install(TARGETS QMdmm6
        COMPONENT "6"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        BUNDLE DESTINATION "."
    )

    if (APPLE)
        # An installed bundle has to carry its own Qt: nothing on the target
        # machine provides the frameworks, the platform plugins or the QML
        # modules the GUI imports.  Qt's deploy commands address the bundle as
        # <name>.app relative to the install prefix, which is why the bundle is
        # installed at the top of the prefix, and they run after install(TARGETS)
        # because they patch the copy that is already in place.
        qt6_generate_deploy_qml_app_script(
            TARGET QMdmm6
            OUTPUT_SCRIPT QMdmm6_deploy_script
        )
        install(SCRIPT "${QMdmm6_deploy_script}" COMPONENT "6")
    endif()

    if (BUILD_TESTING)
        add_subdirectory(test)
    endif()
endif()
