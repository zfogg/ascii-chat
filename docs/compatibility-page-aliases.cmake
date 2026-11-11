if(NOT DEFINED DOC_OUTPUT_DIR)
    message(FATAL_ERROR "DOC_OUTPUT_DIR is required")
endif()

file(GLOB TOPIC_MARKDOWN_PAGES
    "${DOC_OUTPUT_DIR}/md_docs_2topics_2*.html"
)

foreach(page IN LISTS TOPIC_MARKDOWN_PAGES)
    get_filename_component(filename "${page}" NAME)
    string(REGEX REPLACE "^md_docs_2topics_2" "md_docs_2" alias "${filename}")
    if(NOT alias STREQUAL filename)
        file(READ "${page}" html)
        # The compatibility filename is not part of Doxygen's navtree index.
        # Reuse the crypto group tree so this compatibility page retains the
        # full Topics/group sidebar and opens in the same context as the
        # migrated topic.
        string(REPLACE
            "initNavTree('${filename}','','')"
            "initNavTree('group__module__crypto.html','','')"
            html "${html}")
        file(WRITE "${DOC_OUTPUT_DIR}/${alias}" "${html}")
    endif()
endforeach()
