# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Included by cpack once per generator, after CPACK_GENERATOR has been set to
# the one being run, so that what follows applies to the disk image alone and
# the tar archives keep installing every component the way they did.

if (NOT CPACK_GENERATOR STREQUAL "DragNDrop")
    return()
endif()

# The image is the app and the note that says what to do with it.  The runtime
# component also installs the flat layout make install produces - the shared
# libraries beside the bundle - and that layout is what the development
# packages and a prefix install use, not what a user drags out of the image:
# the libraries are already inside the bundle by the time the image is built.
# DragNDropStaging.cmake drops them from the staged tree.
set(CPACK_COMPONENTS_ALL "6")
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/DragNDropStaging.cmake")
set(CPACK_DMG_VOLUME_NAME "QMdmm ${CPACK_PACKAGE_VERSION}")
