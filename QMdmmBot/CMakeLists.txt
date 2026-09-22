# SPDX-License-Identifier: AGPL-3.0-or-later

set(QMDMMBOT_SOURCES
    src/main.cpp
    src/config.cpp
    src/config.h
    src/bot.h
    src/bot.cpp
    src/knifepreferredbot.cpp
    src/horsepreferredbot.cpp
    src/rlbot.cpp
)

if (Qt6_FOUND)
    if (ANDROID)
        # TODO: investigate the *CORRECT* method not using qt6_add_executable in Android platform
        # This was meant to be built as an executable run by QProcess
        # instead of a MODULE loaded from the Qt for Android entry point
        # Also, due to qt6_add_executable creates a MODULE, all Qt executable run inside Termux breaks build
        add_executable(QMdmmBot6 ${QMDMMBOT_SOURCES})
    else()
        qt6_add_executable(QMdmmBot6 ${QMDMMBOT_SOURCES})
    endif()

    set_target_properties(QMdmmBot6 PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
        VERSION "${QMdmm_VERSION}"
    )

    target_link_libraries(QMdmmBot6
        PUBLIC QMdmm6::Core
               QMdmm6::Networking
    )

    if (APPLE)
        # On macOS this program is installed inside the GUI's bundle, as a
        # sibling of the GUI itself, and reaches this project's libraries one
        # level up, in Contents/Frameworks, where the GUI's deploy step puts
        # them.
        set_target_properties(QMdmmBot6 PROPERTIES
            INSTALL_RPATH "@executable_path/../Frameworks"
        )
    endif()

    install(TARGETS QMdmmBot6
        COMPONENT "6"
        DESTINATION "${QMDMM6_INSTALL_RUNTIME_DIR}"
        RUNTIME
        LIBRARY
    )

    if (ANDROID)
        # TODO: check if it is right
        qt6_finalize_target(QMdmmBot6)
    endif()

    if (BUILD_TESTING)
        add_subdirectory(test)
    endif()
endif()
