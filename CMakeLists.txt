cmake_minimum_required(VERSION 3.20)
project(capsule C CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED CONFIG)

set(IMGUI ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_sdl2.cpp ${IMGUI}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI} ${IMGUI}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2-static opengl32)

# RetroAchievements runtime (rcheevos), built from source.
set(RC ${CMAKE_SOURCE_DIR}/third_party/rcheevos)
file(GLOB RC_SOURCES ${RC}/src/*.c ${RC}/src/rapi/*.c ${RC}/src/rcheevos/*.c ${RC}/src/rhash/*.c)
list(FILTER RC_SOURCES EXCLUDE REGEX "rc_client_(external|raintegration)\\.c$") # only for RAIntegration/external clients
add_library(rcheevos STATIC ${RC_SOURCES})
target_include_directories(rcheevos PUBLIC ${RC}/include ${RC}/src ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(rcheevos PUBLIC RC_CLIENT_SUPPORTS_HASH RC_DISABLE_LUA)

add_executable(capsule WIN32 src/main.cpp src/core.cpp src/net.cpp src/ra.cpp installer/app.rc)
target_include_directories(capsule PRIVATE third_party)
target_link_libraries(capsule PRIVATE SDL2::SDL2main imgui rcheevos comdlg32 wininet)
target_link_options(capsule PRIVATE -static)

# Emulation cores (libretro: mGBA for GB/GBC/GBA, melonDS DS for NDS, Azahar for 3DS) ship next to the exe.
add_custom_command(TARGET capsule POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/cores $<TARGET_FILE_DIR:capsule>/cores)

add_executable(selftest tests/selftest.cpp src/core.cpp)
target_include_directories(selftest PRIVATE third_party)
target_compile_options(selftest PRIVATE -UNDEBUG) # asserts must run in Release too
target_link_libraries(selftest PRIVATE SDL2::SDL2-static opengl32)
target_link_options(selftest PRIVATE -static)
