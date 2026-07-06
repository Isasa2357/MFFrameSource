# d3d12_stereo_sync sample

This sample opens two Media Foundation cameras through the D3D12 backend, pairs
frames by timestamp, previews the left/right concatenated image, and optionally
records the concatenated stream as H.264/MP4.

## Usage

```bat
out\build\default\Debug\d3d12_stereo_sync.exe ^
  <leftIndex> <rightIndex> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [pairCount] [outputMp4|-] [bitrate]
```

Example:

```bat
out\build\default\Debug\d3d12_stereo_sync.exe 0 1 1920 1080 60 1 NV12 out\build\default\_deps\d3d12helper-src\shaders\D3D12Processing 300 stereo_sync_output.mp4 50000000
```

`outputMp4` can be `-` to disable recording.

## Preview colour handling

The preview path reads the final D3D12 RGBA8 resources back to a CPU BGRA8
canvas.  The MP4 writer receives that full-resolution canvas as Media
Foundation RGB32.

For the window preview, the full stereo canvas is first downsampled to the
actual preview-window size by a CPU box filter, and then drawn to GDI at 1:1
using explicit BGRA bit masks.  This avoids the strong dithering/aliasing noise
that can appear when `StretchDIBits` or display-driver GDI paths scale a large
3840x1080 stereo image directly into a smaller preview window.

This is still only a debugging preview.  A production-quality realtime preview
should render the D3D12 textures to a DXGI swap chain on the GPU instead of
using CPU readback + GDI.
