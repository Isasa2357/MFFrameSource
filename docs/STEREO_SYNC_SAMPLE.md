# d3d12_stereo_sync sample

`d3d12_stereo_sync` captures two Media Foundation camera streams, synchronizes them by timestamp, shows a preview window, and writes a horizontally concatenated stereo MP4.

## Usage

```bat
out\build\default\Debug\d3d12_stereo_sync.exe ^
  <leftIndex> <rightIndex> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [pairCount] [outputMp4|-] [bitrate]
```

Example:

```bat
out\build\default\Debug\d3d12_stereo_sync.exe 0 1 1920 1080 60 1 NV12 out\build\default\_deps\d3d12helper-src\shaders\D3D12Processing 300 stereo_sync_output.mp4 50000000
```

## Arguments

| Argument | Meaning |
|---|---|
| `leftIndex` | Media Foundation device index for the left camera. |
| `rightIndex` | Media Foundation device index for the right camera. |
| `width` / `height` | Exact input format size requested from both cameras. |
| `fpsNum` / `fpsDen` | Exact input frame rate requested from both cameras. |
| `subtype` | `NV12`, `P010`, `RGB32`, or `ARGB32`. |
| `shaderDir` | D3D12Helper `shaders/D3D12Processing` directory. |
| `pairCount` | Number of synchronized stereo pairs to process. Default: `120`. |
| `outputMp4` | Output MP4 path. Default: `stereo_sync_output.mp4`. Use `-` to disable writing. |
| `bitrate` | H.264 output bitrate. Default: `50000000`. |

## Output format

The preview and MP4 writer use CPU readback from the owned D3D12 RGBA8 output textures. The sample converts the left and right RGBA8 resources to a single top-down RGB32/BGRA CPU image:

```txt
[left RGBA8 D3D12 resource] + [right RGBA8 D3D12 resource]
        -> CPU BGRA canvas: width = leftWidth * 2, height = leftHeight
        -> Win32 preview window
        -> Media Foundation Sink Writer H.264 MP4
```

This readback/encode path is intentionally sample-local. It does not change the library API, which still returns owned native D3D12 resources.
