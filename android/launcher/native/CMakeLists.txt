# libbanana.so: banana-ui's core (ui/) for the launcher app. The same sources as ui/Makefile's banana-ui, with the
# Android backends in place of the Linux ones:
#   kms.cpp, kms_sdl.cpp  ->  android_display.cpp   (the activities' surfaces instead of the panels)
#   input.cpp             ->  android_input.cpp     (Android's keys and touches instead of evdev)
# and the bridge to the app (android_jni.cpp, android_launch.cpp, android_platform.cpp, android_tools.cpp).
# main.cpp's main() is banana_main() here: the app runs it on a thread of its own.
cmake_minimum_required(VERSION 3.22)
project(banana CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT BANANA_REPO)
	get_filename_component(BANANA_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
endif()
set(UI "${BANANA_REPO}/ui")

# FreeType (the menu's text): its source given (-DBANANA_FREETYPE_DIR, an offline build), else downloaded once
include(FetchContent)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if (BANANA_FREETYPE_DIR)
	FetchContent_Declare(freetype SOURCE_DIR "${BANANA_FREETYPE_DIR}")
else()
	FetchContent_Declare(freetype
		URL https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
		URL_HASH SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289)
endif()
FetchContent_MakeAvailable(freetype)

set(CORE_SRC
	src/main.cpp src/recorder.cpp src/lang.cpp src/soft.cpp src/panelfit.cpp src/colors.cpp src/library.cpp src/sound.cpp
	src/splash.cpp src/arrival.cpp src/settings.cpp src/speaker_eq.cpp src/settings_pal.cpp src/settings_defaults.cpp
	src/wifi.cpp src/bluetooth.cpp src/zip.cpp src/zipdir.cpp src/carts.cpp src/cart3d.cpp src/theme.cpp src/pokemon.cpp
	src/pokemap.cpp src/gamecomp.cpp src/vkbd.cpp src/json.cpp src/expr.cpp src/layout.cpp src/fileops.cpp
	src/settings_files.cpp src/complink.cpp src/bcomp.c rnds/RndsRaster.cpp rnds/RndsUI.cpp src/settings_gamepanel.cpp
	src/settings_quick.cpp src/settings_pc.cpp src/settings_v3.cpp
	src/bovl.c src/nds/overlay.cpp src/nds/pages.cpp src/nds/model.cpp src/nds/nitro.cpp src/nds/rom.cpp
	src/nds/townmap.cpp src/nds/guide.cpp src/nds/ram.cpp src/nds/logic.cpp src/nds/session.cpp src/nds/romfile.cpp
	src/gba/gen3live.cpp src/gba/host.cpp src/gba/panel.cpp src/gba/gen3rom.cpp src/gba/hacks.cpp src/gba/gen3map.cpp
	src/gba/expansion.cpp
	src/settings_bshare.cpp
	src/settings_servers.cpp src/nas.cpp src/sysfolders.cpp src/fetch/names.cpp src/fetch/jobfile.cpp src/fetch/servers.cpp
	# (Android's backends and the bridge)
	src/android_display.cpp src/android_input.cpp src/android_jni.cpp src/android_launch.cpp src/android_platform.cpp
	src/android_tools.cpp)
list(TRANSFORM CORE_SRC PREPEND "${UI}/")

add_library(banana SHARED ${CORE_SRC})
target_include_directories(banana PRIVATE "${UI}/src")
# (no FORTIFY: its inline open() wouldn't reach the paths' wrappers below)
target_compile_options(banana PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Wall -Wno-unused-function -Wno-unused-variable
	-Wno-unused-but-set-variable -Wno-sign-compare -Wno-deprecated-declarations -Wno-missing-braces -Wno-unused-private-field
	-Wno-unused-lambda-capture -Wno-format-truncation)
set_source_files_properties("${UI}/src/main.cpp" PROPERTIES COMPILE_DEFINITIONS "main=banana_main")
target_link_libraries(banana PRIVATE freetype android log z)
# the core's Linux paths and shell tools taken where they leave it (ui/src/android_tools.cpp)
set(WRAP fopen fclose open opendir access stat lstat unlink rename mkdir rmdir mkfifo chmod truncate inotify_add_watch
	utimensat realpath popen pclose system execv execl)
foreach(f ${WRAP})
	target_link_options(banana PRIVATE "-Wl,--wrap=${f}")
endforeach()
target_link_options(banana PRIVATE "-Wl,--no-undefined" "-Wl,-z,max-page-size=16384")
