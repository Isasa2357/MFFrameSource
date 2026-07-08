cmake_minimum_required(VERSION 3.20)

project(MFFrameSourceD3D12 VERSION 1.0.1 LANGUAGES CXX)

option(MFFRAMESOURCE_BUILD_SAMPLE "Build MFFrameSource samples" ON)
option(MFFRAMESOURCE_BUILD_TESTS "Build MFFrameSource tests" ON)

option(MFFRAMESOURCE_FETCH_D3D12HELPER "Fetch D3D12Helper with FetchContent when the target is not already available" ON)
option(MFFRAMESOURCE_FETCH_THREADKIT "Fetch ThreadKit with FetchContent when the target is not already available" ON)
set(D3D12HELPER_ROOT "" CACHE PATH "Optional local D3D12Helper repository root. Usually leave empty; FetchContent is used by default.")
set(THREADKIT_ROOT "" CACHE PATH "Optional local ThreadKit repository root. Usually leave empty; FetchContent is used by default.")
set(MFFRAMESOURCE_D3D12HELPER_GIT_REPOSITORY "https://github.com/Isasa2357/D3D12Helper.git" CACHE STRING "D3D12Helper Git repository")
# Pin to the newest confirmed D3D12Helper release tag at the time this branch was created.
# Do not leave this on main; D3D12Helper may continue to change independently.
set(MFFRAMESOURCE_D3D12HELPER_GIT_TAG "v1.11.0" CACHE STRING "D3D12Helper Git ref/tag/commit to fetch")
set(MFFRAMESOURCE_THREADKIT_GIT_REPOSITORY "https://github.com/Isasa2357/ThreadKit.git" CACHE STRING "ThreadKit Git repository")
set(MFFRAMESOURCE_THREADKIT_GIT_TAG "main" CACHE STRING "ThreadKit Git ref/tag/commit to fetch")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "MFFrameSource D3D12 backend requires Windows.")
endif()

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(MFFRAMESOURCE_D3D12HELPER_SOURCE_DIR "")

