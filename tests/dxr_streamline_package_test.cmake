foreach(required_argument IN ITEMS AB3D2_PACKAGE_DIR AB3D2_EXECUTABLE
                                   AB3D2_DUMPBIN)
  if(NOT DEFINED ${required_argument} OR "${${required_argument}}" STREQUAL "")
    message(FATAL_ERROR "${required_argument} was not supplied")
  endif()
endforeach()

set(staged_file_hashes
    "sl.interposer.dll|8C87C9499461DA561EDD529AA9BF7831D67D7B94EBB1C1A5ED54EF4934E1EA4C"
    "sl.common.dll|82924A8954DD671E09351C5DE0EB87AD0EB25B944CC9F9AB955CA1D9950DE15D"
    "sl.dlss_d.dll|026B9F4F9EA848F2F5AA0E2024FC6B1C885F57EEA5D41078124DBA14F873B86B"
    "nvngx_dlssd.dll|4BC7EA5FCB2F32CF86BC2CB072E8D2860914A0BE511CBDCB2FC206C1D9D83B80"
    "nvngx_dlss.license.txt|3027F23CA5A46DD9CB8183FBD522983A86F64D7DAAC5982912BF9F214671F294"
    "streamline-license.txt|7B6F23E7D6F3AD6292F9308D2B42CDC3D82AE4E9B2ABB55F230279D83BEDD43D"
    "streamline-3rd-party-licenses.md|CB251639994465F31D2178C16ACEDF1FF4F9CEF1C370A09782ED36D4C86C1DCA")

foreach(file_hash IN LISTS staged_file_hashes)
  string(REPLACE "|" ";" file_hash_parts "${file_hash}")
  list(GET file_hash_parts 0 filename)
  list(GET file_hash_parts 1 expected_hash)
  set(staged_path "${AB3D2_PACKAGE_DIR}/${filename}")
  if(NOT EXISTS "${staged_path}")
    message(FATAL_ERROR "Streamline package is missing ${filename}")
  endif()
  file(SHA256 "${staged_path}" actual_hash)
  string(TOUPPER "${actual_hash}" actual_hash)
  if(NOT actual_hash STREQUAL expected_hash)
    message(FATAL_ERROR
            "Streamline package SHA-256 mismatch for ${filename}")
  endif()
endforeach()

execute_process(
  COMMAND "${AB3D2_DUMPBIN}" /DEPENDENTS "${AB3D2_EXECUTABLE}"
  RESULT_VARIABLE dumpbin_result
  OUTPUT_VARIABLE dependencies
  ERROR_VARIABLE dumpbin_error)
if(NOT dumpbin_result EQUAL 0)
  message(FATAL_ERROR "dumpbin dependency audit failed: ${dumpbin_error}")
endif()
string(TOUPPER "${dependencies}" dependencies_upper)
foreach(forbidden_import IN ITEMS DXGI D3D12 D3D11 VULKAN-1)
  if(dependencies_upper MATCHES "${forbidden_import}\\.DLL")
    message(FATAL_ERROR
            "Streamline-enabled executable directly imports ${forbidden_import}.dll")
  endif()
endforeach()

execute_process(
  COMMAND "${AB3D2_DUMPBIN}" /IMPORTS:sl.interposer.dll
          "${AB3D2_EXECUTABLE}"
  RESULT_VARIABLE imports_result
  OUTPUT_VARIABLE imports
  ERROR_VARIABLE imports_error)
if(NOT imports_result EQUAL 0)
  message(FATAL_ERROR "dumpbin import audit failed: ${imports_error}")
endif()
string(TOUPPER "${imports}" imports_upper)
if(NOT imports_upper MATCHES "SL\\.INTERPOSER\\.DLL" OR
   NOT imports_upper MATCHES "DELAY LOAD IMPORT")
  message(FATAL_ERROR
          "Streamline interposer must be present only as a delay-load import so its signature is checked before loading")
endif()
