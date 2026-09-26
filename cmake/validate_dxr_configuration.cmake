include(CMakeParseArguments)

function(ab3d2_validate_dxr_options out_error)
  set(one_value_arguments
      PLATFORM_WINDOWS EMSCRIPTEN ENABLE_DXR GPU_VALIDATION ENABLE_STREAMLINE
      STREAMLINE_ROOT STREAMLINE_PROJECT_ID)
  cmake_parse_arguments(DXR "" "${one_value_arguments}" "" ${ARGN})
  set(error "")

  if(DXR_ENABLE_DXR AND (NOT DXR_PLATFORM_WINDOWS OR DXR_EMSCRIPTEN))
    set(error
        "AB3D2_ENABLE_DXR requires a native Windows build; Web and non-Windows builds use the RTX fail-fast stub")
  elseif(DXR_GPU_VALIDATION AND NOT DXR_ENABLE_DXR)
    set(error "AB3D2_DXR_GPU_VALIDATION requires AB3D2_ENABLE_DXR=ON")
  elseif(DXR_ENABLE_STREAMLINE AND NOT DXR_ENABLE_DXR)
    set(error "AB3D2_ENABLE_STREAMLINE requires AB3D2_ENABLE_DXR=ON")
  elseif(DXR_ENABLE_STREAMLINE AND NOT DXR_STREAMLINE_ROOT)
    set(error
        "AB3D2_ENABLE_STREAMLINE requires AB3D2_STREAMLINE_ROOT; no SDK is downloaded or discovered automatically")
  elseif(DXR_ENABLE_STREAMLINE AND NOT DXR_STREAMLINE_PROJECT_ID)
    set(error "AB3D2_ENABLE_STREAMLINE requires AB3D2_STREAMLINE_PROJECT_ID")
  elseif(DXR_ENABLE_STREAMLINE)
    string(LENGTH "${DXR_STREAMLINE_PROJECT_ID}" project_id_length)
    string(REPLACE "-" ";" project_id_parts "${DXR_STREAMLINE_PROJECT_ID}")
    list(LENGTH project_id_parts project_id_part_count)
    set(project_id_shape_valid TRUE)
    if(NOT project_id_part_count EQUAL 5)
      set(project_id_shape_valid FALSE)
    else()
      set(expected_lengths 8 4 4 4 12)
      foreach(index RANGE 0 4)
        list(GET project_id_parts ${index} project_id_part)
        list(GET expected_lengths ${index} expected_length)
        string(LENGTH "${project_id_part}" actual_length)
        if(NOT actual_length EQUAL expected_length)
          set(project_id_shape_valid FALSE)
        endif()
      endforeach()
    endif()
    if(NOT project_id_length EQUAL 36 OR NOT project_id_shape_valid OR
       NOT DXR_STREAMLINE_PROJECT_ID MATCHES "^[0-9A-Fa-f-]+$")
      set(error
          "AB3D2_STREAMLINE_PROJECT_ID must be a 36-character GUID in 8-4-4-4-12 form")
    endif()
  endif()

  set(${out_error} "${error}" PARENT_SCOPE)
endfunction()

