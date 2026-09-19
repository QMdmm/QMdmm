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

    set_target_properties(QMdmm6 PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
        VERSION "${QMdmm_VERSION}"
    )

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
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RUNTIME
        BUNDLE
    )

    if (BUILD_TESTING)
        add_subdirectory(test)
    endif()
endif()
