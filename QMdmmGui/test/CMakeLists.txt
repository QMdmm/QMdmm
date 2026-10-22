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

# A stand-in for a bot: a program that leaves with code 0 at once, which is what a bot's own
# exit looks like once the match it played is over (see botstandin.cpp). The seats a local game
# fills are watched by the bridge, and the half worth a case of its own -- a seat that went home
# rather than one that failed -- is reached by handing the bridge this program instead of a bot.
add_executable(botstandin6 EXCLUDE_FROM_ALL botstandin.cpp)
add_dependencies(QMdmmGui_Test botstandin6)
add_dependencies(tst_qmdmmgui6 botstandin6)

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
    # Resolved by the build system, so the case that hands the bridge a stand-in bot does not
    # have to guess the output directory, the version suffix or the platform's own file name.
    "QMDMM_STANDIN_BOT=\"$<TARGET_FILE:botstandin6>\""
)

# What a failing case leaves behind has to be a file. The test's own output does not reach the
# CI log on every runner -- the Windows probe writes it nowhere -- while a junit report does,
# and the report carries the case name and quotes its message. This is the same report the
# shared test main writes for the other suites (see QMdmmCore/test/test.cpp). The Qt major
# version comes from Qt6_VERSION_MAJOR, since this test has no shared main to take it from. The
# second -o keeps the plain reading on stdout as well: naming any -o suppresses it, and the
# runners whose logs do carry the plain one should keep it.
add_test(NAME "QMdmmGui - gui smoke - Qt6" COMMAND tst_qmdmmgui6 "-o" "-,txt" "-o" "tst_QMdmmGuiSmoke${Qt6_VERSION_MAJOR}.xml,junitxml")

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

# The same reading arrangement as the smoke test above.
add_test(NAME "QMdmmGui - application - Qt6" COMMAND tst_qmdmmguiapp6 "-o" "-,txt" "-o" "tst_QMdmmGuiApp${Qt6_VERSION_MAJOR}.xml,junitxml")

# See the note in QMdmmCore/test/CMakeLists.txt for how the bound is chosen. The smoke test
# drives whole games and reads up to 56.78 s, so six times that rounds up to 360 s; the
# application case only reaches main() and reads up to 0.40 s, which takes the floor.
set_tests_properties("QMdmmGui - gui smoke - Qt6" PROPERTIES TIMEOUT 360)
set_tests_properties("QMdmmGui - application - Qt6" PROPERTIES TIMEOUT 60)

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
