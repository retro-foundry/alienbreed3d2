cmake_minimum_required(VERSION 3.16)

project(alienbreed3d2_pc VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(AB3D2_ENABLE_DXR
       "Build the experimental Windows DirectX 12/DXR renderer" OFF)
option(AB3D2_ENABLE_STREAMLINE
       "Enable the separately gated NVIDIA Streamline integration" OFF)
option(AB3D2_DXR_GPU_VALIDATION
       "Enable slow D3D12 GPU-based validation for DXR development" OFF)
set(AB3D2_DXC_EXECUTABLE "" CACHE FILEPATH
    "Explicit dxc executable for the DXR shader build (otherwise resolved from PATH)")

if(AB3D2_ENABLE_STREAMLINE)
  set(AB3D2_STREAMLINE_ROOT "$ENV{AB3D2_STREAMLINE_ROOT}" CACHE PATH
      "Extracted NVIDIA Streamline v2.12.0 production release root")
  set(AB3D2_STREAMLINE_APPLICATION_ID "$ENV{AB3D2_STREAMLINE_APPLICATION_ID}"
      CACHE STRING "Uncommitted NVIDIA-issued Streamline application ID")
endif()

include(cmake/validate_dxr_configuration.cmake)
ab3d2_validate_dxr_options(AB3D2_DXR_CONFIGURATION_ERROR
  PLATFORM_WINDOWS "${WIN32}"
  EMSCRIPTEN "${EMSCRIPTEN}"
  ENABLE_DXR "${AB3D2_ENABLE_DXR}"
  GPU_VALIDATION "${AB3D2_DXR_GPU_VALIDATION}"
  ENABLE_STREAMLINE "${AB3D2_ENABLE_STREAMLINE}"
  STREAMLINE_ROOT "${AB3D2_STREAMLINE_ROOT}"
  STREAMLINE_APPLICATION_ID "${AB3D2_STREAMLINE_APPLICATION_ID}"
)
if(AB3D2_DXR_CONFIGURATION_ERROR)
  message(FATAL_ERROR "${AB3D2_DXR_CONFIGURATION_ERROR}")
endif()

if(AB3D2_ENABLE_DXR)
  if(NOT AB3D2_DXC_EXECUTABLE)
    # The documented cache entry is deliberately initialized to an empty
    # string, which CMake considers a completed find result. Search through a
    # separate variable so PATH discovery still runs when no override exists.
    find_program(AB3D2_DXC_FROM_PATH NAMES dxc.exe dxc)
    if(AB3D2_DXC_FROM_PATH)
      set(AB3D2_DXC_EXECUTABLE "${AB3D2_DXC_FROM_PATH}" CACHE FILEPATH
          "Explicit dxc executable for the DXR shader build (otherwise resolved from PATH)"
          FORCE)
    endif()
    unset(AB3D2_DXC_FROM_PATH CACHE)
  endif()
  ab3d2_validate_dxc(AB3D2_DXR_DXC_ERROR "${AB3D2_DXC_EXECUTABLE}")
  if(AB3D2_DXR_DXC_ERROR)
    message(FATAL_ERROR "${AB3D2_DXR_DXC_ERROR}")
  endif()
  execute_process(
    COMMAND "${AB3D2_DXC_EXECUTABLE}" --version
    RESULT_VARIABLE AB3D2_DXC_VERSION_RESULT
    OUTPUT_VARIABLE AB3D2_DXC_VERSION
    ERROR_VARIABLE AB3D2_DXC_VERSION_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT AB3D2_DXC_VERSION_RESULT EQUAL 0)
    message(FATAL_ERROR
            "AB3D2_DXC_EXECUTABLE could not report its version: ${AB3D2_DXC_VERSION_ERROR}")
  endif()
  file(SHA256 "${AB3D2_DXC_EXECUTABLE}" AB3D2_DXC_SHA256)
  string(REPLACE "\n" " | " AB3D2_DXC_VERSION_ONE_LINE "${AB3D2_DXC_VERSION}")
  message(STATUS "DXR shader compiler: ${AB3D2_DXC_EXECUTABLE}")
  message(STATUS "DXR shader compiler version: ${AB3D2_DXC_VERSION_ONE_LINE}")
  message(STATUS "DXR shader compiler SHA-256: ${AB3D2_DXC_SHA256}")
endif()

# This PC target intentionally follows the desktop setup of the Alien Breed
# 3D I port: SDL2 is fetched at configure time and the same sources build on
# Windows, Linux, and macOS. The Amiga build remains under amiga/.
include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(NOT EMSCRIPTEN)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.11
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL2)
  find_package(OpenGL REQUIRED)
