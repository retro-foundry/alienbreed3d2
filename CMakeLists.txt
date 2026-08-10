cmake_minimum_required(VERSION 3.16)

project(alienbreed3d2_pc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# This PC target intentionally follows the desktop setup of the Alien Breed
# 3D I port: SDL2 is fetched at configure time and the same sources build on
# Windows, Linux, and macOS. The Amiga build remains under amiga/.
include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
  src/game_bootstrap.c
  src/game_controls.c
  src/game_input.c
  src/game_inventory.c
  src/game_link.c
  src/game_math.c
  src/game_menu.c
  src/game_preferences.c
  src/game_progression.c
  src/game_random.c
  src/game_resources.c
  src/game_save.c
  src/game_session.c
  src/level_bootstrap.c
  src/level_dynamic_state.c
  src/level_draw_graph.c
  src/lighting_runtime.c
  src/message_runtime.c
  src/level_mechanisms.c
  src/level_navigation.c
  src/level_runtime.c
  src/level_static_scene.c
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
  src/scene_frame.c
  # Reused =SB= decoder from the first PC port. It matches
  # modules/file_io.s:io_HandlePacked, which calls the original unLHA code.
  src/sb_decompress.c
)

add_library(ab3d2_core STATIC ${AB3D2_CORE_SOURCES})
target_include_directories(ab3d2_core PUBLIC src)

add_executable(ab3d2
  src/main.c
  src/renderer_stub.c
)
target_link_libraries(ab3d2 PRIVATE ab3d2_core SDL2-static)
target_include_directories(ab3d2 PRIVATE src)

# The authoritative game assets are preserved byte-for-byte. Their Amiga
# volume-relative layout becomes executable-local data/ with lower-case paths,
# avoiding Amiga's case-insensitive filesystem assumption on Linux/macOS.
add_custom_command(TARGET ab3d2 POST_BUILD
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/stage_media.py"
          --source "${CMAKE_CURRENT_SOURCE_DIR}/amiga/media"
          --destination "$<TARGET_FILE_DIR:ab3d2>/data"
  COMMENT "Stage authoritative AB3D2 media as case-stable executable-local data/"
)

if(WIN32)
  target_link_libraries(ab3d2 PRIVATE winmm imm32 version setupapi)
endif()

if(MSVC)
  target_compile_options(ab3d2_core PRIVATE /W4)
  target_compile_options(ab3d2 PRIVATE /W4)
  target_compile_definitions(ab3d2_core PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(ab3d2 PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(ab3d2_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(ab3d2 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The decompressor is reused as a self-contained library rather than the
# first port's logging-integrated runtime module.
target_compile_definitions(ab3d2_core PRIVATE AB3D_STANDALONE_TOOL)

include(CTest)
if(BUILD_TESTING)
  add_executable(ab3d2_level_data_test tests/level_data_test.c)
  target_link_libraries(ab3d2_level_data_test PRIVATE ab3d2_core)
  target_include_directories(ab3d2_level_data_test PRIVATE src)
  add_dependencies(ab3d2_level_data_test ab3d2)
  add_test(
    NAME ab3d2_level_data_test
    COMMAND ab3d2_level_data_test "$<TARGET_FILE_DIR:ab3d2>/data"
            "${CMAKE_CURRENT_SOURCE_DIR}/amiga/ab3d2_old_source/boot.dat"
  )
endif()
