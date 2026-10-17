# SPDX-License-Identifier: AGPL-3.0-or-later

add_custom_target(QMdmmCore_Test)
if (BUILD_TESTING)
    set_target_properties(QMdmmCore_Test PROPERTIES
        EXCLUDE_FROM_ALL false
    )
endif()

function(add_qmdmmcore_test TEST_SOURCE)
    string(MAKE_C_IDENTIFIER "${TEST_SOURCE}" TEST_TARGET_EXECUTABLE)

    if (Qt6_FOUND)
        add_executable("tst_qmdmmcore_${TEST_TARGET_EXECUTABLE}6" EXCLUDE_FROM_ALL ${TEST_SOURCE} test.cpp test.h)
        add_dependencies(QMdmmCore_Test "tst_qmdmmcore_${TEST_TARGET_EXECUTABLE}6")
        target_link_libraries("tst_qmdmmcore_${TEST_TARGET_EXECUTABLE}6" PUBLIC QMdmmCore6)
        if (BUILD_TESTING)
            target_link_libraries("tst_qmdmmcore_${TEST_TARGET_EXECUTABLE}6" PUBLIC Qt6::Test)
        endif()
        set_target_properties("tst_qmdmmcore_${TEST_TARGET_EXECUTABLE}6" PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

        # The protocol fixtures read the header that declares the ids to compare them
        # against, so its path is named here rather than looked up from the test
        # binary, which sits at a different depth in a build tree and in an install.
        # The contract page beside it is read the same way: both are carriers of one
        # list, and the case that reads them is what keeps them in step.
        target_compile_definitions("tst_qmdmmcore_${TEST_TARGET_EXECUTABLE}6" PRIVATE
            QMDMM_PROTOCOL_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/../src/qmdmmprotocol.h"
            QMDMM_PROTOCOL_DOC="${CMAKE_CURRENT_SOURCE_DIR}/../../doc/protocol.md"
        )

        add_test(NAME "QMdmmCore - ${TEST_SOURCE} - Qt6" COMMAND "tst_qmdmmcore_${TEST_TARGET_EXECUTABLE}6")

        # Every case is bounded, so a case that hangs is killed instead of eating the job's
        # budget. The bound is a hang detector, not a performance gate: six times the slowest
        # reading the case has across the CI cells, rounded up to a whole minute, with a
        # one-minute floor. The slowest reading any case in this module has is 0.27 s, so they
        # all take the floor -- and because the bound is set here rather than per case, a case
        # added to the list below cannot quietly go without one.
        set_tests_properties("QMdmmCore - ${TEST_SOURCE} - Qt6" PROPERTIES TIMEOUT 60)
    endif()
endfunction()

add_qmdmmcore_test(tst_qmdmmcore.cpp)
add_qmdmmcore_test(tst_qmdmmlogic.cpp)
# The last case of this suite runs the binary twice more, so the whole reading is three suite
# runs instead of one (about 8 s here against the 2.6 s a single run takes). The bound below
# is provisional until a CI cell reports its own reading, at which point the rule above --
# six times the slowest reading, rounded up to a whole minute -- applies to that number.
set_tests_properties("QMdmmCore - tst_qmdmmlogic.cpp - Qt6" PROPERTIES TIMEOUT 300)
add_qmdmmcore_test(tst_qmdmmplayer.cpp)
add_qmdmmcore_test(tst_qmdmmroom.cpp)
add_qmdmmcore_test(tst_qmdmmlogicconfiguration.cpp)
add_qmdmmcore_test(tst_qmdmmprotocol.cpp)
add_qmdmmcore_test(tst_qmdmmdebug.cpp)
add_qmdmmcore_test(tst_qmdmmsettings.cpp)
