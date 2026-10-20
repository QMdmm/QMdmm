# SPDX-License-Identifier: AGPL-3.0-or-later

# The libFuzzer harness for the inbound packet decoder. It sits outside test/ on
# purpose: a fuzz run is a budget spent generating input rather than a case that
# passes or fails, so it has no add_test() entry, and it is built only when
# QMDMM_BUILD_FUZZ_TARGET asks for it (see the option in the top-level
# CMakeLists.txt). Keeping it out of test/ also keeps it out of the clang-tidy
# scan, whose unit count is the tree's own count of sources under (src|test) --
# a translation unit that only exists in one configure mode would make that
# count disagree with the scan it is compared against.

if (NOT Qt6_FOUND)
    message(FATAL_ERROR "QMDMM_BUILD_FUZZ_TARGET needs Qt: the decoder it hammers is QMdmmCore's.")
endif()

# Asking the toolchain rather than reading its name: -fsanitize=fuzzer is Clang's,
# but "Clang" is not the same answer everywhere -- Apple's clang carries the name
# and, as shipped with the current Xcode, no libFuzzer runtime at all, which would
# otherwise surface as a link error against a library the user cannot install.
# The probe below has no main() of its own on purpose: with the flag on, a link
# that succeeds is exactly "libFuzzer's driver is here".
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer,address")
check_cxx_source_compiles(
    "extern \"C\" int LLVMFuzzerTestOneInput(const unsigned char *, unsigned long) { return 0; }"
    QMDMM_HAVE_LIBFUZZER)
unset(CMAKE_REQUIRED_FLAGS)
if (NOT QMDMM_HAVE_LIBFUZZER)
    message(FATAL_ERROR
        "QMDMM_BUILD_FUZZ_TARGET needs a clang whose -fsanitize=fuzzer links; ${CMAKE_CXX_COMPILER} does not provide one.")
endif()

add_executable(QMdmm_fuzz_packet fuzz_packet.cpp)
target_link_libraries(QMdmm_fuzz_packet PRIVATE QMdmmCore6)
# The instrumentation itself comes from the option (see the top-level
# CMakeLists.txt): it is on every target in a fuzz build, because the decoder
# under test is library code and a harness instrumented on its own would feed
# libFuzzer nothing but its own two branches. What is added here is the other
# half -- the driver and the runtime, which only the executable wants.
target_link_options(QMdmm_fuzz_packet PRIVATE -fsanitize=fuzzer,address)
set_target_properties(QMdmm_fuzz_packet PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
