# MFFrameSource

`MFFrameSource` は、Windows Media Foundation から取得したカメラ映像または動画ファイルのフレームを、**owned native D3D11 / D3D12 resource** として取り出すための C++17 ライブラリです。

D3D11 backend は [D3D11Helper](https://github.com/Isasa2357/D3D11Helper.git) を、D3D12 backend は [D3D12Helper](https://github.com/Isasa2357/D3D12Helper.git) を使います。capture / playback thread と frame queue には [ThreadKit](https://github.com/Isasa2357/ThreadKit.git) を使います。

Repository:

```txt
https://github.com/Isasa2357/MFFrameSource.git
```

## Version / dependency pins

- `main` の `d18e560f4e43f568e94b03cdfde245548293b686` を `v1.0.0` baseline として扱います。
- この更新ブランチの CMake project version は `1.1.0` です。
- `v1.1.0` のゴールは、新 D3DHelper 対応と D3D11 backend の追加です。
- `FetchContent` で取得する `D3D11Helper` / `D3D12Helper` は、`main` ではなく明示的に `v1.11.0` を指定します。
- D3DHelper は今後も更新される可能性があるため、再現性が必要な通常ビルドでは `MFFRAMESOURCE_D3D11HELPER_GIT_TAG` / `MFFRAMESOURCE_D3D12HELPER_GIT_TAG` をタグまたは commit SHA に固定してください。

## Backend overview

### D3D11 backend

- `MFD3D11CameraCapture`
- `MFD3D11CameraCaptureThread`
- `MFD3D11CameraSyncThread`
- `MFD3D11VideoCapture`
- `MFD3D11VideoCaptureThread`
- CPU native sample upload path
- D3D11Processing-based conversion to RGBA8
- default output: `DXGI_FORMAT_R8G8B8A8_UNORM`
- owned native D3D11 texture output via `D3D11CoreLib::D3D11Resource`
- SRV access through `frame.srv()`
- frame pool / lease / reuse
- queue-specific GPU copy
- stereo / multi-camera timestamp sync

D3D11 backend では D3D11 immediate context の command stream を使います。`frame.waitReady()` は `D3D11Core::Flush()` を呼び、CPU 側から明示的な完了点を作りたい場合に使います。

### D3D12 backend

- `MFD3D12CameraCapture`
- `MFD3D12CameraCaptureThread`
- `MFD3D12CameraSyncThread`
- `MFD3D12VideoCapture`
- `MFD3D12VideoCaptureThread`
- CPU native sample upload path
- D3D12Processing-based conversion to RGBA8
- default output: `DXGI_FORMAT_R8G8B8A8_UNORM`
- owned native D3D12 texture output via `D3D12CoreLib::D3D12Resource`
- asynchronous ready-fence frame model
- `frame.isReady()` / `frame.waitReady()`
- frame pool / lease / reuse
- per-slot transient descriptors
- queue-specific GPU copy
- stereo / multi-camera timestamp sync

## Media input scope

### Camera input

- camera device enumeration
- camera format enumeration
- strict native camera format selection by `width`, `height`, `fps`, and `subtype`
- no best-match fallback

### Video file input

- Media Foundation decode to exact requested CPU output format
- EOF handling
- loop playback
- seek by 100 ns Media Foundation time
- real-time playback by timestamp
- decode-as-fast-as-possible playback

### Supported primary input subtypes

```txt
NV12
P010
RGB32
ARGB32
```

## 方針

このライブラリは、プロトタイプ用の簡易 wrapper ではなく、アプリケーション側の D3D11 / D3D12 pipeline へ直接接続するための backend として実装しています。

- `D3D11Core` / `D3D12Core` はライブラリ内部で勝手に生成しません。外側で作成した shared core を渡します。
- camera / video の format selection は原則 exact match です。
- 複数 queue へ同じ GPU resource を共有配信せず、queue ごとに GPU copy した独立 resource を配信します。
- CPU 側で NV12→RGBA 変換は行いません。変換は D3D11Processing / D3D12Processing に任せます。
- D3D11 / D3D12 の API 形状はできる限り対応させますが、同期モデルは backend の性質に合わせます。

## Requirements

- Windows 10/11
- Visual Studio 2022
- CMake 3.20+
- Direct3D 11 / Direct3D 12
- Media Foundation
- Git

FetchContent を使う場合、configure 時に以下を取得します。

- `https://github.com/Isasa2357/D3D11Helper.git` at `v1.11.0`
- `https://github.com/Isasa2357/D3D12Helper.git` at `v1.11.0`
- `https://github.com/Isasa2357/ThreadKit.git`

ローカル checkout を使う場合は、`D3D11HELPER_ROOT`、`D3D12HELPER_ROOT`、`THREADKIT_ROOT` を指定できます。再現性を重視する場合、ローカルの D3DHelper も `v1.11.0` に checkout してください。

## Build

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_BUILD_D3D11=ON ^
  -DMFFRAMESOURCE_BUILD_D3D12=ON ^
  -DMFFRAMESOURCE_BUILD_SAMPLE=ON ^
  -DMFFRAMESOURCE_BUILD_TESTS=ON

cmake --build out/build/default --config Debug

ctest --test-dir out/build/default -C Debug --output-on-failure
```

ローカルの D3DHelper / ThreadKit を使う場合:

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_BUILD_D3D11=ON ^
  -DMFFRAMESOURCE_BUILD_D3D12=ON ^
  -DMFFRAMESOURCE_BUILD_SAMPLE=ON ^
  -DMFFRAMESOURCE_BUILD_TESTS=ON ^
  -DD3D11HELPER_ROOT=C:\path\to\D3D11Helper ^
  -DD3D12HELPER_ROOT=C:\path\to\D3D12Helper ^
  -DTHREADKIT_ROOT=C:\path\to\ThreadKit

cmake --build out/build/default --config Debug

ctest --test-dir out/build/default -C Debug --output-on-failure
```

依存タグを明示的に上書きする場合:

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_D3D11HELPER_GIT_TAG=v1.11.0 ^
  -DMFFRAMESOURCE_D3D12HELPER_GIT_TAG=v1.11.0
```

D3D11 backend だけをビルドする場合:

```bat
cmake -S . -B out/build/d3d11 -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_BUILD_D3D11=ON ^
  -DMFFRAMESOURCE_BUILD_D3D12=OFF
```

D3D12 backend だけをビルドする場合:

```bat
cmake -S . -B out/build/d3d12 -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_BUILD_D3D11=OFF ^
  -DMFFRAMESOURCE_BUILD_D3D12=ON
```

## Install

```bat
cmake --install out/build/default --config Debug --prefix C:\Work\install\MFFrameSource
```

install では以下を配置します。

- public headers
- static libraries
- CMake package files
- D3D11Processing / D3D12Processing shaders when available
- `dxcompiler.dll` / `dxil.dll` when D3D12Helper finds them

## Frame readiness

D3D12 backend は、標準では GPU command submission 後に frame を返します。別 queue や CPU readback など、producer path の外で texture を使う前には `waitReady()` を呼んでください。

```cpp
MFD3D12CameraReadResult r = capture.read();
if (r.ok()) {
    r.frame.waitReady();
    auto& texture = r.frame.resource();
}
```

D3D11 backend では `isReady()` は非空 frame に対して true を返します。CPU 側で明示的に D3D11 command stream の完了点を作りたい場合は `waitReady()` を呼びます。

```cpp
MFD3D11CameraReadResult r = capture.read();
if (r.ok()) {
    r.frame.waitReady();
    auto& texture = r.frame.resource();
    ID3D11ShaderResourceView* srv = r.frame.srv();
}
```

完全同期挙動が必要な場合は、config 側で指定します。

```cpp
MFCameraCaptureConfig cfg;
cfg.waitForGpuCompletionOnRead = true;
```

Video でも同じ config field を使います。

```cpp
MFVideoCaptureConfig cfg;
cfg.waitForGpuCompletionOnRead = true;
```

## Exact format selection

Camera input は native media type から exact match します。`subtype / width / height / fps` はすべて指定してください。一致する native format が無い場合、`open()` は失敗します。

```cpp
MFCameraCaptureConfig cfg;
cfg.input.width = 1920;
cfg.input.height = 1080;
cfg.input.fps = {60, 1};
cfg.input.subtype = MFVideoFormat_NV12;
```

Video input は Media Foundation decoder の出力形式に対して exact request を出します。

```cpp
MFVideoCaptureConfig cfg;
cfg.input.width = 1920;
cfg.input.height = 1080;
cfg.input.fps = {60, 1};
cfg.input.subtype = MFVideoFormat_NV12;
```

## Samples

現時点の sample executable は D3D12 backend 向けです。D3D11 backend の API は library / tests に入っていますが、D3D11 専用 sample executable は今後追加予定です。

### Camera list / single read

```bat
out\build\default\Debug\d3d12_camera_read.exe list

out\build\default\Debug\d3d12_camera_read.exe read 0 1920 1080 60 1 NV12 C:\path\to\D3D12Helper\shaders\D3D12Processing
```

### Camera thread

```bat
out\build\default\Debug\d3d12_camera_thread_read.exe 0 1920 1080 60 1 NV12 C:\path\to\D3D12Helper\shaders\D3D12Processing 300
```

### Stereo sync

```bat
out\build\default\Debug\d3d12_stereo_sync.exe 0 1 1920 1080 60 1 NV12 C:\path\to\D3D12Helper\shaders\D3D12Processing 300
```

### Video read

```bat
out\build\default\Debug\d3d12_video_read.exe C:\path\to\input.mp4 1920 1080 60 1 NV12 C:\path\to\D3D12Helper\shaders\D3D12Processing 300
```

## Smoke tests with real devices/files

通常の `ctest` は、実カメラや実動画が無い環境でも通るように smoke test を skip します。実機 smoke test を有効にする場合は、環境変数を設定してください。

Camera:

```bat
set "MFFRAMESOURCE_TEST_CAMERA_INDEX=0"
set "MFFRAMESOURCE_TEST_CAMERA_WIDTH=1920"
set "MFFRAMESOURCE_TEST_CAMERA_HEIGHT=1080"
set "MFFRAMESOURCE_TEST_CAMERA_FPS_NUM=60"
set "MFFRAMESOURCE_TEST_CAMERA_FPS_DEN=1"
set "MFFRAMESOURCE_TEST_CAMERA_SUBTYPE=NV12"

ctest --test-dir out/build/default -C Debug -R camera --output-on-failure
```

Video:

```bat
set "MFFRAMESOURCE_TEST_VIDEO_PATH=C:\path\to\input.mp4"
set "MFFRAMESOURCE_TEST_VIDEO_WIDTH=1920"
set "MFFRAMESOURCE_TEST_VIDEO_HEIGHT=1080"
set "MFFRAMESOURCE_TEST_VIDEO_FPS_NUM=60"
set "MFFRAMESOURCE_TEST_VIDEO_FPS_DEN=1"
set "MFFRAMESOURCE_TEST_VIDEO_SUBTYPE=NV12"

ctest --test-dir out/build/default -C Debug -R video --output-on-failure
```

## Current limitations

以下は現時点の主経路には含めていません。

- D3D11/D3D12 shared GPU sample path
- GPU hardware decode surface を D3D11 / D3D12 へ直接受ける path
- library API としての video writer / encoder
- D3D11 専用 sample executable
- YUY2 / UYVY / I420 / YV12 などの追加 converter

NV12 / P010 / RGB32 / ARGB32 を D3D11 / D3D12 RGBA8 resource として取り出す用途では、camera / video / thread / sync の backend API まで実装済みです。
