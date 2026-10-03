# SPDX-License-Identifier: AGPL-3.0-or-later
# QMdmmBot unit tests (QtTest).
#
# Bot lives in the QMdmmBot *executable*, not in a linkable library, so its
# sources are compiled straight into this test target -- the same approach the
# QMdmmGui smoke test takes for QMdmmGameClient. The style subclasses come along
# because Bot::createBot() references them.
#
# The command line is the exception. Config::Config() parses qApp->arguments(),
# so linking config.cpp in here would only ever see the test binary's own
# arguments; those cases drive the built executable as a child process instead
# and assert on the exit status and the two streams, the way the QMdmmServer
# command-line test does. Its sources are therefore not linked in, and the
# executable is a build dependency.

add_custom_target(QMdmmBot_Test)
if (BUILD_TESTING)
    set_target_properties(QMdmmBot_Test PROPERTIES EXCLUDE_FROM_ALL false)
endif()

add_executable(tst_qmdmmbot6 EXCLUDE_FROM_ALL
    tst_qmdmmbot.cpp
    test.cpp
    test.h
    ../src/bot.h
    ../src/bot.cpp
    ../src/knifepreferredbot.cpp
    ../src/horsepreferredbot.cpp
    ../src/rlbot.cpp
)
add_dependencies(QMdmmBot_Test tst_qmdmmbot6)

# The executable under test is the subject of the command-line cases, so it has
# to be built before this target is run -- and its sources are not linked into it.
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
