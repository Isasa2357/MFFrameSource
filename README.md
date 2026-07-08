# MFFrameSource

`MFFrameSource` は、Windows Media Foundation から取得したカメラ映像または動画ファイルのフレームを、**owned native D3D12 resource** として取り出すための C++17 ライブラリです。

現在の実装は **D3D12 backend** が中心です。D3D12 の device / queue / fence / command / upload / processing などの定型処理は [D3D12Helper](https://github.com/Isasa2357/D3D12Helper.git) を使い、capture / playback thread と frame queue には [ThreadKit](https://github.com/Isasa2357/ThreadKit.git) を使います。

Repository:

```txt
https://github.com/Isasa2357/MFFrameSource.git
```

## Version / dependency pins

- 現在の `main` の状態を `v1.0.0` baseline として扱います。
- この更新ブランチの CMake project version は `1.0.1` です。
- `FetchContent` で取得する `D3D12Helper` は、`main` ではなく明示的に `v1.11.0` を指定します。
- `D3D12Helper` は今後も更新される可能性があるため、再現性が必要な通常ビルドでは `MFFRAMESOURCE_D3D12HELPER_GIT_TAG` をタグまたは commit SHA に固定してください。
- `D3D11Helper` は現時点の MFFrameSource には直接リンクしていません。D3D11 backend を追加する場合は、その時点で確認できる最新タグを明示的に指定して取り込んでください。

## 実装済みの範囲

### Camera input

- `MFD3D12CameraCapture`
- `MFD3D12CameraCaptureThread`
- `MFD3D12CameraSyncThread`
- camera device enumeration
- camera format enumeration
- strict native camera format selection by `width`, `height`, `fps`, and `subtype`
- no best-match fallback
- queue-specific GPU copy
- stereo / multi-camera timestamp sync

### Video file input

- `MFD3D12VideoCapture`
- `MFD3D12VideoCaptureThread`
- Media Foundation decode to exact requested CPU output format
- EOF handling
- loop playback
- seek by 100 ns Media Foundation time
- real-time playback by timestamp
- decode-as-fast-as-possible playback

### D3D12 frame model

- CPU native sample upload path
- supported primary input subtypes: `NV12`, `P010`, `RGB32`, `ARGB32`
- D3D12Processing-based conversion to RGBA8
- default output: `DXGI_FORMAT_R8G8B8A8_UNORM`
- owned native D3D12 texture output
- asynchronous ready-fence frame model
- `frame.isReady()` / `frame.waitReady()`
- frame pool / lease / reuse
- per-slot transient descriptors
- install support for headers, static library, CMake package files, D3D12Processing shaders, and DXC runtime DLLs when found

## 方針

このライブラリは、プロトタイプ用の簡易 wrapper ではなく、アプリケーション側の D3D12 pipeline へ直接接続するための backend として実装しています。

- `D3D12Core` はライブラリ内部で勝手に生成しません。外側で作成した shared `D3D12Core` を渡します。
- camera / video の format selection は原則 exact match です。
- 複数 queue へ同じ D3D12 resource を共有配信せず、queue ごとに GPU copy した独立 resource を配信します。
- producer は標準では GPU command submit 後に frame を返します。consumer は必要に応じて `waitReady()` します。
- CPU 側で NV12→RGBA 変換は行いません。変換は D3D12Processing に任せます。

## Requirements

- Windows 10/11
- Visual Studio 2022
- CMake 3.20+
- Direct3D 12
- Media Foundation
- Git

FetchContent を使う場合、configure 時に以下を取得します。

- `https://github.com/Isasa2357/D3D12Helper.git` at `v1.11.0`
- `https://github.com/Isasa2357/ThreadKit.git`

ローカル checkout を使う場合は、`D3D12HELPER_ROOT` と `THREADKIT_ROOT` を指定できます。再現性を重視する場合、ローカルの `D3D12HELPER_ROOT` も `v1.11.0` に checkout してください。

## Build

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_BUILD_SAMPLE=ON ^
  -DMFFRAMESOURCE_BUILD_TESTS=ON

cmake --build out/build/default --config Debug

ctest --test-dir out/build/default -C Debug --output-on-failure
```

ローカルの D3D12Helper / ThreadKit を使う場合:

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_BUILD_SAMPLE=ON ^
  -DMFFRAMESOURCE_BUILD_TESTS=ON ^
  -DD3D12HELPER_ROOT=C:\path\to\D3D12Helper ^
  -DTHREADKIT_ROOT=C:\path\to\ThreadKit

cmake --build out/build/default --config Debug

ctest --test-dir out/build/default -C Debug --output-on-failure
```

依存タグを明示的に上書きする場合:

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_D3D12HELPER_GIT_TAG=v1.11.0
```

## Install

```bat
cmake --install out/build/default --config Debug --prefix C:\Work\install\MFFrameSourceD3D12
```

install では以下を配置します。

- public headers
- static library
- CMake package files
- D3D12Processing shaders
- `dxcompiler.dll` / `dxil.dll` when D3D12Helper finds them

## Frame readiness

標準では、frame は GPU command submission 後に返ります。別 queue や CPU readback など、producer path の外で texture を使う前には `waitReady()` を呼んでください。

```cpp
MFD3D12CameraReadResult r = capture.read();
if (r.ok()) {
    r.frame.waitReady();
    auto& texture = r.frame.resource();
}
```

完全同期挙動が必要な場合は、config 側で指定します。

```cpp
MFCameraCaptureConfig cfg;
cfg.waitForGpuCompletionOnRead = true;
```

Video でも同じ frame type を使います。

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

主に想定している input subtype は以下です。

```txt
NV12
P010
RGB32
ARGB32
```

出力 texture は標準で `DXGI_FORMAT_R8G8B8A8_UNORM` です。

## Samples

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

### Camera benchmark

```bat
out\build\default\Debug\d3d12_camera_benchmark.exe 0 1920 1080 60 1 NV12 C:\path\to\D3D12Helper\shaders\D3D12Processing 1000
```

### Video read

```bat
out\build\default\Debug\d3d12_video_read.exe C:\path\to\input.mp4 1920 1080 60 1 NV12 C:\path\to\D3D12Helper\shaders\D3D12Processing 300
```

### Video thread

Real-time by timestamp:

```bat
out\build\default\Debug\d3d12_video_thread_read.exe C:\path\to\input.mp4 1920 1080 60 1 NV12 C:\path\to\D3D12Helper\shaders\D3D12Processing 300
```

Decode as fast as possible:

```bat
out\build\default\Debug\d3d12_video_thread_read.exe C:\path\to\input.mp4 1920 1080 60 1 NV12 C:\path\to\D3D12Helper\shaders\D3D12Processing 300 fast
```

### Video benchmark

```bat
out\build\default\Debug\d3d12_video_benchmark.exe C:\path\to\input.mp4 1920 1080 60 1 NV12 C:\path\to\D3D12Helper\shaders\D3D12Processing 1000
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

- D3D11 backend
- D3D11/D3D12 shared GPU sample path
- GPU hardware decode surface を D3D12 へ直接受ける path
- library API としての video writer / encoder
- YUY2 / UYVY / I420 / YV12 などの追加 converter

NV12 / P010 / RGB32 / ARGB32 を D3D12 RGBA8 resource として取り出す用途では、camera / video / thread / sync / benchmark まで実装済みです。
