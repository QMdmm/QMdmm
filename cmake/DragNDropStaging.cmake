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

    # lib/ holds this project's two shared libraries.  The deploy step has
    # already copied both of them into Contents/Frameworks and pointed the
    # bundle's load commands at that copy; what is staged here is the flat
    # copy a prefix install keeps for the development packages, and the image
    # should not carry the same libraries twice.
    file(REMOVE_RECURSE "${QMDMM_STAGING_ROOT}/lib")
endforeach()
