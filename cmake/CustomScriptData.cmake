set(custom_entry_path "${PROJECT_SOURCE_DIR}/Extension/Scripting/custom_entry.lua")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${custom_entry_path}")
file(READ "${custom_entry_path}" custom_entry_source)
configure_file(cmake/templates/custom_entry.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_custom_entry.h" @ONLY)
