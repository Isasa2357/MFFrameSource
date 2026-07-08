# MFFrameSource v1.1.0 Release Notes

## Summary

`v1.1.0` adds the D3D11 backend and updates the dependency pins for the D3D helper libraries. The main goal of this release is to provide parity between D3D11 and D3D12 for Media Foundation camera / video frame acquisition into owned native GPU resources.

This release is prepared on branch:

```txt
feature/d3d11-backend-v1.1.0
```

Baseline for `v1.0.0`:

```txt
d18e560f4e43f568e94b03cdfde245548293b686
```

## Dependency pins

- `D3D11Helper`: `v1.12.0`
- `D3D12Helper`: `v1.12.0`
- `ThreadKit`: `main`

D3DHelper `v1.12.0` includes Advanced Processing, but MFFrameSource capture paths continue to use the fused convert / resize processing path.

## Added D3D11 backend

New public API:

- `MFD3D11CameraCapture`
- `MFD3D11CameraCaptureThread`
- `MFD3D11CameraSyncThread`
- `MFD3D11VideoCapture`
- `MFD3D11VideoCaptureThread`

D3D11 backend behavior:

- CPU native Media Foundation sample upload
- D3D11Processing-based conversion to RGBA8
- default output format: `DXGI_FORMAT_R8G8B8A8_UNORM`
- owned native D3D11 texture output through `D3D11CoreLib::D3D11Resource`
- SRV access through `frame.srv()`
- queue-specific GPU copy for thread distribution
- stereo / multi-camera timestamp sync
- D3D11 immediate-context readiness model

For thread and sync usage, applications should enable D3D11 multithread protection on the shared D3D11 core when the same core is used across capture / consumer threads.

## Video decode robustness

The video file reader now tolerates small decoder padding changes after the first `ReadSample()`.

Example handled by this release:

```txt
requested visible decode size: 2880x1404
actual decoder sample size:   2880x1408
```

The library opens the stream with the visible size request, accepts the small padded coded-size change, and rebuilds the upload pool to match the actual sample layout while preserving the requested output size.

## Tests added or updated

D3D11 unit / smoke coverage includes:

- D3D11 capture state
- D3D11 capture thread state
- D3D11 sync thread state
- D3D11 video capture state
- D3D11 video thread state
- D3D11 camera capture smoke
- D3D11 video capture smoke
- D3D11 camera thread smoke
- D3D11 video thread smoke
- D3D11 sync thread smoke

Smoke tests skip automatically when required real-device / real-file environment variables are missing.

## Samples added

D3D11 CLI samples:

- `d3d11_camera_read`
- `d3d11_camera_thread_read`
- `d3d11_stereo_sync`
- `d3d11_video_read`
- `d3d11_video_thread_read`

These D3D11 samples are intentionally minimal validation samples. They print frame metadata and confirm resource / SRV availability. D3D11 preview / recording samples are not part of this release.

## Build notes

Recommended configure command:

```bat
cmake -S . -B out\build\default -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_BUILD_D3D11=ON ^
  -DMFFRAMESOURCE_BUILD_D3D12=ON ^
  -DMFFRAMESOURCE_BUILD_SAMPLE=ON ^
  -DMFFRAMESOURCE_BUILD_TESTS=ON ^
  -DMFFRAMESOURCE_INSTALL=OFF ^
  -DMFFRAMESOURCE_FETCH_D3D11HELPER=ON ^
  -DMFFRAMESOURCE_FETCH_D3D12HELPER=ON ^
  -DMFFRAMESOURCE_FETCH_THREADKIT=ON ^
  -DMFFRAMESOURCE_D3D11HELPER_GIT_TAG=v1.12.0 ^
  -DMFFRAMESOURCE_D3D12HELPER_GIT_TAG=v1.12.0
```

Build and test:

```bat
cmake --build out\build\default --config Debug --parallel
ctest --test-dir out\build\default -C Debug --output-on-failure
```

## Verification before tag

Before creating `v1.1.0`, run:

```bat
git fetch origin
git checkout feature/d3d11-backend-v1.1.0
git pull

cmake --build out\build\d3dhelper_v112 --config Debug --parallel
ctest --test-dir out\build\d3dhelper_v112 -C Debug --output-on-failure
```

Real-device smoke tests used during preparation:

- D3D11 camera capture smoke: passed
- D3D11 video capture smoke: passed
- D3D11 camera thread smoke: passed
- D3D11 video thread smoke: passed
- Full CTest: passed

D3D11 sync smoke is registered and can be run when two compatible cameras are available.

## Tag command

Do not run this until the final build / CTest pass on the release commit.

```bat
git tag v1.1.0 <release-commit-sha>
git push origin v1.1.0
```