function(ab3d2_validate_streamline_sdk out_error streamline_root)
  set(error "")
  # These are the exact files consumed or redistributed from NVIDIA's signed
  # streamline-sdk-v2.14.1.zip release (archive SHA-256
  # 92C4D954631A1710DA86CA3FA8D5034F2B9503838C95FC4AE977AE149319781B).
  # Every redistributed binary below carries a valid NVIDIA Authenticode
  # signature, so these hashes attest to a verified payload rather than to
  # whatever a download happened to produce.
  # Checking the extracted payload prevents a source checkout, development
  # plugin, or merely version-shaped directory from entering the build.
  set(required_file_hashes
      "include/sl.h|E1E81A7428D15B30DB37587E9469BD68A56D630D820A4ACCEC0AEE3B17E157DD"
      "include/sl_appidentity.h|1337385AC9867D66FA6BEB34C750E79AAF25A74A156A47E83F24937299DADE87"
      "include/sl_consts.h|17DE74AFDA2CB96204FF380464BED926F660D92015FE0304DFF4E329509C88B9"
      "include/sl_core_api.h|328DC3A2C1DEE579C200CA97E2D5B1EC38C893BE4BA2745EF5AD506AF7172E81"
      "include/sl_core_types.h|F420CB052CE14489FC9FA0D933C3ED81A010DEB01FA13B1AA8D45DA1C5BA3234"
      "include/sl_deepdvc.h|C6086FFE6B1E00588368FDB91E4AB9DD9617624E0E4344CD503A2CA73E46D1A4"
      "include/sl_device_wrappers.h|AF7741305B1A468C3A83EFAA2005ECD53D2FFD7AADB8EC6868C9DCB4A8A8E3A1"
      "include/sl_directsr.h|65D4E68B2A0F0425EA4D381379172197B81352A50A5E399A8E8F51953E765C71"
      "include/sl_dlss.h|D2C8C61FA71794C8BA424CEBCE5B3079EBFAB8E47ED98B4B00CF8B257ECFD6B5"
      "include/sl_dlss_d.h|6E51CF9815301C597D0657970A668147E34A20DB5A76D0AF254EC11248F5C4FE"
      "include/sl_dlss_g.h|1FC18CBE004E280DF1F787276D08A1B28B8A8C4C65856FBAA659F56DFF6A915D"
      "include/sl_helpers.h|48629EDBF25DFDB6444C91235FC278F0F9A5878243FAE83204C5906CE9EACD8A"
      "include/sl_helpers_vk.h|82604567D239D00AC0AC2270CEC744F6F11266E1D9D5187A95D4954F76D8C41C"
      "include/sl_hooks.h|CFEFF70A52E1CC012CF4C955A15CC9D0B290F7E084DCB3C0168A9AF06FF8F122"
      "include/sl_matrix_helpers.h|A65758D85ABBA1E12845D266D7D5E36AAEFA952383CDF093F3143CD6D8653341"
      "include/sl_nis.h|F45BE3CF149200E1EAE081CD144C95726A7A290A89822A5D52B9F538E52484AA"
      "include/sl_nvperf.h|025F9F1C8482E75A90F8B8EFCCDC9CFC53A9FC8B05BA3ACC6C3783708B66BD69"
      "include/sl_pcl.h|F43C5135FC8D5349CCD345E424F8CF1F61953A9F17E9897204B0741A3AB7B0FB"
      "include/sl_reflex.h|3B623A1189E04A686384D224A58C4AD9974C4E6E3204077676F6EC529475164C"
      "include/sl_result.h|2A0F6C12863BDC00B38910A5EC85D1F083C5671EE817A920AFE626AC2A9100F7"
      "include/sl_security.h|A36B1C20394402BEE7CE738089A5A1305FF6E9DD9DD517566154CE834F7C32B4"
      "include/sl_struct.h|28DEDA67EA1A74371DD4F33FF4842B7DB03B2DB007001DA6DB06F6982297928D"
      "include/sl_template.h|AF2D4A61361F2603872DCB6EDC86CC9E18912762A5C03E6A782FF39A8C3B871B"
      "include/sl_version.h|48E9CB86F0FF6711304BDF3F7150F46466878BBEFC21F648773267BC6A326285"
      "lib/x64/sl.interposer.lib|197618C9DB7F4D4E4553AF5F33807958634D01CD68603B7EC5820A0D8CDBA7D5"
      "bin/x64/sl.interposer.dll|8C87C9499461DA561EDD529AA9BF7831D67D7B94EBB1C1A5ED54EF4934E1EA4C"
      "bin/x64/sl.common.dll|82924A8954DD671E09351C5DE0EB87AD0EB25B944CC9F9AB955CA1D9950DE15D"
      "bin/x64/sl.dlss_d.dll|026B9F4F9EA848F2F5AA0E2024FC6B1C885F57EEA5D41078124DBA14F873B86B"
      "bin/x64/nvngx_dlssd.dll|4BC7EA5FCB2F32CF86BC2CB072E8D2860914A0BE511CBDCB2FC206C1D9D83B80"
      "bin/x64/nvngx_dlss.license.txt|3027F23CA5A46DD9CB8183FBD522983A86F64D7DAAC5982912BF9F214671F294"
      "license.txt|7B6F23E7D6F3AD6292F9308D2B42CDC3D82AE4E9B2ABB55F230279D83BEDD43D"
      "3rd-party-licenses.md|CB251639994465F31D2178C16ACEDF1FF4F9CEF1C370A09782ED36D4C86C1DCA")
  foreach(file_hash IN LISTS required_file_hashes)
    string(REPLACE "|" ";" file_hash_parts "${file_hash}")
    list(GET file_hash_parts 0 relative_path)
    list(GET file_hash_parts 1 expected_hash)
    if(NOT EXISTS "${streamline_root}/${relative_path}")
      set(error
          "AB3D2_STREAMLINE_ROOT is not a complete Streamline v2.14.1 production release; missing ${relative_path}")
      break()
    endif()
    file(SHA256 "${streamline_root}/${relative_path}" actual_hash)
    string(TOUPPER "${actual_hash}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
      set(error
          "AB3D2_STREAMLINE_ROOT does not match the pinned Streamline v2.14.1 production release; SHA-256 mismatch for ${relative_path}")
      break()
    endif()
  endforeach()
  if(NOT error)
    file(READ "${streamline_root}/include/sl_version.h" version_header)
    if(NOT version_header MATCHES "#define[ \t]+SL_VERSION_MAJOR[ \t]+2" OR
       NOT version_header MATCHES "#define[ \t]+SL_VERSION_MINOR[ \t]+14" OR
       NOT version_header MATCHES "#define[ \t]+SL_VERSION_PATCH[ \t]+1")
      set(error
          "AB3D2_STREAMLINE_ROOT must refer to the pinned Streamline v2.14.1 release")
    endif()
  endif()
  set(${out_error} "${error}" PARENT_SCOPE)
endfunction()

function(ab3d2_validate_dxc out_error dxc_executable)
  if(NOT dxc_executable OR NOT EXISTS "${dxc_executable}")
    set(${out_error}
        "AB3D2_ENABLE_DXR requires dxc.exe on PATH or an explicit AB3D2_DXC_EXECUTABLE file"
        PARENT_SCOPE)
  else()
    set(${out_error} "" PARENT_SCOPE)
  endif()
endfunction()