if(NOT TARGET D3D12Helper::D3D12Helper)
    if(D3D12HELPER_ROOT AND EXISTS "${D3D12HELPER_ROOT}/CMakeLists.txt")
        message(STATUS "Using local D3D12Helper from D3D12HELPER_ROOT=${D3D12HELPER_ROOT}")
        set(D3D12HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
        set(D3D12HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${D3D12HELPER_ROOT}" "${CMAKE_BINARY_DIR}/_deps/D3D12Helper-build")
        set(MFFRAMESOURCE_D3D12HELPER_SOURCE_DIR "${D3D12HELPER_ROOT}")
    elseif(MFFRAMESOURCE_FETCH_D3D12HELPER)
        message(STATUS "Fetching D3D12Helper ${MFFRAMESOURCE_D3D12HELPER_GIT_TAG}")
        set(D3D12HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
        set(D3D12HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(D3D12HELPER_ENABLE_WARNINGS OFF CACHE BOOL "" FORCE)
        set(D3D12HELPER_COPY_DXC_RUNTIME ON CACHE BOOL "" FORCE)

        FetchContent_Declare(
            D3D12Helper
            GIT_REPOSITORY "${MFFRAMESOURCE_D3D12HELPER_GIT_REPOSITORY}"
            GIT_TAG        "${MFFRAMESOURCE_D3D12HELPER_GIT_TAG}"
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(D3D12Helper)
        FetchContent_GetProperties(D3D12Helper)
        set(MFFRAMESOURCE_D3D12HELPER_SOURCE_DIR "${d3d12helper_SOURCE_DIR}")
    else()
        message(FATAL_ERROR
            "D3D12Helper::D3D12Helper is not available. "
            "Enable MFFRAMESOURCE_FETCH_D3D12HELPER, set D3D12HELPER_ROOT, "
            "or add D3D12Helper before MFFrameSource.")
    endif()
else()
    message(STATUS "Using existing D3D12Helper::D3D12Helper target")
endif()

if(NOT TARGET ThreadKit::ThreadKit)
    if(THREADKIT_ROOT AND EXISTS "${THREADKIT_ROOT}/CMakeLists.txt")
        message(STATUS "Using local ThreadKit from THREADKIT_ROOT=${THREADKIT_ROOT}")
        set(THREADKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(THREADKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${THREADKIT_ROOT}" "${CMAKE_BINARY_DIR}/_deps/ThreadKit-build")
    elseif(MFFRAMESOURCE_FETCH_THREADKIT)
        set(THREADKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(THREADKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            ThreadKit
            GIT_REPOSITORY "${MFFRAMESOURCE_THREADKIT_GIT_REPOSITORY}"
            GIT_TAG        "${MFFRAMESOURCE_THREADKIT_GIT_TAG}"
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(ThreadKit)
    else()
        message(FATAL_ERROR
            "ThreadKit::ThreadKit is not available. "
            "Enable MFFRAMESOURCE_FETCH_THREADKIT, set THREADKIT_ROOT, "
            "or add ThreadKit before MFFrameSource.")
    endif()
endif()

set(MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR
    "${MFFRAMESOURCE_D3D12HELPER_SOURCE_DIR}/shaders/D3D12Processing"
    CACHE PATH "D3D12Helper Processing shader directory used by samples/tests")

function(mfframesource_copy_dxc_runtime target_name)
    if(COMMAND d3d12helper_copy_dxc_runtime)
        d3d12helper_copy_dxc_runtime(${target_name})
    else()
        message(WARNING
            "d3d12helper_copy_dxc_runtime() is not available. "
            "If ${target_name} exits with 0xc0000135, copy dxcompiler.dll and dxil.dll "
            "next to the executable or set D3D12HELPER_DXC_RUNTIME_DIR.")
    endif()
endfunction()

if(COMMAND d3d12helper_find_dxc_runtime)
    d3d12helper_find_dxc_runtime()
endif()

add_library(MFFrameSourceD3D12 STATIC
    src/MFCommon.cpp
    src/MFPlatformContext.cpp
    src/MFCameraEnumerator.cpp
    src/MFD3D12CameraCapture.cpp
    src/MFD3D12CameraCaptureThread.cpp
    src/MFD3D12CameraSyncThread.cpp
    src/MFD3D12VideoCapture.cpp
    src/MFD3D12VideoCaptureThread.cpp
    src/internal/MFCpuSampleReader.cpp
    src/internal/MFVideoFileSampleReader.cpp
    src/internal/MFD3D12UploadFramePool.cpp
    src/internal/MFD3D12FrameCloner.cpp
)

add_library(MFFrameSource::D3D12 ALIAS MFFrameSourceD3D12)
set_target_properties(MFFrameSourceD3D12 PROPERTIES EXPORT_NAME D3D12)

target_include_directories(MFFrameSourceD3D12
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(MFFrameSourceD3D12
    PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

if(MSVC)
    target_compile_options(MFFrameSourceD3D12 PRIVATE /utf-8 /EHsc /W4 /permissive-)
endif()

target_link_libraries(MFFrameSourceD3D12
    PUBLIC
        $<BUILD_INTERFACE:D3D12Helper::D3D12Helper>
        $<BUILD_INTERFACE:ThreadKit::ThreadKit>
        $<INSTALL_INTERFACE:D3D12Helper::D3D12Helper>
        $<INSTALL_INTERFACE:ThreadKit::ThreadKit>
        mf
        mfplat
        mfreadwrite
        mfuuid
        ole32
)



# -----------------------------------------------------------------------------
# Install / package support
# -----------------------------------------------------------------------------
install(TARGETS MFFrameSourceD3D12
    EXPORT MFFrameSourceD3D12Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(EXISTS "${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}")
    install(DIRECTORY "${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/MFFrameSourceD3D12/shaders/D3D12Processing"
    )
endif()

install(EXPORT MFFrameSourceD3D12Targets
    NAMESPACE MFFrameSource::
    FILE MFFrameSourceD3D12Targets.cmake
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MFFrameSourceD3D12"
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MFFrameSourceD3D12Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/MFFrameSourceD3D12Config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MFFrameSourceD3D12"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/MFFrameSourceD3D12ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/MFFrameSourceD3D12Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/MFFrameSourceD3D12ConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MFFrameSourceD3D12"
)

if(D3D12HELPER_DXCOMPILER_DLL AND EXISTS "${D3D12HELPER_DXCOMPILER_DLL}")
    install(FILES "${D3D12HELPER_DXCOMPILER_DLL}" DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(D3D12HELPER_DXIL_DLL AND EXISTS "${D3D12HELPER_DXIL_DLL}")
    install(FILES "${D3D12HELPER_DXIL_DLL}" DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(MFFRAMESOURCE_BUILD_SAMPLE)
    foreach(sample_name IN ITEMS
        d3d12_camera_read
        d3d12_camera_thread_read
        d3d12_camera_benchmark
        d3d12_stereo_sync
        d3d12_video_read
        d3d12_video_thread_read
        d3d12_video_benchmark)
        add_executable(${sample_name} sample/${sample_name}.cpp)
        target_link_libraries(${sample_name} PRIVATE MFFrameSource::D3D12)
        mfframesource_copy_dxc_runtime(${sample_name})
    endforeach()
endif()

if(MFFRAMESOURCE_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()
