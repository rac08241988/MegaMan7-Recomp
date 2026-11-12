cmake_minimum_required(VERSION 3.20)
project(MegaMan7SNESRecomp C CXX)

set(CMAKE_C_STANDARD 11)

# Release pin for lobby matching. Release builds stamp the VERSION file so two
# peers can prove they are the same build; everything else stamps "dev".
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _GAME_VER_RAW)
string(STRIP "${_GAME_VER_RAW}" GAME_RELEASE_VERSION)
set(SNESRECOMP_BUILD_VERSION "" CACHE STRING
    "Lobby release pin (empty = Release uses VERSION, else dev)")
if(SNESRECOMP_BUILD_VERSION STREQUAL "")
    get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_is_multi)
        set(SNESRECOMP_BUILD_VERSION "$<IF:$<CONFIG:Release>,${GAME_RELEASE_VERSION},dev>")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(SNESRECOMP_BUILD_VERSION "${GAME_RELEASE_VERSION}")
    else()
        set(SNESRECOMP_BUILD_VERSION "dev")
    endif()
endif()

set(SNESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")
if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
    message(FATAL_ERROR
        "snesrecomp submodule missing at ${SNESRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive")
endif()
# Mod packages are part of every project, not an option. The launcher's Mods
# page, the .snesmod loader, the trusted-plugin registry and netplay's mod-set
# gate all hang off this one switch; a build without it cannot exchange mods
# with a peer that has them, and the framework says so at every lobby init.
# Must precede the include: runner.cmake reads it to add the loader sources.
set(SNESRECOMP_ENABLE_MODS ON CACHE BOOL
    "Mod package loader + trusted plugins (required for every title)" FORCE)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

add_executable(MegaMan7SNESRecomp
    ${SNESRECOMP_RUNNER_SOURCES}
    src/main.c
    src/host_contract.c
    src/game_rtl.c
    src/gen_stubs.c
    # Widescreen: presentation geometry + per-frame scene policy, and the
    # trusted activation plugin for the preloaded mod package. See
    # docs/WIDESCREEN_PLAN.md and src/dc_widescreen.c.
    src/dc_display.c
    src/dc_widescreen.c
    src/mods/dc_widescreen_plugin.c
    # In-game Options menu (F12): RetroArch shader presets + 120 Hz motion.
    src/dc_options.c
    # librashader (loaded at runtime, never linked): libretro slang presets on
    # the OpenGL presenter and on SDL's Vulkan renderer.
    src/dc_libra.c
    src/ui/rpanel.c
    src/dc_interp.c
    src/dc_default_config.c
    # In-game Check for Updates (GitHub latest release).
    src/updater/recomp_updater.c
)

# ROM identity. rom_identity.txt is the one place the digests live: the build
# turns it into snesrecomp_rom_identity.h (SNESRECOMP_ROM_EXPECTED_SHA256 and
# friends) for src/main.c, and tools/regen.sh, scripts/package_release.sh and
# the release workflow read the same file. A revision bump is one edit there.
target_include_directories(MegaMan7SNESRecomp PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/librashader/include
    ${CMAKE_SOURCE_DIR}/vendor/vulkan-headers/include)
target_link_libraries(MegaMan7SNESRecomp PRIVATE ${CMAKE_DL_LIBS})

snesrecomp_rom_identity(MegaMan7SNESRecomp ${CMAKE_SOURCE_DIR}/rom_identity.txt)

# Generated recompiler output. Not committed: it is derived from a copyrighted
# ROM, so every developer regenerates it locally from their own copy -- and a
# release does not ship it either. With -DSNESRECOMP_SETUP_HOST=ON and an empty
# src/gen/, this builds the SETUP HOST instead: the same executable minus the
# recompiled code, whose only path is the launcher's Generate & rebuild wizard.
# That is what release zips contain; the player's machine produces the
# playable binary from their own ROM. Without the option an empty src/gen/ is
# still a hard error (the framework owns the message).
snesrecomp_target_generated_code(MegaMan7SNESRecomp ${CMAKE_SOURCE_DIR}/src/gen)

# The playable Windows build opens without a console window (GUI subsystem);
# src/main.c sends logs to MegaMan7.log beside the exe, or to the calling
# terminal / a new console with --console. The setup host stays a console app
# so its generate/build child processes share one console.
if(WIN32 AND NOT SNESRECOMP_SETUP_HOST)
    set_target_properties(MegaMan7SNESRecomp PROPERTIES WIN32_EXECUTABLE TRUE)
endif()


# The mod catalog. This names WHERE the packages are; the framework owns the
# layout they are staged into beside the executable, and stages them on every
# build OF THE EXECUTABLE -- including the launcher's own Generate & rebuild on
# a player's machine, which builds only this target and would skip a sibling
# ALL target. mods/preloaded/packages/ holds reviewed, default-disabled
# packages; players install .snesmod archives through the launcher.
# snesrecomp/docs/MOD_PACKAGES.md.
#
# It used to be `snesrecomp_target_stage_dir(... ${CMAKE_SOURCE_DIR}/mods mods)`
# -- the destination spelled per title, which is how three ports in the same
# workspace ended up shipping an empty Mods page and nothing said so. Declaring
# it is now also enforced: a project with packages that never calls this fails
# to configure.
snesrecomp_target_mod_catalog(MegaMan7SNESRecomp ${CMAKE_SOURCE_DIR}/mods/preloaded)
# RetroArch GLSL presets for the in-game Options menu (shaders-120hz fork).
# Not tracked: scripts/fetch_shaders.sh stages libretro/glsl-shaders into
# <repo>/shaders, and a setup pack ships that folder at its root, so the
# player's rebuild puts it beside the playable exe too.
if(EXISTS ${CMAKE_SOURCE_DIR}/shaders/presets OR EXISTS ${CMAKE_SOURCE_DIR}/shaders/slang)
    snesrecomp_target_stage_dir(MegaMan7SNESRecomp ${CMAKE_SOURCE_DIR}/shaders shaders)
endif()
# Windows app icon (original SNES front box art, letterboxed). The .rc is
# part of this target so both the shipped setup-host exe and the player's
# first-run Generate & rebuild embed it. icon.png / icon.bmp are staged
# beside the exe for the SDL window icon at runtime.
if(WIN32)
    enable_language(RC)
    target_sources(MegaMan7SNESRecomp PRIVATE src/win32/app.rc)
endif()
add_custom_command(TARGET MegaMan7SNESRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/icon/icon.png"
        "$<TARGET_FILE_DIR:MegaMan7SNESRecomp>/icon.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/icon/icon.bmp"
        "$<TARGET_FILE_DIR:MegaMan7SNESRecomp>/icon.bmp"
    COMMENT "MegaMan7SNESRecomp: staging window icon beside the executable"
    VERBATIM)
# Folder form so scripts/package_release.sh can --runtime-dir icon into the zip.
snesrecomp_target_stage_dir(MegaMan7SNESRecomp
    ${CMAKE_SOURCE_DIR}/assets/icon icon)
# Any other directory the game reads exe-relative (a translations/ table, ...)
# is staged with snesrecomp_target_stage_dir, never with a separate ALL target.

target_include_directories(MegaMan7SNESRecomp PRIVATE
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
    # common_rtl.c includes "config.h": the desktop key-map / Config contract
    # the framework host supplies (snesrecomp_target_desktop_host below).
    ${SNESRECOMP_ROOT}/runner/src/desktop
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/updater
    ${CMAKE_SOURCE_DIR}/recomp
)
target_link_libraries(MegaMan7SNESRecomp PRIVATE ${SNESRECOMP_RUNNER_LIBRARIES})
if(WIN32)
    target_link_libraries(MegaMan7SNESRecomp PRIVATE winhttp shell32)
endif()
target_compile_definitions(MegaMan7SNESRecomp PRIVATE
    SNES_GAME_VERSION="${SNESRECOMP_BUILD_VERSION}"
    SNES_GAME_TITLE="Mega Man 7"
    RECOMP_UPDATER_REPO_OWNER="rac08241988"
    RECOMP_UPDATER_REPO_NAME="MegaMan7-Recomp"
)
snesrecomp_target_sdl(MegaMan7SNESRecomp)

# The desktop host: launcher flow, ROM resolution, config.ini + [KeyMap],
# window + SDL/OpenGL presenters, audio, gamepads, the save-state browser and
# rewind filmstrip, the pacing clock, crash handlers and the post-mortem
# report. src/main.c is the shim that names this title and supplies its
# hooks; the host is the framework's and improves on a submodule pull.
# TIER2: post-mortem reports carry the interpreter-gap manifest; the coverage
# capture loop and the promotion investigation are documented in
# docs/PORTING_NOTES.md. Promotion of captured roots is held until the
# scheduler HLE exists (see that document).
snesrecomp_target_desktop_host(MegaMan7SNESRecomp TIER2)
# rom.cfg (the ROM the wizard set this project up from; git-ignored) is
# staged beside the executable when none is there yet, so the first launch
# opens on that dump rather than asking for one.
snesrecomp_target_rom_cache(MegaMan7SNESRecomp)

# Shared Dear ImGui pre-boot launcher (ROM picker, verification, display /
# audio / input settings). Self-contained: it brings its own ImGui, GL link and
# staged assets, and defines RECOMP_LAUNCHER so src/main.c compiles its GUI path.
set(RECOMP_UI_ROOT "${CMAKE_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Root directory of the recomp-ui launcher repo" FORCE)
if(NOT EXISTS "${RECOMP_UI_ROOT}/recomp_ui.cmake")
    message(FATAL_ERROR
        "recomp-ui missing at ${RECOMP_UI_ROOT}.\n"
        "Run: git submodule update --init --recursive recomp-ui")
endif()
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
# BOXART stages launcher_assets/img/boxart.tga beside the exe; the launcher
# shows assets/img/boxart.tga by default. EXISTS-guarded in recomp_ui.cmake,
# so this configures cleanly before any art is fetched.
recomp_target_launcher_ui(MegaMan7SNESRecomp CONSOLE snes
    BOXART "${CMAKE_SOURCE_DIR}/launcher_assets/img/boxart.tga")

# Generate & rebuild from inside the launcher: pick ROM -> recompile locally
# -> cmake --build -> relaunch. The implementation is the framework's; this
# project contributes no configuration (snesrecomp_codegen_host_autowire in
# src/main.c). recomp-ui/src is on the include path because the host speaks
# RecompLauncherCGameInfo.
target_sources(MegaMan7SNESRecomp PRIVATE
    ${SNESRECOMP_ROOT}/host/snesrecomp_codegen_host.c)
target_include_directories(MegaMan7SNESRecomp PRIVATE
    ${SNESRECOMP_ROOT}/host
    ${RECOMP_UI_ROOT}/src)

# Netplay is not built into this target.
# No multitap: two seats, one controller per port.