endif()

set(AB3D2_CORE_SOURCES
  src/alien_attack.c
  src/alien_animation.c
  src/alien_charge.c
  src/alien_damage.c
  src/alien_runtime.c
  src/alien_decision.c
  src/alien_death.c
  src/alien_dispatch.c
  src/alien_dark.c
  src/alien_flight.c
  src/alien_main.c
  src/alien_math.c
  src/alien_memory.c
  src/alien_pause.c
  src/alien_perception.c
  src/alien_prowl.c
  src/alien_run_around.c
  src/alien_setup.c
  src/alien_spatial.c
  src/alien_spawn.c
  src/alien_torch.c
  src/asset_io.c
  src/desktop_settings.c
  src/game_audio.c
  src/game_bootstrap.c
  src/game_controls.c
  src/game_hud.c
  src/game_input.c
  src/game_inventory.c
  src/game_link.c
  src/game_math.c
  src/game_menu.c
  src/game_preferences.c
  src/game_progression.c
  src/game_quicksave.c
  src/game_random.c
  src/game_resources.c
  src/game_save.c
  src/game_session.c
  src/game_vblank_clock.c
  src/level_bootstrap.c
  src/level_dynamic_state.c
  src/level_draw_graph.c
  src/lighting_runtime.c
  src/message_runtime.c
  src/level_mechanisms.c
  src/level_navigation.c
  src/level_runtime.c
  src/level_static_scene.c
  src/level_transition.c
  src/mechanism_runtime.c
  src/object_activatables.c
  src/object_animation.c
  src/object_blast.c
  src/object_collectables.c
  src/object_collision.c
  src/object_explosion.c
  src/object_handler.c
  src/object_heading.c
  src/object_movement.c
  src/object_observation.c
  src/object_passives.c
  src/object_projectiles.c
  src/object_runtime.c
  src/object_scene.c
  src/object_teleport.c
  src/object_viewpoint.c
  src/object_visibility.c
  src/object_worry.c
  src/player_entity.c
  src/player_runtime.c
  src/player_shoot.c
  src/render_view.c
  src/renderer_backend.c
  src/scene_frame.c
  src/scene_geometry_compile.c
  src/source_bitmap_lighting.c
  src/source_bitmap_sprite.c
  src/source_flat_visibility.c
  src/source_vector_model_transform.c
  src/source_vector_model_scene.c
  src/source_vector_projection.c
  src/source_world_material.c
  src/ui_text_layout.c
  src/world_light_tessellation.c
  # Reused =SB= decoder from the first PC port. It matches
  # modules/file_io.s:io_HandlePacked, which calls the original unLHA code.
  src/sb_decompress.c
)

add_library(ab3d2_core STATIC ${AB3D2_CORE_SOURCES})
target_include_directories(ab3d2_core PUBLIC src)
if(NOT MSVC)
  target_link_libraries(ab3d2_core PUBLIC m)
endif()

# Development-only exporter for the decoded source samples and packed tracker
# module.  The committed WAVs are generated from this instead of treating the
# original CSFX/LHA files as host PCM.
if(NOT EMSCRIPTEN)
  add_executable(ab3d2_audio_export tools/audio_export.c)
  target_link_libraries(ab3d2_audio_export PRIVATE ab3d2_core)
  target_include_directories(ab3d2_audio_export PRIVATE src)
endif()

