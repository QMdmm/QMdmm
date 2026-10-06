
find_package(Doxygen)

if (DOXYGEN_FOUND)
    set(QMDMM_DOC_SOURCE_FILES)

    foreach (i IN LISTS QMDMMCORE_DOC_FILES)
        list(APPEND QMDMM_DOC_SOURCE_FILES "../QMdmmCore/${i}")
    endforeach()

    foreach (i IN LISTS QMDMMNETWORKING_DOC_FILES)
        list(APPEND QMDMM_DOC_SOURCE_FILES "../QMdmmNetworking/${i}")
    endforeach()

    set(DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
    set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
    set(DOXYGEN_PROJECT_BRIEF "${PROJECT_DESCRIPTION}")
    set(DOXYGEN_CREATE_SUBDIRS YES)
    set(DOXYGEN_CASE_SENSE_NAMES YES)
    set(DOXYGEN_FULL_PATH_NAMES NO)
    set(DOXYGEN_SOURCE_BROWSER NO)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_GENERATE_RTF YES)
    set(DOXYGEN_GENERATE_MAN YES)
    set(DOXYGEN_PREDEFINED
        DOXYGEN
        Q_DOC
        Q_CLANG_DOC
        "_declspec(x)= "
        "__attribute__(x)= "
        "Q_ENUM(x)= "
        "Q_ENUM_NS(x)= "
        "Q_DISABLE_COPY(x)=x(const x&)=delete;x&operator=(const x&)=delete;"
        "Q_DISABLE_COPY_MOVE(x)=x(const x&)=delete;x(x&&)=delete;x&operator=(const x&)=delete;x&operator=(x&&)=delete;"
    )
    set(DOXYGEN_SORT_MEMBER_DOCS NO)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "../README.md")
    set(DOXYGEN_MACRO_EXPANSION YES)
    set(DOXYGEN_EXTRACT_PRIV_VIRTUAL YES)
    set(DOXYGEN_WARN_IF_UNDOC_ENUM_VAL YES)

    doxygen_add_docs(
        "${PROJECT_NAME}_docs"
        ${QMDMM_DOC_SOURCE_FILES}
        QMdmmCore/group.cpp
        QMdmmNetworking/group.cpp
        ../README.md
        architecture.md
        getting-started.md
        ALL
        USE_STAMP_FILE
    )

    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/html"
        "${CMAKE_CURRENT_BINARY_DIR}/rtf"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        COMPONENT "doc"
    )

    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/man"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/.."
        COMPONENT "doc"
    )
endif()
