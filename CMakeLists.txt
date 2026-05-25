cmake_minimum_required(VERSION 3.21)

project(learn_qt)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# set(CMAKE_AR "/opt/homebrew/opt/llvm/bin/llvm-ar" CACHE FILEPATH "Path to ar" FORCE)
# set(CMAKE_RANLIB "/opt/homebrew/opt/llvm/bin/llvm-ranlib" CACHE FILEPATH "Path to ranlib" FORCE)

if (CMAKE_BUILD_TYPE STREQUAL "Coverage")
    # llvm coverage test
    message(STATUS "Enabling coverage instrumentation")
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)

elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Compiling Release Version")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DREDIRECT_INPUT")

elseif (CMAKE_BUILD_TYPE STREQUAL "Sanitize")
    message(STATUS "Enabling sanitize address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSANITIZE")
    add_compile_options(-fsanitize=address -g -fno-omit-frame-pointer -O0)
    add_link_options(-fsanitize=address)
    # link_directories(/opt/homebrew/opt/llvm/lib/clang/20/lib/darwin)
endif ()

include(FetchContent)

FetchContent_Declare(toy_concurrency
    GIT_REPOSITORY git@github.com:lym01803/toy_concurrency.git
    GIT_TAG feecd19
)
FetchContent_MakeAvailable(toy_concurrency)

FetchContent_Declare(spdlog
    GIT_REPOSITORY git@github.com:gabime/spdlog.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

# 全局 include 目录·
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/external)

add_library(logger_utils src/utils/logger_utils.cpp)
target_link_libraries(logger_utils PUBLIC spdlog::spdlog)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

message(STATUS "--- Qt6 info ---")
message(STATUS "  Found: ${Qt6_FOUND}")
message(STATUS "  Version: ${Qt6_VERSION}")
message(STATUS "  Path: ${Qt6_DIR}")
message(STATUS "  QtCore Include Path: ${Qt6Core_INCLUDE_DIRS}")

qt_standard_project_setup(REQUIRES 6.11)

add_library(datamodel src/datamodel.cpp)
target_link_libraries(datamodel 
    PUBLIC logger_utils
    PUBLIC toy_concurrency
)

add_library(view src/view.cpp)
target_link_libraries(view 
    PUBLIC logger_utils
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    PUBLIC datamodel
)

qt_add_executable(main WIN32 MACOSX_BUNDLE src/main.cpp)

target_link_libraries(main PRIVATE 
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    logger_utils
    datamodel
    view
    toy_concurrency
)

qt_add_qml_module(main
    URI "learn_qt"
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Dir.qml
    SOURCES
        include/view.h
        src/view.cpp
)

if(APPLE)
    set_target_properties(main PROPERTIES
        OUTPUT_NAME "LearnQt"
        MACOSX_BUNDLE_BUNDLE_NAME "LearnQt"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.lym01803.LearnQt"
    )
    if(IOS)
        set(asset_catalog_path "ios/Assets.xcassets")
        target_sources(main PRIVATE "${asset_catalog_path}")
        set_source_files_properties(${asset_catalog_path} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        set_target_properties(main
            PROPERTIES XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon)
    endif()
endif()
