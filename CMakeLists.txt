cmake_minimum_required(VERSION 3.20)

project(CanisPack VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(CANISPACK_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor" CACHE PATH "CanisPack dependency submodule directory")
set(CANISPACK_ENGINE_REPO "${CANISPACK_VENDOR_DIR}/canis" CACHE PATH "Canis engine source checkout")
set(CANISPACK_TEMPLATE_REPOSITORY "git@github.com:EricWRogers/CanisTemplate.git" CACHE STRING "Default Canis project template repository")

set(CANISPACK_ENGINE_EXECUTABLE
    ""
    CACHE FILEPATH
    "Canis engine executable launched by CanisPack")

if(NOT EXISTS "${CANISPACK_ENGINE_REPO}/CMakeLists.txt")
    message(FATAL_ERROR "Missing Canis engine submodule. Run: git submodule update --init vendor/canis vendor/SDL vendor/imgui vendor/yaml-cpp")
endif()

if(NOT EXISTS "${CANISPACK_VENDOR_DIR}/SDL/CMakeLists.txt")
    message(FATAL_ERROR "Missing SDL submodule. Run: git submodule update --init vendor/canis vendor/SDL vendor/imgui vendor/yaml-cpp")
endif()

if(NOT EXISTS "${CANISPACK_VENDOR_DIR}/imgui/imgui.cpp")
    message(FATAL_ERROR "Missing ImGui submodule. Run: git submodule update --init vendor/canis vendor/SDL vendor/imgui vendor/yaml-cpp")
endif()

if(NOT EXISTS "${CANISPACK_VENDOR_DIR}/yaml-cpp/CMakeLists.txt")
    message(FATAL_ERROR "Missing yaml-cpp submodule. Run: git submodule update --init vendor/canis vendor/SDL vendor/imgui vendor/yaml-cpp")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CANISPACK_VENDOR_DIR}/SDL" "${CMAKE_BINARY_DIR}/vendor/SDL" EXCLUDE_FROM_ALL)

set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CANISPACK_VENDOR_DIR}/yaml-cpp" "${CMAKE_BINARY_DIR}/vendor/yaml-cpp" EXCLUDE_FROM_ALL)

set(IMGUI_PATH "${CANISPACK_VENDOR_DIR}/imgui")
add_library(CanisPackImGui STATIC
    "${IMGUI_PATH}/imgui.cpp"
    "${IMGUI_PATH}/imgui_draw.cpp"
    "${IMGUI_PATH}/imgui_tables.cpp"
    "${IMGUI_PATH}/imgui_widgets.cpp"
    "${IMGUI_PATH}/backends/imgui_impl_opengl3.cpp"
    "${IMGUI_PATH}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_PATH}/misc/cpp/imgui_stdlib.cpp"
)
target_include_directories(CanisPackImGui PUBLIC
    "${IMGUI_PATH}"
    "${IMGUI_PATH}/backends"
    "${IMGUI_PATH}/misc/cpp"
)
target_link_libraries(CanisPackImGui PRIVATE SDL3::SDL3)

find_package(OpenGL REQUIRED)

add_executable(CanisPack src/main.cpp)
target_link_libraries(CanisPack PRIVATE
    SDL3::SDL3
    CanisPackImGui
    yaml-cpp
    OpenGL::GL
)

target_compile_definitions(CanisPack PRIVATE
    CANISPACK_VERSION="${PROJECT_VERSION}"
    CANISPACK_ENGINE_EXECUTABLE="${CANISPACK_ENGINE_EXECUTABLE}"
    CANISPACK_TEMPLATE_REPOSITORY="${CANISPACK_TEMPLATE_REPOSITORY}"
)
