function(mfframesource_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE MFFrameSource::D3D12)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /EHsc /W4 /permissive-)
    endif()

    if(COMMAND mfframesource_copy_dxc_runtime)
        mfframesource_copy_dxc_runtime(${target})
    endif()

    add_test(NAME ${target} COMMAND ${target})
endfunction()

mfframesource_add_test(test_common
    test_common.cpp
)
set_tests_properties(test_common PROPERTIES LABELS "unit")

mfframesource_add_test(test_capture_state
    test_capture_state.cpp
)
set_tests_properties(test_capture_state PROPERTIES LABELS "unit")

mfframesource_add_test(test_capture_thread_state
    test_capture_thread_state.cpp
)
set_tests_properties(test_capture_thread_state PROPERTIES LABELS "unit;thread")

mfframesource_add_test(test_sync_thread_state
    test_sync_thread_state.cpp
)
set_tests_properties(test_sync_thread_state PROPERTIES LABELS "unit;thread;sync")

mfframesource_add_test(test_platform_enumeration
    test_platform_enumeration.cpp
)
set_tests_properties(test_platform_enumeration PROPERTIES LABELS "mf;unit")

mfframesource_add_test(test_d3d12_camera_capture_smoke
    test_d3d12_camera_capture_smoke.cpp
)
set_tests_properties(test_d3d12_camera_capture_smoke PROPERTIES
    LABELS "smoke;camera;d3d12"
    ENVIRONMENT "MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR=${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}"
)

mfframesource_add_test(test_d3d12_camera_thread_smoke
    test_d3d12_camera_thread_smoke.cpp
)
set_tests_properties(test_d3d12_camera_thread_smoke PROPERTIES
    LABELS "smoke;camera;d3d12;thread"
    ENVIRONMENT "MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR=${MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR}"
)
