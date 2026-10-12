# SPDX-License-Identifier: AGPL-3.0-or-later

add_custom_target(QMdmmNetworking_Test)
if (BUILD_TESTING)
    set_target_properties(QMdmmNetworking_Test PROPERTIES
        EXCLUDE_FROM_ALL false
    )
endif()

function(add_qmdmmnetworking_test TEST_SOURCE TEST_TIMEOUT)
    string(MAKE_C_IDENTIFIER "${TEST_SOURCE}" TEST_TARGET_EXECUTABLE)

    if (Qt6_FOUND)
        add_executable("tst_qmdmmnetworking_${TEST_TARGET_EXECUTABLE}6" EXCLUDE_FROM_ALL ${TEST_SOURCE} test.cpp test.h)
        add_dependencies(QMdmmNetworking_Test "tst_qmdmmnetworking_${TEST_TARGET_EXECUTABLE}6")
        target_link_libraries("tst_qmdmmnetworking_${TEST_TARGET_EXECUTABLE}6" PUBLIC QMdmm6::Networking)
        if (BUILD_TESTING)
            target_link_libraries("tst_qmdmmnetworking_${TEST_TARGET_EXECUTABLE}6" PUBLIC Qt6::Test)
        endif()
        set_target_properties("tst_qmdmmnetworking_${TEST_TARGET_EXECUTABLE}6" PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
        add_test(NAME "QMdmmNetworking - ${TEST_SOURCE} - Qt6" COMMAND "tst_qmdmmnetworking_${TEST_TARGET_EXECUTABLE}6")

        # See the note in QMdmmCore/test/CMakeLists.txt for how the bound is chosen: six times
        # the slowest reading the case has across the CI cells, rounded up to a whole minute,
        # with a one-minute floor. Here the value is an argument rather than a constant, so a
        # case added below has to declare its own.
        set_tests_properties("QMdmmNetworking - ${TEST_SOURCE} - Qt6" PROPERTIES TIMEOUT ${TEST_TIMEOUT})
    endif()
endfunction()

# The networked-gameplay suite reads up to 14.22 s.
add_qmdmmnetworking_test(tst_qmdmmnetworking.cpp 120)
# The configuration cases read up to 0.16 s and take the floor.
add_qmdmmnetworking_test(tst_qmdmmserverconfiguration.cpp 60)

if (Qt6_FOUND)
    add_executable(tst_qmdmm_smoke6 EXCLUDE_FROM_ALL tst_smoke_main.cpp)
    add_dependencies(QMdmmNetworking_Test tst_qmdmm_smoke6)
    target_link_libraries(tst_qmdmm_smoke6 PRIVATE QMdmm6::Core QMdmm6::Networking)

    set_target_properties(tst_qmdmm_smoke6 PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

    # Two cases cover the playerNumPerRoom boundary: the default (2, the minimum
    # valid room) and the high end (8), so a room-size regression at either end
    # of the supported 2..9 range is caught.
    add_test(NAME "QMdmmNetworking - Smoke - Qt6" COMMAND tst_qmdmm_smoke6)
    add_test(NAME "QMdmmNetworking - Smoke <8 players> - Qt6" COMMAND tst_qmdmm_smoke6 8)

    # Both smoke cases play a whole game, which is what the 20 s reading is: six times that
    # rounds up to 180 s. See the note in QMdmmCore/test/CMakeLists.txt.
    set_tests_properties(
        "QMdmmNetworking - Smoke - Qt6"
        "QMdmmNetworking - Smoke <8 players> - Qt6"
        PROPERTIES TIMEOUT 180
    )
endif()
