function(mfframesource_add_test target backend_target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE ${backend_target})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /EHsc /W4 /permissive-)
    endif()

    if(COMMAND mfframesource_copy_dxc_runtime)
        mfframesource_copy_dxc_runtime(${target})
    endif()

    add_test(NAME ${target} COMMAND ${target})
endfunction()

set(MFFRAMESOURCE_TEST_COMMON_BACKEND "")
if(TARGET MFFrameSource::D3D12)
    set(MFFRAMESOURCE_TEST_COMMON_BACKEND MFFrameSource::D3D12)
elseif(TARGET MFFrameSource::D3D11)
    set(MFFRAMESOURCE_TEST_COMMON_BACKEND MFFrameSource::D3D11)
endif()

if(MFFRAMESOURCE_TEST_COMMON_BACKEND)
    mfframesource_add_test(test_common ${MFFRAMESOURCE_TEST_COMMON_BACKEND}
        test_common.cpp
    )
    set_tests_properties(test_common PROPERTIES LABELS "unit")

    mfframesource_add_test(test_platform_enumeration ${MFFRAMESOURCE_TEST_COMMON_BACKEND}
        test_platform_enumeration.cpp
    )
    set_tests_properties(test_platform_enumeration PROPERTIES LABELS "mf;unit")
endif()

if(TARGET MFFrameSource::D3D12)
    mfframesource_add_test(test_capture_state MFFrameSource::D3D12
        test_capture_state.cpp
    )
    set_tests_properties(test_capture_state PROPERTIES LABELS "unit;d3d12")

    mfframesource_add_test(test_capture_thread_state MFFrameSource::D3D12
        test_capture_thread_state.cpp
    )
    set_tests_properties(test_capture_thread_state PROPERTIES LABELS "unit;thread;d3d12")

    mfframesource_add_test(test_sync_thread_state MFFrameSource::D3D12
        test_sync_thread_state.cpp
    )
    set_tests_properties(test_sync_thread_state PROPERTIES LABELS "unit;thread;sync;d3d12")

    mfframesource_add_test(test_video_capture_state MFFrameSource::D3D12
        test_video_capture_state.cpp
    )
    set_tests_properties(test_video_capture_state PROPERTIES LABELS "unit;video;d3d12")

    mfframesource_add_test(test_video_thread_state MFFrameSource::D3D12
        test_video_thread_state.cpp
    )
    set_tests_properties(test_video_thread_state PROPERTIES LABELS "unit;video;thread;d3d12")

    mfframesource_add_test(test_d3d12_camera_capture_smoke MFFrameSource::D3D12
        test_d3d12_camera_capture_smoke.cpp
    )
    set_tests_properties(test_d3d12_camera_capture_smoke PROPERTIES
        LABELS "smoke;camera;d3d12"
        ENVIRONMENT "MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR=${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}"
    )

    mfframesource_add_test(test_d3d12_camera_thread_smoke MFFrameSource::D3D12
        test_d3d12_camera_thread_smoke.cpp
    )
    set_tests_properties(test_d3d12_camera_thread_smoke PROPERTIES
        LABELS "smoke;camera;d3d12;thread"
        ENVIRONMENT "MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR=${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}"
    )

    mfframesource_add_test(test_d3d12_video_capture_smoke MFFrameSource::D3D12
        test_d3d12_video_capture_smoke.cpp
    )
    set_tests_properties(test_d3d12_video_capture_smoke PROPERTIES
        LABELS "smoke;video;d3d12"
        ENVIRONMENT "MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR=${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}"
    )

    mfframesource_add_test(test_d3d12_video_thread_smoke MFFrameSource::D3D12
        test_d3d12_video_thread_smoke.cpp
    )
    set_tests_properties(test_d3d12_video_thread_smoke PROPERTIES
        LABELS "smoke;video;d3d12;thread"
        ENVIRONMENT "MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR=${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}"
    )
endif()

if(TARGET MFFrameSource::D3D11)
    mfframesource_add_test(test_d3d11_capture_state MFFrameSource::D3D11
        test_d3d11_capture_state.cpp
    )
    set_tests_properties(test_d3d11_capture_state PROPERTIES LABELS "unit;d3d11")

    mfframesource_add_test(test_d3d11_capture_thread_state MFFrameSource::D3D11
        test_d3d11_capture_thread_state.cpp
    )
    set_tests_properties(test_d3d11_capture_thread_state PROPERTIES LABELS "unit;thread;d3d11")

    mfframesource_add_test(test_d3d11_sync_thread_state MFFrameSource::D3D11
        test_d3d11_sync_thread_state.cpp
    )
    set_tests_properties(test_d3d11_sync_thread_state PROPERTIES LABELS "unit;thread;sync;d3d11")

    mfframesource_add_test(test_d3d11_video_capture_state MFFrameSource::D3D11
        test_d3d11_video_capture_state.cpp
    )
    set_tests_properties(test_d3d11_video_capture_state PROPERTIES LABELS "unit;video;d3d11")

    mfframesource_add_test(test_d3d11_video_thread_state MFFrameSource::D3D11
        test_d3d11_video_thread_state.cpp
    )
    set_tests_properties(test_d3d11_video_thread_state PROPERTIES LABELS "unit;video;thread;d3d11")

    mfframesource_add_test(test_d3d11_camera_capture_smoke MFFrameSource::D3D11
        test_d3d11_camera_capture_smoke.cpp
    )
    set_tests_properties(test_d3d11_camera_capture_smoke PROPERTIES
        LABELS "smoke;camera;d3d11"
        ENVIRONMENT "MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR=${MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR}"
    )

    mfframesource_add_test(test_d3d11_video_capture_smoke MFFrameSource::D3D11
        test_d3d11_video_capture_smoke.cpp
    )
    set_tests_properties(test_d3d11_video_capture_smoke PROPERTIES
        LABELS "smoke;video;d3d11"
        ENVIRONMENT "MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR=${MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR}"
    )
endif()
