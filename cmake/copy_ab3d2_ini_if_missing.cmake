# POST_BUILD: seed a user-editable ab3d2.ini only when it does not exist.
if(NOT DEST_DIR OR NOT SRC_FILE)
  message(FATAL_ERROR "copy_ab3d2_ini_if_missing: DEST_DIR and SRC_FILE required")
endif()
set(OUT_INI "${DEST_DIR}/ab3d2.ini")
if(EXISTS "${OUT_INI}")
  return()
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy "${SRC_FILE}" "${OUT_INI}"
  RESULT_VARIABLE _copy_result
)
if(NOT _copy_result EQUAL 0)
  message(WARNING "copy_ab3d2_ini_if_missing: failed to create ${OUT_INI}")
endif()
