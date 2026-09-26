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
        add_test(NAME "QMdmmCore - ${TEST_SOURCE} - Qt6" COMMAND "tst_qmdmmcore_${TEST_TARGET_EXECUTABLE}6")
    endif()
endfunction()

add_qmdmmcore_test(tst_qmdmmcore.cpp)
add_qmdmmcore_test(tst_qmdmmlogic.cpp)
add_qmdmmcore_test(tst_qmdmmplayer.cpp)
add_qmdmmcore_test(tst_qmdmmroom.cpp)
add_qmdmmcore_test(tst_qmdmmlogicconfiguration.cpp)
add_qmdmmcore_test(tst_qmdmmprotocol.cpp)
add_qmdmmcore_test(tst_qmdmmdebug.cpp)
add_qmdmmcore_test(tst_qmdmmsettings.cpp)
