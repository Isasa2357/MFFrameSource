cmake_minimum_required(VERSION 3.20)

project(MFFrameSource VERSION 1.1.0 LANGUAGES CXX)

option(MFFRAMESOURCE_BUILD_SAMPLE "Build MFFrameSource samples" ON)
option(MFFRAMESOURCE_BUILD_TESTS "Build MFFrameSource tests" ON)
option(MFFRAMESOURCE_BUILD_D3D11 "Build MFFrameSource D3D11 backend" ON)
option(MFFRAMESOURCE_BUILD_D3D12 "Build MFFrameSource D3D12 backend" ON)
option(MFFRAMESOURCE_INSTALL "Enable MFFrameSource install and CMake package export rules" OFF)

option(MFFRAMESOURCE_FETCH_D3D11HELPER "Fetch D3D11Helper with FetchContent when the target is not already available" ON)
option(MFFRAMESOURCE_FETCH_D3D12HELPER "Fetch D3D12Helper with FetchContent when the target is not already available" ON)
option(MFFRAMESOURCE_FETCH_THREADKIT "Fetch ThreadKit with FetchContent when the target is not already available" ON)
set(D3D11HELPER_ROOT "" CACHE PATH "Optional local D3D11Helper repository root. Usually leave empty; FetchContent is used by default.")
set(D3D12HELPER_ROOT "" CACHE PATH "Optional local D3D12Helper repository root. Usually leave empty; FetchContent is used by default.")
set(THREADKIT_ROOT "" CACHE PATH "Optional local ThreadKit repository root. Usually leave empty; FetchContent is used by default.")
set(MFFRAMESOURCE_D3D11HELPER_GIT_REPOSITORY "https://github.com/Isasa2357/D3D11Helper.git" CACHE STRING "D3D11Helper Git repository")
set(MFFRAMESOURCE_D3D12HELPER_GIT_REPOSITORY "https://github.com/Isasa2357/D3D12Helper.git" CACHE STRING "D3D12Helper Git repository")
# Pin to the newest confirmed release tags at the time this branch was updated.
# Do not leave these on main; D3DHelper repositories may continue to change independently.
set(MFFRAMESOURCE_D3D11HELPER_GIT_TAG "v1.12.0" CACHE STRING "D3D11Helper Git ref/tag/commit to fetch")
set(MFFRAMESOURCE_D3D12HELPER_GIT_TAG "v1.12.0" CACHE STRING "D3D12Helper Git ref/tag/commit to fetch")
set(MFFRAMESOURCE_THREADKIT_GIT_REPOSITORY "https://github.com/Isasa2357/ThreadKit.git" CACHE STRING "ThreadKit Git repository")
set(MFFRAMESOURCE_THREADKIT_GIT_TAG "main" CACHE STRING "ThreadKit Git ref/tag/commit to fetch")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "MFFrameSource requires Windows.")
endif()

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(MFFRAMESOURCE_D3D11HELPER_SOURCE_DIR "")
set(MFFRAMESOURCE_D3D12HELPER_SOURCE_DIR "")

