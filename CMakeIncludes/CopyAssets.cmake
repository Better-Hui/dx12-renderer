# Modify Begin:2026-07-21 by BestHui
# Copy shared runtime assets as a build event on the real demo target.
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Assets/" "$<TARGET_FILE_DIR:${TARGET_NAME}>/Assets"
        VERBATIM
        )
# Modify End
