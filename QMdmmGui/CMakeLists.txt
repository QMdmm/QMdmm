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

    # What makes CMake treat this program as a macOS application instead of as a
    # command.  The plain shape leaves it out, so that all three programs are
    # laid out the same way and the GUI finds the other two where it puts them.
    if (APPLE AND QMDMM_MACOS_APP_BUNDLE)
        set(QMDMMGUI_BUNDLE_KEYWORD "MACOSX_BUNDLE")
    else()
        set(QMDMMGUI_BUNDLE_KEYWORD "")
    endif()

    qt6_add_executable(QMdmm6 WIN32 ${QMDMMGUI_BUNDLE_KEYWORD} ${QMDMMGUI_SOURCES})

    set(QMDMMGUI_TARGET_PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )
    if (APPLE AND QMDMM_MACOS_APP_BUNDLE)
        # In this shape the bundle is the artefact and it carries the version in
        # Info.plist.  Versioning the executable inside it as well (QMdmm6-0.0.1,
        # with QMdmm6 a symlink to it) would make Qt's deploy commands look for a
        # bundle named after that executable rather than after the bundle, and
        # CMake's own install-time rpath fixup is skipped for a bundle whose
        # executable is reached through a symlink.
        list(APPEND QMDMMGUI_TARGET_PROPERTIES
            # Without this the bundle is identified as com.yourcompany.QMdmm6,
            # which is the placeholder CMake falls back to.  The 6 is the Qt
            # major version this branch builds against, as in the target name.
            MACOSX_BUNDLE_GUI_IDENTIFIER "me.fsu0413.QMdmm6"
            MACOSX_BUNDLE_BUNDLE_VERSION "${QMdmm_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${QMdmm_VERSION}"
        )
        # Where the bundle looks for the Qt frameworks and for this project's
        # own libraries: the frameworks directory inside the bundle, and the
        # libdir of the prefix the bundle is installed into.  That libdir is
        # named relative to the executable rather than absolutely, because the
        # deploy step also runs while the tree is staged for a disk image,
        # under a prefix that is not the configure-time one; an absolute path
        # baked in at configure time sends the deploy looking for the libraries
        # in the wrong prefix, and the bundle is then left without them.  Set
        # on its own because the value contains the list separator.
        set_target_properties(QMdmm6 PROPERTIES
            INSTALL_RPATH "@executable_path/../Frameworks;@executable_path/../../../${CMAKE_INSTALL_LIBDIR}"
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
        BUNDLE DESTINATION "${QMDMM6_INSTALL_BUNDLE_DESTINATION}"
    )

    if (APPLE AND QMDMM_MACOS_APP_BUNDLE)
        # An installed bundle has to carry its own Qt: nothing on the target
        # machine provides the frameworks, the platform plugins or the QML
        # modules the GUI imports.  Qt's deploy commands address the bundle as
        # <name>.app relative to the install prefix, which is why the bundle is
        # installed at the top of the prefix, and they run after install(TARGETS)
        # because they patch the copy that is already in place.

        # The deploy step picks its plugins from the Qt modules the application
        # resolves to, and drops the platform plugins from that set on purpose:
        # it hands the choice to the deploy tool, which copies the one the
        # application actually runs on - here, cocoa alone.  A bundle that only
        # starts under cocoa cannot be driven headless, so the whole set is
        # installed instead.  This has to precede the deploy script: that script
        # signs the bundle, and a file added afterwards leaves the signature
        # describing a bundle that no longer exists.  The deploy tool walks the
        # bundle as it finds it, so it rewrites these plugins' dependencies and
        # signs them along with the rest, and it leaves alone what it did not
        # put there.
        install(DIRECTORY "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms/"
            DESTINATION "${QMDMM6_INSTALL_BUNDLE_DIR}/Contents/PlugIns/platforms"
            COMPONENT "6"
        )

        qt6_generate_deploy_qml_app_script(
            TARGET QMdmm6
            OUTPUT_SCRIPT QMdmm6_deploy_script
        )
        install(SCRIPT "${QMdmm6_deploy_script}" COMPONENT "6")

        # The note that sits next to the app in the disk image and says what to
        # do with it.  Installed at the top of the prefix, which is where the
        # image takes its contents from.
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/macos/readme.md"
            DESTINATION "."
            COMPONENT "6"
        )
    endif()

    if (BUILD_TESTING)
        add_subdirectory(test)
    endif()
endif()
