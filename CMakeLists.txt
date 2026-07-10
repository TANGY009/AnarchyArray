cmake_minimum_required(VERSION 3.18)
project(AnarchyArray LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INSIDE_CODE ON)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

add_compile_options(
    -O2
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
    -fexceptions
    -w
)

add_link_options(
    -Wl,--gc-sections
    -Wl,--strip-all
    -s
)

include(FetchContent)

FetchContent_Declare(
    preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG main
)
FetchContent_MakeAvailable(preloader_android)

set(IMGUI_SOURCES
    src/main.cpp
    src/ImGui/imgui.cpp
    src/ImGui/imgui_draw.cpp
    src/ImGui/imgui_tables.cpp
    src/ImGui/imgui_widgets.cpp
    src/ImGui/backends/imgui_impl_opengl3.cpp
    src/ImGui/backends/imgui_impl_android.cpp
)

add_library(AnarchyArray SHARED ${IMGUI_SOURCES})

target_include_directories(AnarchyArray PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/ImGui
    ${CMAKE_SOURCE_DIR}/src/ImGui/backends
    ${preloader_android_SOURCE_DIR}/src
    ${preloader_android_SOURCE_DIR}/include
)

target_link_libraries(AnarchyArray
    PRIVATE
    preloader
    log
    android
    EGL
    GLESv2
    GLESv3
)