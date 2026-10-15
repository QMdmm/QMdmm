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

# The free-slot table the cases hold to the options lives in the executable's own source file,
# which is not linked into this target. It is read as text, the way QMdmmCore's protocol case
# reads its page and the header it checks against.
target_compile_definitions(tst_qmdmmserver6 PRIVATE
    "QMDMMSERVER_CONFIG_SOURCE=\"${CMAKE_CURRENT_SOURCE_DIR}/../src/config.cpp\""
)

# The networking suite drives this executable as well: its inbound-corpus case spawns the server and
# feeds it the same frames, which is how "the server survived the corpus" becomes a reading about a
# process rather than about a case's own verdict (see tst_qmdmmnetworking.cpp). The two modules are
# configured Networking-then-Server, so this dependency cannot be declared next to the case that
# needs it -- the target it depends on does not exist yet at that point -- and both lines live here
# instead, where the executable target does. The guard is for a configuration that leaves the
# networking test out.
if (TARGET tst_qmdmmnetworking_tst_qmdmmnetworking_cpp6)
    add_dependencies(tst_qmdmmnetworking_tst_qmdmmnetworking_cpp6 QMdmmServer6)

    target_compile_definitions(tst_qmdmmnetworking_tst_qmdmmnetworking_cpp6 PRIVATE
        "QMDMMSERVER_EXECUTABLE=\"$<TARGET_FILE:QMdmmServer6>\""
    )
endif()

# What a failing case leaves behind has to be a file. This binary's own output does not reach
# the CI log on every runner -- the Windows probe writes it nowhere -- while a junit report
# does. It is the same report the shared test main writes for the other QtTest suites (see
# QMdmmCore/test/test.cpp). The Qt major version comes from Qt6_VERSION_MAJOR, since this test
# has no shared main to take it from. The second -o keeps the plain reading on stdout as well:
# naming any -o suppresses it, and the runners whose logs do carry the plain one should keep it.
add_test(NAME "QMdmmServer - server cli - Qt6" COMMAND tst_qmdmmserver6 "-o" "-,txt" "-o" "tst_QMdmmServerCli${Qt6_VERSION_MAJOR}.xml,junitxml")

# See the note in QMdmmCore/test/CMakeLists.txt for how the bound is chosen. The slowest
# reading this case has across the CI cells is 5.43 s, and six times that is below the floor.
set_tests_properties("QMdmmServer - server cli - Qt6" PROPERTIES TIMEOUT 60)
