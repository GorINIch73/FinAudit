if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

string(TIMESTAMP CURRENT_BUILD_DATE "%Y-%m-%d %H:%M:%S")
get_filename_component(OUTPUT_DIRECTORY "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT_FILE}"
    "#pragma once\n#define FINAUDIT_BUILD_DATE \"${CURRENT_BUILD_DATE}\"\n")
