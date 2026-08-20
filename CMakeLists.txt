cmake_minimum_required(VERSION 3.20)
project(arrayplot LANGUAGES CXX)

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
# only populates the sources -- build the platform-agnostic core (no
# backends, which differ per platform) as one static lib ourselves.
add_library(imgui_implot STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(imgui_implot PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
)

# ---- arrayplot client (header-only) ----
# find_package(Threads) up front and attach it to the interface target so
# every consumer (examples, and the viewer via its own link below) picks up
# -pthread automatically on Linux, where std::mutex/std::thread need it on
# some toolchains; a no-op on Windows.
find_package(Threads REQUIRED)

add_library(arrayplot_client INTERFACE)
target_include_directories(arrayplot_client INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(arrayplot_client INTERFACE Threads::Threads)

# ---- viewer executable ----
# Same target name and shared plot_store.h on both platforms; only the
# windowing/graphics backend and transport accept loop differ.
if(WIN32)
    set(ARRAYPLOT_VIEWER_SOURCES
        viewer/main_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )
    set(ARRAYPLOT_VIEWER_LIBS d3d11 dxgi d3dcompiler)
else()
    # GLFW ships its own CMakeLists.txt, so FetchContent builds it directly.
    # Building it needs X11 development headers on the machine, e.g. on
    # Debian/Ubuntu: `sudo apt install libx11-dev libxrandr-dev
    # libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`.
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(glfw)

    find_package(OpenGL REQUIRED)

    set(ARRAYPLOT_VIEWER_SOURCES
        viewer/main_linux.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    set(ARRAYPLOT_VIEWER_LIBS glfw OpenGL::GL Threads::Threads)
endif()

add_executable(arrayplot_viewer ${ARRAYPLOT_VIEWER_SOURCES})
target_link_libraries(arrayplot_viewer PRIVATE imgui_implot ${ARRAYPLOT_VIEWER_LIBS})
target_include_directories(arrayplot_viewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/viewer
)

# ---- example producer (optional demo) ----
option(ARRAYPLOT_BUILD_EXAMPLE "Build the example producer app" ON)
if(ARRAYPLOT_BUILD_EXAMPLE)
    add_executable(arrayplot_example examples/example.cpp)
    target_link_libraries(arrayplot_example PRIVATE arrayplot_client)

    # Prefer an existing install (e.g. vcpkg, or a distro package on Linux)
    # since it's faster than fetching; fall back to pulling Eigen's source
    # directly if none is found. Eigen is header-only and its own
    # CMakeLists defines the Eigen3::Eigen target even when just
    # add_subdirectory'd (via FetchContent), so no extra glue is needed
    # either way -- disable its tests/docs so the fetch stays fast.
    find_package(Eigen3 CONFIG QUIET)
    if(NOT Eigen3_FOUND)
        message(STATUS "arrayplot: Eigen3 not found via find_package -- fetching it instead (pinned to 3.4.0).")
        set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
        set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            eigen
            GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
            GIT_TAG 3.4.0
        )
        FetchContent_MakeAvailable(eigen)
    endif()

    target_link_libraries(arrayplot_example PRIVATE Eigen3::Eigen)
    target_compile_definitions(arrayplot_example PRIVATE ARRAYPLOT_HAVE_EIGEN)

    add_executable(arrayplot_example_eigen examples/example_eigen.cpp)
    target_link_libraries(arrayplot_example_eigen PRIVATE arrayplot_client Eigen3::Eigen)
endif()
