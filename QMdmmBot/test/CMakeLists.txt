# SPDX-License-Identifier: AGPL-3.0-or-later
# QMdmmBot unit tests (QtTest).
#
# Bot lives in the QMdmmBot *executable*, not in a linkable library, so its
# sources are compiled straight into this test target -- the same approach the
# QMdmmGui smoke test takes for QMdmmGameClient. The style subclasses come along
# because Bot::createBot() references them.
#
# The command line splits the difference. A run that is rejected, or that asks
# for the help text or the version, ends in std::exit() and prints to the two
# streams -- nothing below a process can observe that, so those cases drive the
# built executable as a child process and assert on the exit status and the two
# streams, the way the QMdmmServer command-line test does. What the
# configuration *resolved* is readable in this process, on the other hand:
# Config is handed the command line as an argument, so its source comes along
# and the cases give it one of their own. The runs that would resolve those
# values never leave the bot's own event loop, which is what puts that half out
# of reach of the outside.

add_custom_target(QMdmmBot_Test)
if (BUILD_TESTING)
    set_target_properties(QMdmmBot_Test PROPERTIES EXCLUDE_FROM_ALL false)
endif()

add_executable(tst_qmdmmbot6 EXCLUDE_FROM_ALL
    tst_qmdmmbot.cpp
    test.cpp
    test.h
    ../src/config.h
    ../src/config.cpp
    ../src/bot.h
    ../src/bot.cpp
    ../src/knifepreferredbot.cpp
    ../src/horsepreferredbot.cpp
    ../src/rlbot.cpp
)
add_dependencies(QMdmmBot_Test tst_qmdmmbot6)

# The executable under test is the subject of the command-line cases, so it has
# to be built before this target is run.
add_dependencies(tst_qmdmmbot6 QMdmmBot6)

target_link_libraries(tst_qmdmmbot6
    PUBLIC QMdmm6::Core
           QMdmm6::Networking
)
if (BUILD_TESTING)
    target_link_libraries(tst_qmdmmbot6 PUBLIC Qt6::Test)
endif()

# Resolved by the build system, so the cases do not have to guess the output
# directory or the version suffix the executable carries.
target_compile_definitions(tst_qmdmmbot6 PRIVATE
    "QMDMMBOT_EXECUTABLE=\"$<TARGET_FILE:QMdmmBot6>\""
)

set_target_properties(tst_qmdmmbot6 PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# bot.h is declared in src/, next to the sources it belongs to.
target_include_directories(tst_qmdmmbot6 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_test(NAME "QMdmmBot - bot - Qt6" COMMAND tst_qmdmmbot6)

# The headless leg: a real server and real bots, one room, played to game over. No case reads a
# module of this project -- what they drive are two built executables -- but the shared main is
# compiled along with them, and it includes the core module's global header, so Core comes along
# the way it does for the bot suite above.
#
# It is declared here rather than next to the server's own suite because of the order the modules
# are configured in: QMdmmServer comes before QMdmmBot, so a target declared there cannot depend on
# the bot executable, which does not exist yet. Here both do.
add_executable(tst_qmdmmheadlessgame6 EXCLUDE_FROM_ALL
    tst_qmdmmheadlessgame.cpp
    test.cpp
    test.h
)
add_dependencies(QMdmmBot_Test tst_qmdmmheadlessgame6)

# Both programs are the subject of every case, so both have to be built before this target is run.
add_dependencies(tst_qmdmmheadlessgame6 QMdmmServer6 QMdmmBot6)

target_link_libraries(tst_qmdmmheadlessgame6
    PUBLIC QMdmm6::Core
           Qt6::Network
)
if (BUILD_TESTING)
    target_link_libraries(tst_qmdmmheadlessgame6 PUBLIC Qt6::Test)
endif()

# Resolved by the build system, so the cases do not have to guess either output directory or the
# version suffix the executables carry.
target_compile_definitions(tst_qmdmmheadlessgame6 PRIVATE
    "QMDMMSERVER_EXECUTABLE=\"$<TARGET_FILE:QMdmmServer6>\""
    "QMDMMBOT_EXECUTABLE=\"$<TARGET_FILE:QMdmmBot6>\""
)

set_target_properties(tst_qmdmmheadlessgame6 PROPERTIES
    AUTOMOC ON
)

add_test(NAME "QMdmmBot - headless game - Qt6" COMMAND tst_qmdmmheadlessgame6)

# The cases wait on state rather than on a fixed pause, and every wait is bounded well inside the
# suite's own bound: 60 s for the server to answer, 60 s for a room to fill, 120 s for a match to
# play out, 20 s for the bots whose match was watched to end their runs, 140 s for the second set
# (its match is not watched, so its bound covers the match and the exit path), plus 15 s per
# process to stop, comes to 505 s.
#
# The match bound is the one that moved. It was 20 s for a set of bots' runs, taken from a local
# reading where a match is over in a fraction of a second; on ubuntu a match runs at a round or
# more per second, so every Linux cell of the run that carried it failed with the second room
# still playing. What a case may wait for is therefore set from the slowest reading CI has given,
# and the bound below from the sum, with room for a case that has to be killed rather than stuck.
set_tests_properties("QMdmmBot - headless game - Qt6" PROPERTIES TIMEOUT 600)

# See the note in QMdmmCore/test/CMakeLists.txt for how the bound is chosen. This case drives
# whole games through the three styles, which is what its 34.59 s reading is; six times that
# rounds up to 240 s.
set_tests_properties("QMdmmBot - bot - Qt6" PROPERTIES TIMEOUT 240)
