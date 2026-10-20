# SPDX-License-Identifier: AGPL-3.0-or-later
# QMdmmCtl tests (QtTest).
#
# The program under test is an executable, and what it does is ask a second executable a question
# over a socket -- so every case drives both as child processes and reads what a caller outside them
# can see: the exit status and the two streams. Nothing of either program is linked in here.
#
# The server is the far end of the question. It is configured Networking-then-Server-then-Bot-then-
# Ctl, so by the time this target exists, the server's own executable does too.

add_custom_target(QMdmmCtl_Test)
if (BUILD_TESTING)
    set_target_properties(QMdmmCtl_Test PROPERTIES EXCLUDE_FROM_ALL false)
endif()

add_executable(tst_qmdmmctl6 EXCLUDE_FROM_ALL
    tst_qmdmmctl.cpp
)
add_dependencies(QMdmmCtl_Test tst_qmdmmctl6)

# Both programs are the subject of the cases: the one that is run and the one it asks.
add_dependencies(tst_qmdmmctl6 QMdmmCtl6 QMdmmServer6)

target_link_libraries(tst_qmdmmctl6
    PUBLIC Qt6::Core
           Qt6::Network
)
if (BUILD_TESTING)
    target_link_libraries(tst_qmdmmctl6 PUBLIC Qt6::Test)
endif()

set_target_properties(tst_qmdmmctl6 PROPERTIES
    AUTOMOC ON
)

# Resolved by the build system, so the cases do not have to guess the output directory or the
# version suffix either executable carries.
target_compile_definitions(tst_qmdmmctl6 PRIVATE
    "QMDMMCTL_EXECUTABLE=\"$<TARGET_FILE:QMdmmCtl6>\""
    "QMDMMSERVER_EXECUTABLE=\"$<TARGET_FILE:QMdmmServer6>\""
)

# The junit report is what a failing case leaves behind when the plain output does not reach the CI
# log; the second -o keeps the plain reading on stdout as well. Same shape as the server's suite.
add_test(NAME "QMdmmCtl - ctl cli - Qt6" COMMAND tst_qmdmmctl6 "-o" "-,txt" "-o" "tst_QMdmmCtlCli${Qt6_VERSION_MAJOR}.xml,junitxml")

# Every case here is two short-lived processes and one socket exchange; the wait for the server to
# reach its listener is the longest of them at 15 s. Sixty seconds is that bound with room for a
# loaded runner; it is re-read from CI once this leg has run there.
set_tests_properties("QMdmmCtl - ctl cli - Qt6" PROPERTIES TIMEOUT 60)
