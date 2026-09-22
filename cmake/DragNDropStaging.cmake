# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Run by cpack from CPACK_PRE_BUILD_SCRIPTS: after the runtime component has
# been installed into the staging tree and before the disk image is produced
# from that tree.

# The staging root is the directory holding the staged bundle; it is found
# rather than assumed, because the name of the directory below
# CPACK_TEMPORARY_DIRECTORY is cpack's to choose.
file(GLOB QMDMM_STAGED_BUNDLES "${CPACK_TEMPORARY_DIRECTORY}/*/QMdmm6.app")
foreach (QMDMM_STAGED_BUNDLE IN LISTS QMDMM_STAGED_BUNDLES)
    get_filename_component(QMDMM_STAGING_ROOT "${QMDMM_STAGED_BUNDLE}" DIRECTORY)

    # The disk image is the app and the note beside it.  These are the
    # directories a prefix install produces beside the bundle, and what they
    # hold is for make install and for the development packages: the libraries
    # are already inside the bundle (the deploy step copied them into
    # Contents/Frameworks and pointed the load commands at that copy), and the
    # headers, the CMake package files and the documentation are not part of
    # what a user drags to Applications.
    foreach (QMDMM_STAGED_FLAT_DIR IN ITEMS bin lib include share)
        if (IS_DIRECTORY "${QMDMM_STAGING_ROOT}/${QMDMM_STAGED_FLAT_DIR}")
            file(REMOVE_RECURSE "${QMDMM_STAGING_ROOT}/${QMDMM_STAGED_FLAT_DIR}")
        endif()
    endforeach()
endforeach()