set(AB3D2_RENDERER_RTX_SOURCES src/renderer_rtx_stub.c)
if(AB3D2_ENABLE_DXR)
  set(AB3D2_RENDERER_RTX_SOURCES
    src/renderer_rtx.cpp
    src/renderer_dxr/dxr_debug.cpp
    src/renderer_dxr/dxr_device.cpp
    src/renderer_dxr/dxr_pipeline.cpp
    src/renderer_dxr/dxr_renderer.cpp
    src/renderer_dxr/dxr_scene.cpp
  )
endif()

add_executable(ab3d2
  src/audio_sdl.c
  src/main.c
  src/renderer.c
  src/renderer_opengl.c
  ${AB3D2_RENDERER_RTX_SOURCES}
)
target_link_libraries(ab3d2 PRIVATE ab3d2_core)
if(NOT EMSCRIPTEN)
  target_link_libraries(ab3d2 PRIVATE SDL2-static OpenGL::GL)
endif()
target_include_directories(ab3d2 PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
if(MSVC)
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ab3d2)
endif()

if(AB3D2_ENABLE_DXR)
  set_target_properties(
    ab3d2 PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    VS_DEBUGGER_COMMAND_ARGUMENTS "--renderer rtx"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ab3d2>"
  )
  target_compile_definitions(ab3d2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_definitions(
    ab3d2 PRIVATE
    $<$<CONFIG:Debug>:AB3D2_DXR_ENABLE_DEBUG_LAYER=1>
    $<$<BOOL:${AB3D2_DXR_GPU_VALIDATION}>:AB3D2_DXR_ENABLE_DEBUG_LAYER=1>
    $<$<BOOL:${AB3D2_DXR_GPU_VALIDATION}>:AB3D2_DXR_GPU_VALIDATION=1>
  )
  target_link_libraries(ab3d2 PRIVATE d3d12 dxgi dxguid)

  set(AB3D2_DXR_SHADER_SOURCE
      "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer_dxr/shaders/diagnostic.hlsl")
  set(AB3D2_DXR_SHADER_BUILD_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/generated/renderer_dxr")
  set(AB3D2_DXR_VERTEX_SHADER
      "${AB3D2_DXR_SHADER_BUILD_DIR}/diagnostic_vs.dxil")
  set(AB3D2_DXR_PIXEL_SHADER
      "${AB3D2_DXR_SHADER_BUILD_DIR}/diagnostic_ps.dxil")
  set(AB3D2_DXR_PRESENT_SHADER_SOURCE
      "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer_dxr/shaders/present.hlsl")
  set(AB3D2_DXR_PRESENT_PIXEL_SHADER
      "${AB3D2_DXR_SHADER_BUILD_DIR}/present_ps.dxil")
  set(AB3D2_DXR_PRESENT_VERTEX_SHADER
      "${AB3D2_DXR_SHADER_BUILD_DIR}/present_vs.dxil")
  set(AB3D2_DXR_PATH_TRACE_SHADER_SOURCE
      "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer_dxr/shaders/path_trace.hlsl")
  set(AB3D2_DXR_PATH_TRACE_SHADER
      "${AB3D2_DXR_SHADER_BUILD_DIR}/path_trace.dxil")
  add_custom_command(
    OUTPUT "${AB3D2_DXR_VERTEX_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AB3D2_DXR_SHADER_BUILD_DIR}"
    COMMAND "${AB3D2_DXC_EXECUTABLE}" -nologo -WX -Ges -O3 -Qstrip_debug
            -Qstrip_reflect -T vs_6_6 -E vs_main
            -Fo "${AB3D2_DXR_VERTEX_SHADER}" "${AB3D2_DXR_SHADER_SOURCE}"
    DEPENDS "${AB3D2_DXR_SHADER_SOURCE}"
    COMMENT "Compile the DXR diagnostic vertex shader with DXC"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${AB3D2_DXR_PIXEL_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AB3D2_DXR_SHADER_BUILD_DIR}"
    COMMAND "${AB3D2_DXC_EXECUTABLE}" -nologo -WX -Ges -O3 -Qstrip_debug
            -Qstrip_reflect -T ps_6_6 -E ps_main
            -Fo "${AB3D2_DXR_PIXEL_SHADER}" "${AB3D2_DXR_SHADER_SOURCE}"
    DEPENDS "${AB3D2_DXR_SHADER_SOURCE}"
    COMMENT "Compile the DXR diagnostic pixel shader with DXC"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${AB3D2_DXR_PRESENT_VERTEX_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AB3D2_DXR_SHADER_BUILD_DIR}"
    COMMAND "${AB3D2_DXC_EXECUTABLE}" -nologo -WX -Ges -O3 -Qstrip_debug
            -Qstrip_reflect -T vs_6_6 -E vs_main
            -Fo "${AB3D2_DXR_PRESENT_VERTEX_SHADER}"
            "${AB3D2_DXR_PRESENT_SHADER_SOURCE}"
    DEPENDS "${AB3D2_DXR_PRESENT_SHADER_SOURCE}"
    COMMENT "Compile the DXR noisy-HDR presentation vertex shader with DXC"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${AB3D2_DXR_PRESENT_PIXEL_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AB3D2_DXR_SHADER_BUILD_DIR}"
    COMMAND "${AB3D2_DXC_EXECUTABLE}" -nologo -WX -Ges -O3 -Qstrip_debug
            -Qstrip_reflect -T ps_6_6 -E ps_main
            -Fo "${AB3D2_DXR_PRESENT_PIXEL_SHADER}"
            "${AB3D2_DXR_PRESENT_SHADER_SOURCE}"
    DEPENDS "${AB3D2_DXR_PRESENT_SHADER_SOURCE}"
    COMMENT "Compile the DXR noisy-HDR presentation shader with DXC"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${AB3D2_DXR_PATH_TRACE_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AB3D2_DXR_SHADER_BUILD_DIR}"
    COMMAND "${AB3D2_DXC_EXECUTABLE}" -nologo -WX -Ges -O3 -Qstrip_debug
            -Qstrip_reflect -T lib_6_3
            -Fo "${AB3D2_DXR_PATH_TRACE_SHADER}"
            "${AB3D2_DXR_PATH_TRACE_SHADER_SOURCE}"
    DEPENDS "${AB3D2_DXR_PATH_TRACE_SHADER_SOURCE}"
    COMMENT "Compile the DXR fresh-sample path tracer with DXC"
    VERBATIM
  )
  add_custom_target(ab3d2_dxr_shaders
                    DEPENDS "${AB3D2_DXR_VERTEX_SHADER}"
                            "${AB3D2_DXR_PIXEL_SHADER}"
                            "${AB3D2_DXR_PRESENT_VERTEX_SHADER}"
                            "${AB3D2_DXR_PRESENT_PIXEL_SHADER}"
                            "${AB3D2_DXR_PATH_TRACE_SHADER}")
  add_dependencies(ab3d2 ab3d2_dxr_shaders)

  set(AB3D2_DXR_MATERIAL_SOURCE_SPEC
      "${CMAKE_CURRENT_SOURCE_DIR}/data/renderer_dxr/material_sources.json")
  set(AB3D2_DXR_MATERIAL_BUILD_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/generated/renderer_dxr/materials")
  set(AB3D2_DXR_MATERIAL_OUTPUTS
      "${AB3D2_DXR_MATERIAL_BUILD_DIR}/material_manifest.json"
      "${AB3D2_DXR_MATERIAL_BUILD_DIR}/material_runtime.bin")
  set(AB3D2_DXR_MATERIAL_NAMES
      alienredwall brownpipes brownspeakers brownwithyellowstripes chevrondoor
      floor_0201 gieger hullmetal redhullmetal rocky steampunk technolights
      technotritile)
  foreach(AB3D2_DXR_MATERIAL_NAME IN LISTS AB3D2_DXR_MATERIAL_NAMES)
    foreach(AB3D2_DXR_MATERIAL_CHANNEL IN ITEMS base_color normal metalness roughness)
      list(APPEND AB3D2_DXR_MATERIAL_OUTPUTS
           "${AB3D2_DXR_MATERIAL_BUILD_DIR}/${AB3D2_DXR_MATERIAL_NAME}_${AB3D2_DXR_MATERIAL_CHANNEL}.png")
    endforeach()
  endforeach()
  file(GLOB AB3D2_DXR_MATERIAL_SHEETS CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/textures_pbr/*.png")
  add_custom_command(
    OUTPUT ${AB3D2_DXR_MATERIAL_OUTPUTS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AB3D2_DXR_MATERIAL_BUILD_DIR}"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_dxr_materials.py"
            --source-dir "${CMAKE_CURRENT_SOURCE_DIR}/textures_pbr"
            --spec "${AB3D2_DXR_MATERIAL_SOURCE_SPEC}"
            --output-dir "${AB3D2_DXR_MATERIAL_BUILD_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_dxr_materials.py"
            "${AB3D2_DXR_MATERIAL_SOURCE_SPEC}"
            ${AB3D2_DXR_MATERIAL_SHEETS}
    COMMENT "Build renderer-native DXR material textures"
    VERBATIM
  )
  add_custom_target(ab3d2_dxr_materials DEPENDS ${AB3D2_DXR_MATERIAL_OUTPUTS})
  add_dependencies(ab3d2 ab3d2_dxr_materials)
  add_custom_command(TARGET ab3d2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:ab3d2>/renderer_dxr"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AB3D2_DXR_VERTEX_SHADER}"
            "$<TARGET_FILE_DIR:ab3d2>/renderer_dxr/diagnostic_vs.dxil"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AB3D2_DXR_PIXEL_SHADER}"
            "$<TARGET_FILE_DIR:ab3d2>/renderer_dxr/diagnostic_ps.dxil"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AB3D2_DXR_PRESENT_VERTEX_SHADER}"
            "$<TARGET_FILE_DIR:ab3d2>/renderer_dxr/present_vs.dxil"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AB3D2_DXR_PRESENT_PIXEL_SHADER}"
            "$<TARGET_FILE_DIR:ab3d2>/renderer_dxr/present_ps.dxil"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AB3D2_DXR_PATH_TRACE_SHADER}"
            "$<TARGET_FILE_DIR:ab3d2>/renderer_dxr/path_trace.dxil"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${AB3D2_DXR_MATERIAL_BUILD_DIR}"
            "$<TARGET_FILE_DIR:ab3d2>/renderer_dxr/materials"
    COMMENT "Stage project-built DXR shaders and materials"
  )
endif()

# Match the first port's Windows manifest.  Its desktop-sized normal SDL
# window is intentionally DPI-unaware; without this, Windows can apply a
# different non-client-area scale and leave a visible desktop edge.
if(MSVC AND NOT EMSCRIPTEN)
  set_property(TARGET ab3d2 PROPERTY VS_DPI_AWARE OFF)
endif()

# The authoritative game assets are preserved byte-for-byte. Their Amiga
# volume-relative layout becomes executable-local data/ with lower-case paths,
# avoiding Amiga's case-insensitive filesystem assumption on Linux/macOS.
if(EMSCRIPTEN)
  # stage_media.py is also the source of the lower-case browser preload tree.
  set(AB3D2_WEB_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/web_data")
  add_custom_target(ab3d2_web_media
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/stage_media.py"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/amiga/media"
            --destination "${AB3D2_WEB_DATA_DIR}"
    COMMENT "Stage authoritative AB3D2 media for WebGL preload"
  )
  add_dependencies(ab3d2 ab3d2_web_media)
  add_custom_target(ab3d2_web_audio
    COMMAND ${CMAKE_COMMAND} -E make_directory "${AB3D2_WEB_DATA_DIR}/audio"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio"
            "${AB3D2_WEB_DATA_DIR}/audio"
    DEPENDS ab3d2_web_media
    COMMENT "Stage converted AB3D2 WAV audio for WebAudio preload"
  )
  add_dependencies(ab3d2 ab3d2_web_audio)
  set(AB3D2_WEB_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/web_fonts")
  add_custom_target(ab3d2_web_fonts
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${AB3D2_WEB_FONT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
            "${AB3D2_WEB_FONT_DIR}"
    COMMENT "Stage first-port bitmap fonts for WebGL preload"
  )
  add_dependencies(ab3d2 ab3d2_web_fonts)
  target_compile_options(ab3d2 PRIVATE "SHELL:-sUSE_SDL=2")
  target_link_options(ab3d2 PRIVATE
    "SHELL:-sUSE_SDL=2"
    "SHELL:-sFULL_ES2=1"
    "SHELL:-sMIN_WEBGL_VERSION=1"
    "SHELL:-sMAX_WEBGL_VERSION=2"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:--preload-file=${AB3D2_WEB_DATA_DIR}@/data"
    "SHELL:--preload-file=${AB3D2_WEB_FONT_DIR}@/fonts"
  )
  set_target_properties(ab3d2 PROPERTIES SUFFIX ".html")
else()
  add_custom_command(TARGET ab3d2 POST_BUILD
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/stage_media.py"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/amiga/media"
            --destination "$<TARGET_FILE_DIR:ab3d2>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio"
            "$<TARGET_FILE_DIR:ab3d2>/data/audio"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/fonts"
            "$<TARGET_FILE_DIR:ab3d2>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/README.txt"
            "$<TARGET_FILE_DIR:ab3d2>/README.txt"
    COMMAND ${CMAKE_COMMAND}
            -DDEST_DIR="$<TARGET_FILE_DIR:ab3d2>"
            -DSRC_FILE="${CMAKE_CURRENT_SOURCE_DIR}/ab3d2.ini.template"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_ab3d2_ini_if_missing.cmake"
    COMMENT "Stage authoritative AB3D2 media as case-stable executable-local data/"
  )
  set_property(TARGET ab3d2 APPEND PROPERTY ADDITIONAL_CLEAN_FILES
               "$<TARGET_FILE_DIR:ab3d2>/ab3d2.ini")
  add_custom_target(ab3d2_gpu_smoke
    COMMAND $<TARGET_FILE:ab3d2> --gpu-smoke all --renderer opengl
    DEPENDS ab3d2
    COMMENT "Run the opt-in hidden-window OpenGL scene/shader smoke test"
  )
endif()

if(WIN32 AND NOT EMSCRIPTEN)
  target_link_libraries(ab3d2 PRIVATE winmm imm32 version setupapi)
endif()

if(MSVC)
  target_compile_options(ab3d2_core PRIVATE /W4)
  target_compile_options(ab3d2 PRIVATE /W4)
  if(NOT EMSCRIPTEN)
    target_compile_options(ab3d2_audio_export PRIVATE /W4)
  endif()
  target_compile_definitions(ab3d2_core PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(ab3d2 PRIVATE _CRT_SECURE_NO_WARNINGS)
  if(NOT EMSCRIPTEN)
    target_compile_definitions(ab3d2_audio_export PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
else()
  target_compile_options(ab3d2_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(ab3d2 PRIVATE -Wall -Wextra -Wpedantic)
  if(NOT EMSCRIPTEN)
    target_compile_options(ab3d2_audio_export PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

# The decompressor is reused as a self-contained library rather than the
# first port's logging-integrated runtime module.
target_compile_definitions(ab3d2_core PRIVATE AB3D_STANDALONE_TOOL)

include(CTest)
if(BUILD_TESTING AND NOT EMSCRIPTEN)
  add_test(NAME ab3d2_dxr_configuration_test
    COMMAND ${CMAKE_COMMAND}
            -DAB3D2_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/dxr_configuration_test.cmake)
  add_test(NAME ab3d2_dxr_material_builder_test
    COMMAND "${Python3_EXECUTABLE}" -m unittest
            tests.test_build_dxr_materials)
  set_tests_properties(ab3d2_dxr_material_builder_test PROPERTIES
                       WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  if(AB3D2_ENABLE_DXR)
    add_executable(ab3d2_renderer_rtx_foundation_test
      tests/renderer_rtx_foundation_test.c
      ${AB3D2_RENDERER_RTX_SOURCES}
    )
    set_target_properties(ab3d2_renderer_rtx_foundation_test
                          PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
                                     CXX_EXTENSIONS OFF)
    target_include_directories(ab3d2_renderer_rtx_foundation_test PRIVATE src)
    target_compile_definitions(
      ab3d2_renderer_rtx_foundation_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
      $<$<CONFIG:Debug>:AB3D2_DXR_ENABLE_DEBUG_LAYER=1>
      $<$<BOOL:${AB3D2_DXR_GPU_VALIDATION}>:AB3D2_DXR_ENABLE_DEBUG_LAYER=1>
      $<$<BOOL:${AB3D2_DXR_GPU_VALIDATION}>:AB3D2_DXR_GPU_VALIDATION=1>
    )
    target_link_libraries(ab3d2_renderer_rtx_foundation_test
                          PRIVATE ab3d2_core SDL2-static d3d12 dxgi dxguid user32)
    add_dependencies(ab3d2_renderer_rtx_foundation_test ab3d2_dxr_shaders)
    add_custom_command(TARGET ab3d2_renderer_rtx_foundation_test POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "$<TARGET_FILE_DIR:ab3d2_renderer_rtx_foundation_test>/renderer_dxr"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${AB3D2_DXR_VERTEX_SHADER}"
              "$<TARGET_FILE_DIR:ab3d2_renderer_rtx_foundation_test>/renderer_dxr/diagnostic_vs.dxil"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${AB3D2_DXR_PIXEL_SHADER}"
              "$<TARGET_FILE_DIR:ab3d2_renderer_rtx_foundation_test>/renderer_dxr/diagnostic_ps.dxil"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${AB3D2_DXR_PRESENT_VERTEX_SHADER}"
              "$<TARGET_FILE_DIR:ab3d2_renderer_rtx_foundation_test>/renderer_dxr/present_vs.dxil"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${AB3D2_DXR_PRESENT_PIXEL_SHADER}"
              "$<TARGET_FILE_DIR:ab3d2_renderer_rtx_foundation_test>/renderer_dxr/present_ps.dxil"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${AB3D2_DXR_PATH_TRACE_SHADER}"
              "$<TARGET_FILE_DIR:ab3d2_renderer_rtx_foundation_test>/renderer_dxr/path_trace.dxil"
      COMMENT "Stage DXR diagnostic shaders for the foundation test"
    )
    add_test(NAME ab3d2_renderer_rtx_foundation_test
             COMMAND ab3d2_renderer_rtx_foundation_test)
    add_test(NAME ab3d2_renderer_rtx_game_smoke
             COMMAND $<TARGET_FILE:ab3d2> --gpu-smoke all --renderer rtx)
    set_tests_properties(ab3d2_renderer_rtx_game_smoke PROPERTIES TIMEOUT 120)
  else()
    add_executable(ab3d2_renderer_rtx_stub_test
                   tests/renderer_rtx_stub_test.c src/renderer_rtx_stub.c)
    target_include_directories(ab3d2_renderer_rtx_stub_test PRIVATE src)
    add_test(NAME ab3d2_renderer_rtx_stub_test COMMAND ab3d2_renderer_rtx_stub_test)
  endif()
  add_executable(ab3d2_desktop_settings_test tests/desktop_settings_test.c)
  target_link_libraries(ab3d2_desktop_settings_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_desktop_settings_test PRIVATE src)
  add_test(NAME ab3d2_desktop_settings_test COMMAND ab3d2_desktop_settings_test)
  add_executable(ab3d2_game_quicksave_test tests/game_quicksave_test.c)
  target_link_libraries(ab3d2_game_quicksave_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_game_quicksave_test PRIVATE src)
  add_dependencies(ab3d2_game_quicksave_test ab3d2)
  add_test(
    NAME ab3d2_game_quicksave_test
    COMMAND ab3d2_game_quicksave_test "$<TARGET_FILE_DIR:ab3d2>/data"
            "$<TARGET_FILE_DIR:ab3d2>/quicksave-test.bin"
  )
  add_executable(ab3d2_world_light_tessellation_test
                 tests/world_light_tessellation_test.c)
  target_link_libraries(ab3d2_world_light_tessellation_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_world_light_tessellation_test PRIVATE src)
  add_test(NAME ab3d2_world_light_tessellation_test
           COMMAND ab3d2_world_light_tessellation_test)
  add_executable(ab3d2_scene_geometry_compile_test
                 tests/scene_geometry_compile_test.c)
  target_link_libraries(ab3d2_scene_geometry_compile_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_scene_geometry_compile_test PRIVATE src)
  add_test(NAME ab3d2_scene_geometry_compile_test
           COMMAND ab3d2_scene_geometry_compile_test)
  add_executable(ab3d2_source_flat_visibility_test
                 tests/source_flat_visibility_test.c)
  target_link_libraries(ab3d2_source_flat_visibility_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_source_flat_visibility_test PRIVATE src)
  add_test(NAME ab3d2_source_flat_visibility_test
           COMMAND ab3d2_source_flat_visibility_test)
  add_executable(ab3d2_source_world_material_test
                 tests/source_world_material_test.c)
  target_link_libraries(ab3d2_source_world_material_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_source_world_material_test PRIVATE src)
  add_test(NAME ab3d2_source_world_material_test
           COMMAND ab3d2_source_world_material_test)
  add_executable(ab3d2_source_bitmap_lighting_test
                 tests/source_bitmap_lighting_test.c)
  target_link_libraries(ab3d2_source_bitmap_lighting_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_source_bitmap_lighting_test PRIVATE src)
  add_test(NAME ab3d2_source_bitmap_lighting_test
           COMMAND ab3d2_source_bitmap_lighting_test)
  add_executable(ab3d2_source_renderer_assets_test
                 tests/source_renderer_assets_test.c)
  target_link_libraries(ab3d2_source_renderer_assets_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_source_renderer_assets_test PRIVATE src)
  add_test(NAME ab3d2_source_renderer_assets_test
           COMMAND ab3d2_source_renderer_assets_test)
  add_executable(ab3d2_source_vector_model_transform_test
                 tests/source_vector_model_transform_test.c)
  target_link_libraries(ab3d2_source_vector_model_transform_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_source_vector_model_transform_test PRIVATE src)
  add_test(NAME ab3d2_source_vector_model_transform_test
           COMMAND ab3d2_source_vector_model_transform_test)
  add_executable(ab3d2_ui_text_layout_test tests/ui_text_layout_test.c)
  target_link_libraries(ab3d2_ui_text_layout_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_ui_text_layout_test PRIVATE src)
  add_test(NAME ab3d2_ui_text_layout_test COMMAND ab3d2_ui_text_layout_test)
  add_executable(ab3d2_level_transition_test tests/level_transition_test.c)
  target_link_libraries(ab3d2_level_transition_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_level_transition_test PRIVATE src)
  add_test(NAME ab3d2_level_transition_test COMMAND ab3d2_level_transition_test)
  add_executable(ab3d2_level_data_test tests/level_data_test.c)
  target_link_libraries(ab3d2_level_data_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_level_data_test PRIVATE src)
  add_dependencies(ab3d2_level_data_test ab3d2)
  if(MSVC)
    # This source-parity fixture intentionally keeps several complete runtime
    # snapshots alive at once.  Give the Windows test process the same stack
    # headroom it has on the other native hosts.
    target_link_options(ab3d2_level_data_test PRIVATE "/STACK:8388608")
  endif()
  add_test(
    NAME ab3d2_level_data_test
    COMMAND ab3d2_level_data_test "$<TARGET_FILE_DIR:ab3d2>/data"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga/ab3d2_old_source/boot.dat"
  )
  add_executable(ab3d2_audio_sdl_test tests/audio_sdl_test.c src/audio_sdl.c)
  target_link_libraries(ab3d2_audio_sdl_test PRIVATE ab3d2_core SDL2-static)
  target_include_directories(ab3d2_audio_sdl_test PRIVATE src)
  target_compile_definitions(ab3d2_audio_sdl_test PRIVATE AB3D2_AUDIO_SDL_TEST)
  add_dependencies(ab3d2_audio_sdl_test ab3d2)
  add_test(
    NAME ab3d2_audio_sdl_test
    COMMAND ab3d2_audio_sdl_test "$<TARGET_FILE_DIR:ab3d2>/data"
  )
  set_tests_properties(ab3d2_audio_sdl_test PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
endif()
