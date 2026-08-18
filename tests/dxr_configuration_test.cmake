if(NOT AB3D2_SOURCE_DIR)
  message(FATAL_ERROR "AB3D2_SOURCE_DIR was not supplied to the DXR configuration test")
endif()
include("${AB3D2_SOURCE_DIR}/cmake/validate_dxr_configuration.cmake")

function(expect_dxr_error expected)
  ab3d2_validate_dxr_options(actual ${ARGN})
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
            "DXR configuration error mismatch\nexpected: ${expected}\nactual: ${actual}")
  endif()
endfunction()

expect_dxr_error(""
  PLATFORM_WINDOWS OFF EMSCRIPTEN OFF ENABLE_DXR OFF GPU_VALIDATION OFF
  ENABLE_STREAMLINE OFF)
expect_dxr_error(
  "AB3D2_ENABLE_DXR requires a native Windows build; Web and non-Windows builds use the RTX fail-fast stub"
  PLATFORM_WINDOWS OFF EMSCRIPTEN OFF ENABLE_DXR ON GPU_VALIDATION OFF
  ENABLE_STREAMLINE OFF)
expect_dxr_error(
  "AB3D2_ENABLE_DXR requires a native Windows build; Web and non-Windows builds use the RTX fail-fast stub"
  PLATFORM_WINDOWS ON EMSCRIPTEN ON ENABLE_DXR ON GPU_VALIDATION OFF
  ENABLE_STREAMLINE OFF)
expect_dxr_error("AB3D2_DXR_GPU_VALIDATION requires AB3D2_ENABLE_DXR=ON"
  PLATFORM_WINDOWS ON EMSCRIPTEN OFF ENABLE_DXR OFF GPU_VALIDATION ON
  ENABLE_STREAMLINE OFF)
expect_dxr_error("AB3D2_ENABLE_STREAMLINE requires AB3D2_ENABLE_DXR=ON"
  PLATFORM_WINDOWS ON EMSCRIPTEN OFF ENABLE_DXR OFF GPU_VALIDATION OFF
  ENABLE_STREAMLINE ON)
expect_dxr_error(
  "AB3D2_ENABLE_STREAMLINE requires AB3D2_STREAMLINE_ROOT; no SDK is downloaded or discovered automatically"
  PLATFORM_WINDOWS ON EMSCRIPTEN OFF ENABLE_DXR ON GPU_VALIDATION OFF
  ENABLE_STREAMLINE ON)
expect_dxr_error(
  "AB3D2_ENABLE_STREAMLINE requires AB3D2_STREAMLINE_PROJECT_ID"
  PLATFORM_WINDOWS ON EMSCRIPTEN OFF ENABLE_DXR ON GPU_VALIDATION OFF
  ENABLE_STREAMLINE ON STREAMLINE_ROOT "C:/streamline")
expect_dxr_error(
  "AB3D2_STREAMLINE_PROJECT_ID must be a 36-character GUID in 8-4-4-4-12 form"
  PLATFORM_WINDOWS ON EMSCRIPTEN OFF ENABLE_DXR ON GPU_VALIDATION OFF
  ENABLE_STREAMLINE ON STREAMLINE_ROOT "C:/streamline"
  STREAMLINE_PROJECT_ID "not-a-guid")
expect_dxr_error(""
  PLATFORM_WINDOWS ON EMSCRIPTEN OFF ENABLE_DXR ON GPU_VALIDATION OFF
  ENABLE_STREAMLINE ON STREAMLINE_ROOT "C:/streamline"
  STREAMLINE_PROJECT_ID "6b0d5e3a-a774-4e9e-8937-e6ca29a6885e")

set(incomplete_streamline_root
    "${CMAKE_CURRENT_BINARY_DIR}/incomplete-streamline-sdk")
file(MAKE_DIRECTORY "${incomplete_streamline_root}")
ab3d2_validate_streamline_sdk(streamline_sdk_error
                              "${incomplete_streamline_root}")
if(NOT streamline_sdk_error MATCHES "missing include/sl.h")
  message(FATAL_ERROR
          "Incomplete Streamline SDK failure was not explicit: ${streamline_sdk_error}")
endif()

set(altered_streamline_root
    "${CMAKE_CURRENT_BINARY_DIR}/altered-streamline-sdk")
file(MAKE_DIRECTORY "${altered_streamline_root}/include")
file(WRITE "${altered_streamline_root}/include/sl.h" "altered")
ab3d2_validate_streamline_sdk(streamline_sdk_error
                              "${altered_streamline_root}")
if(NOT streamline_sdk_error MATCHES "SHA-256 mismatch for include/sl.h")
  message(FATAL_ERROR
          "Altered Streamline SDK failure was not explicit: ${streamline_sdk_error}")
endif()

ab3d2_validate_dxc(dxc_error "C:/a/path/which/does/not/exist/dxc.exe")
if(NOT dxc_error STREQUAL
   "AB3D2_ENABLE_DXR requires dxc.exe on PATH or an explicit AB3D2_DXC_EXECUTABLE file")
  message(FATAL_ERROR "DXC configuration failure was not explicit: ${dxc_error}")
endif()
