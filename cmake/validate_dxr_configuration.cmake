include(CMakeParseArguments)

function(ab3d2_validate_dxr_options out_error)
  set(one_value_arguments
      PLATFORM_WINDOWS EMSCRIPTEN ENABLE_DXR GPU_VALIDATION ENABLE_STREAMLINE
      STREAMLINE_ROOT STREAMLINE_APPLICATION_ID)
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
  elseif(DXR_ENABLE_STREAMLINE AND NOT DXR_STREAMLINE_APPLICATION_ID)
    set(error
        "AB3D2_ENABLE_STREAMLINE requires an uncommitted NVIDIA-issued AB3D2_STREAMLINE_APPLICATION_ID")
  elseif(DXR_ENABLE_STREAMLINE)
    set(error
        "AB3D2_ENABLE_STREAMLINE is reserved for Phase 3 and is not available in the Phase 2 diagnostic foundation")
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
