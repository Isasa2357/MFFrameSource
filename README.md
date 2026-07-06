# MFFrameSource D3D12

Media Foundation から取得したカメラフレームを、Direct3D 12 の native resource として取り出すための C++17 ライブラリです。

このリポジトリは、D3D12 backend を先に実装する方針で作られています。Media Foundation の CPU native sample を受け取り、NV12 / P010 / BGRA / RGBA のまま D3D12 texture へ upload し、D3D12Processing で RGBA8 に変換して返します。D3D11 shared resource 経路や VideoCapture は、現時点では別拡張として扱います。

## 現在の実装範囲

```txt
MF camera
  -> CPU native sample
  -> D3D12 native input texture
  -> D3D12Processing
  -> owned RGBA8 D3D12Resource
```

実装済みの主なクラスです。

```txt
MFPlatformContext
MFCameraEnumerator
MFD3D12CameraCapture
MFD3D12CameraCaptureThread
MFD3D12CameraSyncThread
MFD3D12CameraFrame
```

主な機能です。

```txt
- Media Foundation camera enumeration
- exact format selection
- CPU native sample lock
- NV12 / P010 / RGB32 / ARGB32 input
- D3D12 native texture upload
- D3D12Processing による RGBA8 変換
- async ready fence model
- reusable frame pool
- queue ごとの独立 D3D12 GPU copy
- ThreadKit による capture worker / queue
- stereo timestamp sync
- samples / tests / install support
```

## 設計方針

### D3D12Core は外側で作る

`D3D12Core` は複数オブジェクトで共有される前提です。そのため、このライブラリ内部では D3D12 device / adapter を勝手に生成しません。利用側で `std::shared_ptr<D3D12CoreLib::D3D12Core>` を作成し、各 capture object に渡してください。

### format selection は完全一致

`width`、`height`、`fps`、`subtype` はすべて完全一致です。一致する Media Foundation native media type が無い場合、`open()` は失敗します。best match や暗黙 fallback は行いません。

### frame は async ready fence を持つ

`read()` や queue から受け取った frame は、標準では GPU command submit 後に返ります。GPU 完了を待ってから使う場合は、利用側で `waitReady()` を呼びます。

```cpp
if (result.ok()) {
    auto frame = std::move(result.frame);
    frame.waitReady();
    auto& tex = frame.resource();
}
```

同期挙動が必要な場合は、`MFCameraCaptureConfig::waitForGpuCompletionOnRead = true` を指定できます。

### 複数 queue へ同じ resource は流さない

`MFD3D12CameraCaptureThread` は、登録された queue ごとに独立した D3D12 texture へ GPU copy して配信します。これにより、後段 pipeline ごとの resource state / lifetime が混ざらないようにしています。

## 未実装 / 対象外

```txt
- MFD3D12VideoCapture
- MJPEG / H.264 / H.265 compressed sample path
- D3D11/D3D12 shared GPU sample path
- D3D11 public backend
- video encode / file recording
```

このライブラリは現時点では「カメラから D3D12 resource を取り出す」部分が対象です。動画エンコードはまだ行いません。

## 依存関係

必須環境です。

```txt
Windows 10/11
Visual Studio 2022
CMake 3.20+
C++17
Media Foundation
Direct3D 12
```

外部依存は CMake の `FetchContent` で取得できます。

```txt
D3D12Helper
ThreadKit
```

手元の clone を使いたい場合は、`D3D12HELPER_ROOT` / `THREADKIT_ROOT` を指定できます。通常は指定不要です。

## ビルド

Visual Studio 2022 x64 の例です。

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DMFFRAMESOURCE_BUILD_SAMPLE=ON ^
  -DMFFRAMESOURCE_BUILD_TESTS=ON

cmake --build out/build/default --config Debug

ctest --test-dir out/build/default -C Debug --output-on-failure
```

DXC runtime が自動検出される場合、`dxcompiler.dll` / `dxil.dll` は sample / test exe の横へコピーされます。見つからない場合は、`Microsoft.Direct3D.DXC` を NuGet 等で復元するか、以下を指定してください。

```bat
cmake -S . -B out/build/default -G "Visual Studio 17 2022" -A x64 ^
  -DD3D12HELPER_DXC_RUNTIME_DIR=C:\path\to\dxc\bin\x64 ^
  -DMFFRAMESOURCE_BUILD_SAMPLE=ON ^
  -DMFFRAMESOURCE_BUILD_TESTS=ON
