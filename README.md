# MFFrameSource D3D12 backend

Media Foundation camera capture backend that returns owned native D3D12 RGBA resources.

## Current implemented scope

- `MFD3D12CameraCapture`
- `MFD3D12CameraCaptureThread`
- `MFD3D12CameraSyncThread`
- strict native camera format selection by `width`, `height`, `fps`, and `subtype`
- CPU native sample upload path for NV12/P010/BGRA8/RGBA8
- D3D12Processing fused conversion to RGBA8
- queue-specific GPU copies
- asynchronous ready-fence frame model
- CMake FetchContent for D3D12Helper and ThreadKit
- tests and camera smoke samples
- install/package/runtime shader organization

`MFD3D12VideoCapture` is intentionally not implemented in this package.

## Build

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_BUILD_SAMPLE=ON ^
  -DMFFRAMESOURCE_BUILD_TESTS=ON

cmake --build out/build/default --config Debug

ctest --test-dir out/build/default -C Debug --output-on-failure
```

## Frame readiness

Frames are returned after D3D12 command submission by default. Before using the texture outside the producer path, call:

```cpp
frame.waitReady();
```

For blocking behavior:

```cpp
MFCameraCaptureConfig cfg;
cfg.waitForGpuCompletionOnRead = true;
```

## Exact camera format selection

The input request must be complete and exact:

```cpp
cfg.input.width = 1920;
cfg.input.height = 1080;
cfg.input.fps = {60, 1};
cfg.input.subtype = MFVideoFormat_NV12;
```

If there is no exact native media type match, `open()` fails.

## Install

```bat
cmake --install out/build/default --config Debug --prefix C:\path\to\install
```

This installs public headers, the static library, D3D12Processing shaders, CMake package files, and DXC runtime DLLs when D3D12Helper finds them.


## v0.4 video input

This version adds `MFD3D12VideoCapture` and `MFD3D12VideoCaptureThread`.  Video files are decoded by Media Foundation into an exact requested CPU output format, uploaded to native D3D12 textures, and converted to owned RGBA8 D3D12 resources by D3D12Processing.

New samples:

```bat
d3d12_video_read <file> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [frameCount]
d3d12_video_thread_read <file> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [frameCount] [fast]
d3d12_video_benchmark <file> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> <frameCount>
```

Video exact selection uses `width / height / fps / subtype`; no best-match fallback is performed.