if(MFFRAMESOURCE_BUILD_D3D11 AND NOT TARGET D3D11Helper::D3D11Helper)
    if(D3D11HELPER_ROOT AND EXISTS "${D3D11HELPER_ROOT}/CMakeLists.txt")
        message(STATUS "Using local D3D11Helper from D3D11HELPER_ROOT=${D3D11HELPER_ROOT}")
        set(D3D11HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
        set(D3D11HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${D3D11HELPER_ROOT}" "${CMAKE_BINARY_DIR}/_deps/D3D11Helper-build")
        set(MFFRAMESOURCE_D3D11HELPER_SOURCE_DIR "${D3D11HELPER_ROOT}")
    elseif(MFFRAMESOURCE_FETCH_D3D11HELPER)
        message(STATUS "Fetching D3D11Helper ${MFFRAMESOURCE_D3D11HELPER_GIT_TAG}")
        set(D3D11HELPER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
        set(D3D11HELPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(D3D11HELPER_ENABLE_WARNINGS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            D3D11Helper
            GIT_REPOSITORY "${MFFRAMESOURCE_D3D11HELPER_GIT_REPOSITORY}"
            GIT_TAG        "${MFFRAMESOURCE_D3D11HELPER_GIT_TAG}"
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(D3D11Helper)
        FetchContent_GetProperties(D3D11Helper)
        set(MFFRAMESOURCE_D3D11HELPER_SOURCE_DIR "${d3d11helper_SOURCE_DIR}")
    else()
        message(FATAL_ERROR
            "D3D11Helper::D3D11Helper is not available. "
            "Enable MFFRAMESOURCE_FETCH_D3D11HELPER, set D3D11HELPER_ROOT, "
            "or add D3D11Helper before MFFrameSource.")
    endif()
elseif(TARGET D3D11Helper::D3D11Helper)
    message(STATUS "Using existing D3D11Helper::D3D11Helper target")
endif()

if(MFFRAMESOURCE_BUILD_D3D12 AND NOT TARGET D3D12Helper::D3D12Helper)
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
elseif(TARGET D3D12Helper::D3D12Helper)
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

set(MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR
    "${MFFRAMESOURCE_D3D11HELPER_SOURCE_DIR}/shaders/D3D11Processing"
    CACHE PATH "D3D11Helper Processing shader directory used by samples/tests")
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

set(MFFRAMESOURCE_COMMON_SOURCES
    src/MFCommon.cpp
    src/MFPlatformContext.cpp
    src/MFCameraEnumerator.cpp
    src/internal/MFCpuSampleReader.cpp
    src/internal/MFVideoFileSampleReader.cpp
)

function(mfframesource_configure_backend_target target_name)
    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(${target_name}
        PUBLIC
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /utf-8 /EHsc /W4 /permissive-)
    endif()
    target_link_libraries(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:ThreadKit::ThreadKit>
            $<INSTALL_INTERFACE:ThreadKit::ThreadKit>
            mf
            mfplat
            mfreadwrite
            mfuuid
            ole32
    )
endfunction()

set(MFFRAMESOURCE_INSTALL_TARGETS)

if(MFFRAMESOURCE_BUILD_D3D11)
    add_library(MFFrameSourceD3D11 STATIC
        ${MFFRAMESOURCE_COMMON_SOURCES}
        src/MFD3D11CameraCapture.cpp
        src/MFD3D11CameraCaptureThread.cpp
        src/MFD3D11CameraSyncThread.cpp
        src/MFD3D11VideoCapture.cpp
        src/MFD3D11VideoCaptureThread.cpp
        src/internal/MFD3D11UploadFramePool.cpp
        src/internal/MFD3D11FrameCloner.cpp
    )
    add_library(MFFrameSource::D3D11 ALIAS MFFrameSourceD3D11)
    set_target_properties(MFFrameSourceD3D11 PROPERTIES EXPORT_NAME D3D11)
    mfframesource_configure_backend_target(MFFrameSourceD3D11)
    target_link_libraries(MFFrameSourceD3D11
        PUBLIC
            $<BUILD_INTERFACE:D3D11Helper::D3D11Helper>
            $<INSTALL_INTERFACE:D3D11Helper::D3D11Helper>
    )
    list(APPEND MFFRAMESOURCE_INSTALL_TARGETS MFFrameSourceD3D11)
endif()

if(MFFRAMESOURCE_BUILD_D3D12)
    add_library(MFFrameSourceD3D12 STATIC
        ${MFFRAMESOURCE_COMMON_SOURCES}
        src/MFD3D12CameraCapture.cpp
        src/MFD3D12CameraCaptureThread.cpp
        src/MFD3D12CameraSyncThread.cpp
        src/MFD3D12VideoCapture.cpp
        src/MFD3D12VideoCaptureThread.cpp
        src/internal/MFD3D12UploadFramePool.cpp
        src/internal/MFD3D12FrameCloner.cpp
    )
    add_library(MFFrameSource::D3D12 ALIAS MFFrameSourceD3D12)
    set_target_properties(MFFrameSourceD3D12 PROPERTIES EXPORT_NAME D3D12)
    mfframesource_configure_backend_target(MFFrameSourceD3D12)
    target_link_libraries(MFFrameSourceD3D12
        PUBLIC
            $<BUILD_INTERFACE:D3D12Helper::D3D12Helper>
            $<INSTALL_INTERFACE:D3D12Helper::D3D12Helper>
    )
    list(APPEND MFFRAMESOURCE_INSTALL_TARGETS MFFrameSourceD3D12)
endif()

# -----------------------------------------------------------------------------
# Install / package support
# -----------------------------------------------------------------------------
if(MFFRAMESOURCE_INSTALL)
    if(MFFRAMESOURCE_INSTALL_TARGETS)
        install(TARGETS ${MFFRAMESOURCE_INSTALL_TARGETS}
            EXPORT MFFrameSourceTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()

    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    if(MFFRAMESOURCE_BUILD_D3D11 AND EXISTS "${MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR}")
        install(DIRECTORY "${MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR}/"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/MFFrameSource/shaders/D3D11Processing"
        )
    endif()
    if(MFFRAMESOURCE_BUILD_D3D12 AND EXISTS "${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}")
        install(DIRECTORY "${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}/"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/MFFrameSource/shaders/D3D12Processing"
        )
    endif()

    install(EXPORT MFFrameSourceTargets
        NAMESPACE MFFrameSource::
        FILE MFFrameSourceTargets.cmake
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MFFrameSource"
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MFFrameSourceConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/MFFrameSourceConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MFFrameSource"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/MFFrameSourceConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/MFFrameSourceConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/MFFrameSourceConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MFFrameSource"
    )

    if(D3D12HELPER_DXCOMPILER_DLL AND EXISTS "${D3D12HELPER_DXCOMPILER_DLL}")
        install(FILES "${D3D12HELPER_DXCOMPILER_DLL}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    if(D3D12HELPER_DXIL_DLL AND EXISTS "${D3D12HELPER_DXIL_DLL}")
        install(FILES "${D3D12HELPER_DXIL_DLL}" DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

if(MFFRAMESOURCE_BUILD_SAMPLE AND MFFRAMESOURCE_BUILD_D3D11)
    foreach(sample_name IN ITEMS
        d3d11_camera_read
        d3d11_camera_thread_read
        d3d11_stereo_sync
        d3d11_video_read
        d3d11_video_thread_read)
        add_executable(${sample_name} sample/${sample_name}.cpp)
        target_link_libraries(${sample_name} PRIVATE MFFrameSource::D3D11)
        if(MSVC)
            target_compile_options(${sample_name} PRIVATE /utf-8 /EHsc /W4 /permissive-)
        endif()
    endforeach()
endif()

if(MFFRAMESOURCE_BUILD_SAMPLE AND MFFRAMESOURCE_BUILD_D3D12)
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
        if(MSVC)
            target_compile_options(${sample_name} PRIVATE /utf-8 /EHsc /W4 /permissive-)
        endif()
        mfframesource_copy_dxc_runtime(${sample_name})
    endforeach()
endif()

if(MFFRAMESOURCE_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()
