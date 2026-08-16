cmake_minimum_required(VERSION 3.20)
project(arrayplot LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "arrayplot_viewer uses Win32 + DirectX11 and only builds on Windows")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master
)
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG master
)
FetchContent_MakeAvailable(imgui implot)

# Neither imgui nor implot ship a top-level CMakeLists.txt, so FetchContent
# only populates the sources -- build them as one static lib ourselves.
add_library(imgui_implot STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(imgui_implot PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
)
target_link_libraries(imgui_implot PUBLIC d3d11 dxgi d3dcompiler)

# ---- arrayplot client (header-only) ----
add_library(arrayplot_client INTERFACE)
target_include_directories(arrayplot_client INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

# ---- viewer executable ----
add_executable(arrayplot_viewer
    viewer/main.cpp
)
target_link_libraries(arrayplot_viewer PRIVATE imgui_implot)
target_include_directories(arrayplot_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)

# ---- example producer (optional demo) ----
option(ARRAYPLOT_BUILD_EXAMPLE "Build the example producer app" ON)
if(ARRAYPLOT_BUILD_EXAMPLE)
    add_executable(arrayplot_example examples/example.cpp)
    target_link_libraries(arrayplot_example PRIVATE arrayplot_client)

    find_package(Eigen3 CONFIG QUIET)
    if(Eigen3_FOUND)
        target_link_libraries(arrayplot_example PRIVATE Eigen3::Eigen)
        target_compile_definitions(arrayplot_example PRIVATE ARRAYPLOT_HAVE_EIGEN)

        add_executable(arrayplot_example_eigen examples/example_eigen.cpp)
        target_link_libraries(arrayplot_example_eigen PRIVATE arrayplot_client Eigen3::Eigen)
    else()
        message(STATUS "arrayplot: Eigen3 not found via find_package -- example and example_eigen will skip "
                        "the Eigen demo. Install it with 'vcpkg install eigen3' (and pass "
                        "-DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake), or point CMAKE_PREFIX_PATH at an existing "
                        "Eigen install.")
    endif()
endif()
