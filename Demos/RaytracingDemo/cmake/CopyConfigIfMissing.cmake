if(NOT DEFINED SOURCE OR NOT DEFINED DESTINATION)
    message(FATAL_ERROR "CopyConfigIfMissing.cmake requires SOURCE and DESTINATION")
endif()

if(NOT EXISTS "${DESTINATION}")
    file(COPY_FILE "${SOURCE}" "${DESTINATION}")
endif()