```

## install

```bat
cmake --install out/build/default --config Debug --prefix C:\Work\install\MFFrameSourceD3D12
```

install されるものです。

```txt
include/
lib/MFFrameSourceD3D12.lib
lib/cmake/MFFrameSourceD3D12/
share/MFFrameSourceD3D12/shaders/D3D12Processing/
bin/dxcompiler.dll, dxil.dll  # 見つかった場合のみ
```

## サンプル

### カメラ一覧と対応 format の表示

```bat
out\build\default\Debug\d3d12_camera_read.exe list
```

### 1 frame 読み取り

```bat
out\build\default\Debug\d3d12_camera_read.exe read 0 1920 1080 60 1 NV12 out\build\default\_deps\d3d12helper-src\shaders\D3D12Processing
```

### capture thread 経由で読み取り

```bat
out\build\default\Debug\d3d12_camera_thread_read.exe 0 1920 1080 60 1 NV12 out\build\default\_deps\d3d12helper-src\shaders\D3D12Processing 120
```

### benchmark

```bat
out\build\default\Debug\d3d12_camera_benchmark.exe 0 1920 1080 60 1 NV12 out\build\default\_deps\d3d12helper-src\shaders\D3D12Processing 300
```

### stereo sync

```bat
out\build\default\Debug\d3d12_stereo_sync.exe 0 1 1920 1080 60 1 NV12 out\build\default\_deps\d3d12helper-src\shaders\D3D12Processing 120
```

## 任意 smoke test

実カメラを使う smoke test は、環境変数が無い場合は skip 扱いで pass します。実行する場合は以下を設定します。

```bat
set "MFFRAMESOURCE_TEST_CAMERA_INDEX=0"
set "MFFRAMESOURCE_TEST_WIDTH=1920"
set "MFFRAMESOURCE_TEST_HEIGHT=1080"
set "MFFRAMESOURCE_TEST_FPS_NUM=60"
set "MFFRAMESOURCE_TEST_FPS_DEN=1"
set "MFFRAMESOURCE_TEST_SUBTYPE=NV12"
set "MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR=out\build\default\_deps\d3d12helper-src\shaders\D3D12Processing"

ctest --test-dir out/build/default -C Debug --output-on-failure
```

## 最小コード例

```cpp
#include <MFFrameSource/MFPlatformContext.hpp>
#include <MFFrameSource/MFD3D12CameraCapture.hpp>
#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

using namespace MFFrameSource;

int main() {
    MFPlatformContext platform;

    auto core = D3D12CoreLib::D3D12Core::CreateShared();

    MFCameraSelector selector;
    selector.deviceIndex = 0;

    MFCameraCaptureConfig config;
    config.input.width = 1920;
    config.input.height = 1080;
    config.input.fps = {60, 1};
    config.input.subtype = MFVideoFormat_NV12;
    config.processingShaderDirectory = L"out/build/default/_deps/d3d12helper-src/shaders/D3D12Processing";

    MFD3D12CameraCapture capture;
    if (!capture.open(selector, config, core)) {
        return 1;
    }

    auto result = capture.read();
    if (!result.ok()) {
        return 1;
    }

    result.frame.waitReady();
    auto& texture = result.frame.resource();
    (void)texture;
    return 0;
}
```

## ディレクトリ構成

```txt
include/MFFrameSource/
  public headers

src/
  public class implementations

src/internal/
  MF sample reader, frame pool, frame cloner

sample/
  executable samples

test/
  unit tests and optional camera smoke tests

cmake/
  package config template
```

## GitHub へ push する前の確認

```bat
cmake --build out/build/default --config Debug
ctest --test-dir out/build/default -C Debug --output-on-failure
git status --ignored
```

`.gitignore` により、`out/`、Visual Studio の一時ファイル、FetchContent の `_deps`、NuGet packages、install 先、生成 zip などは `git add .` の対象から外れます。
