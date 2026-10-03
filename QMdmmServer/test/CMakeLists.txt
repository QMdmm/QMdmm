# SPDX-License-Identifier: AGPL-3.0-or-later
# QMdmmServer command-line tests (QtTest).
#
# The configuration code lives in the QMdmmServer *executable* and works off the
# process arguments: Config::Config() parses qApp->arguments(), and --help,
# --show-current-configuration, --save-configuration as well as every rejected
# value end in std::exit(). Linking its sources into a test target (the way
# QMdmmBot's tests do) would therefore only ever see the test binary's own
# arguments. This test instead drives the built executable as a child process and
# asserts on what a user can observe from the outside: the exit code and the two
# streams.

add_custom_target(QMdmmServer_Test)
if (BUILD_TESTING)
    set_target_properties(QMdmmServer_Test PROPERTIES EXCLUDE_FROM_ALL false)
endif()

add_executable(tst_qmdmmserver6 EXCLUDE_FROM_ALL
    tst_qmdmmserver.cpp
)
add_dependencies(QMdmmServer_Test tst_qmdmmserver6)

# The executable under test is the subject of every case here, so it has to be
# built before this target is run -- and its sources are not linked into it.
add_dependencies(tst_qmdmmserver6 QMdmmServer6)

# Qt6::Network is here for the case that has to hand the run a port it cannot
# bind: it holds one of its own and passes that port on the command line.
target_link_libraries(tst_qmdmmserver6
    PUBLIC Qt6::Core
           Qt6::Network
)
if (BUILD_TESTING)
    target_link_libraries(tst_qmdmmserver6 PUBLIC Qt6::Test)
endif()

set_target_properties(tst_qmdmmserver6 PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# Resolved by the build system, so the cases do not have to guess the output
# directory or the version suffix the executable carries.
target_compile_definitions(tst_qmdmmserver6 PRIVATE
    "QMDMMSERVER_EXECUTABLE=\"$<TARGET_FILE:QMdmmServer6>\""
)

add_test(NAME "QMdmmServer - server cli - Qt6" COMMAND tst_qmdmmserver6)
