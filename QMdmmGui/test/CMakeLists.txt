# SPDX-License-Identifier: AGPL-3.0-or-later
# GUI smoke test (Qt Quick Test). Exercises the QMdmmGameClient bridge (the
# C++/QML glue used by the "local game" mode) and the QML scenes' interaction
# wiring at runtime, where the GUI currently has only static verification.
#
# QMdmmGameClient lives in the QMdmmGui *executable*, not a linkable library, so
# its source is compiled straight into this test target.

add_custom_target(QMdmmGui_Test)
if (BUILD_TESTING)
    set_target_properties(QMdmmGui_Test PROPERTIES EXCLUDE_FROM_ALL false)
endif()

add_executable(tst_qmdmmgui6 EXCLUDE_FROM_ALL
    tst_qmdmmgui.cpp
    ../src/gameclient.h
    ../src/gameclient.cpp
)
add_dependencies(QMdmmGui_Test tst_qmdmmgui6)

# The paths the bridge resolves for the local game's server and bot (see
# QMdmmGameClient::setProgramPaths) are read back by the test cases, so both programs have to
# be there when the tests run -- which is next to the test, as the build lays them out.
add_dependencies(tst_qmdmmgui6 QMdmmServer6 QMdmmBot6)

target_link_libraries(tst_qmdmmgui6
    PUBLIC QMdmm6::Core
           QMdmm6::Networking
           Qt6::Qml
           Qt6::Quick
)
if (BUILD_TESTING)
    target_link_libraries(tst_qmdmmgui6 PUBLIC Qt6::QuickTest)
endif()

set_target_properties(tst_qmdmmgui6 PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# QMdmmGameClient is declared in src/, next to the target's own test entry point.
target_include_directories(tst_qmdmmgui6 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Qt Quick Test discovers the tst_*.qml cases in this directory.
target_compile_definitions(tst_qmdmmgui6 PRIVATE
    "QUICK_TEST_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
)

add_test(NAME "QMdmmGui - gui smoke - Qt6" COMMAND tst_qmdmmgui6)

# The application itself, which the smoke test above cannot reach: it drives the QML
# scenes, while main() and MainWindow's constructor only run in the program. MainWindow
# is compiled in here the way QMdmmGameClient is -- both live in the executable rather
# than in a library -- and main() is reached by running the built executable, the way
# the command-line cases of QMdmmBot and QMdmmServer reach theirs.
add_executable(tst_qmdmmguiapp6 EXCLUDE_FROM_ALL
    tst_qmdmmguiapp.cpp
    ../src/mainwindow.h
    ../src/mainwindow.cpp
    ../src/gameclient.h
    ../src/gameclient.cpp
)
add_dependencies(QMdmmGui_Test tst_qmdmmguiapp6)

# The executable under test is the subject of the command-line case, so it has to be
# built before this target is run.
add_dependencies(tst_qmdmmguiapp6 QMdmm6)

# This one builds a widget, so it needs an application object rather than the
# QGuiApplication the smoke test runs under.
target_link_libraries(tst_qmdmmguiapp6
    PUBLIC QMdmm6::Core
           QMdmm6::Networking
           Qt6::Qml
           Qt6::Quick
           Qt6::Widgets
           Qt6::QuickWidgets
)
if (BUILD_TESTING)
    target_link_libraries(tst_qmdmmguiapp6 PUBLIC Qt6::Test)
endif()

set_target_properties(tst_qmdmmguiapp6 PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# mainwindow.h and gameclient.h are declared in src/, next to their sources.
target_include_directories(tst_qmdmmguiapp6 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Resolved by the build system, so the case does not have to guess the output directory,
# the version suffix or the bundle this program is laid out as.
target_compile_definitions(tst_qmdmmguiapp6 PRIVATE
    "QMDMMGUI_EXECUTABLE=\"$<TARGET_FILE:QMdmm6>\""
)

add_test(NAME "QMdmmGui - application - Qt6" COMMAND tst_qmdmmguiapp6)

# To prevent initializing a real display in headless CI run, recognize the CI
# environment variable.
# As far as I know both GitHub Actions and Jenkins set this environment variable.
if (( DEFINED ENV{CI} ) AND "$ENV{CI}")
    set_tests_properties(
        "QMdmmGui - gui smoke - Qt6"
        "QMdmmGui - application - Qt6"
        PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    )
endif()
