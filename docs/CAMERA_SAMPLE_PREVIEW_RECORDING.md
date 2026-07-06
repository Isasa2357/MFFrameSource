# Camera sample preview / optional recording

The continuous camera samples now present the captured D3D12 RGBA8 frames in a Win32 preview window.

Affected samples:

- `d3d12_camera_thread_read`
- `d3d12_camera_benchmark`

Both samples keep recording optional.  The additional arguments are:

```bat
[outputMp4|-] [bitrate]
```

- Omit `outputMp4` to disable recording.
- Pass `-` to explicitly disable recording.
- Pass a file path such as `camera.mp4` to write H.264/MP4.
- `bitrate` is optional and defaults to `20000000`.

Examples:

```bat
out\build\default\Debug\d3d12_camera_thread_read.exe 0 1920 1080 60 1 NV12 out\build\default\_deps\d3d12helper-src\shaders\D3D12Processing 300 -
```

```bat
out\build\default\Debug\d3d12_camera_thread_read.exe 0 1920 1080 60 1 NV12 out\build\default\_deps\d3d12helper-src\shaders\D3D12Processing 300 camera_thread.mp4 30000000
```

The preview path uses D3D12 readback and a CPU-side box-filter downscale before GDI presentation.  This avoids the strong GDI scaling noise that appears when a full-resolution camera image is stretched directly in the window.

The MP4 writer receives the full-resolution BGRA frame, so preview downscaling does not reduce recording quality.
