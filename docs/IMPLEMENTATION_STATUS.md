# MFFrameSource D3D12 backend implementation status

This document summarizes the final implementation status of the current D3D12 backend.

## Review result

The latest repository revision was checked for obvious prototype markers and incomplete implementation markers.

Checked keywords:

```txt
TODO
FIXME
prototype
Not implemented
not implemented
仮
```

No source-level prototype markers were found in the repository search results.

One documentation inconsistency was found: the top-level README still said that `MFD3D12VideoCapture` was intentionally not implemented, while the repository already contains the video capture implementation. The final README in this delta removes that obsolete statement and updates the implemented scope.

## Implemented public classes

### Common / platform

- `MFPlatformContext`
- `MFCameraEnumerator`
- `MFCameraSelector`
- `MFCameraFormatRequest`
- `MFErrorInfo`
- `MFFrameStatus`

### D3D12 camera

- `MFD3D12CameraCapture`
- `MFD3D12CameraCaptureThread`
- `MFD3D12CameraSyncThread`
- `MFD3D12CameraFrame`

### D3D12 video

- `MFD3D12VideoCapture`
- `MFD3D12VideoCaptureThread`
- `MFD3D12VideoFrame`

## Implemented internal components

- `MFCpuSampleReader`
- `MFVideoFileSampleReader`
- `MFD3D12UploadFramePool`
- `MFD3D12FrameCloner`
- `MFD3D12FrameLeaseControl`

## Supported primary path

```txt
Media Foundation camera/video source
  -> exact CPU output sample: NV12 / P010 / RGB32 / ARGB32
  -> D3D12 upload texture
  -> D3D12Processing conversion
  -> owned native D3D12 R8G8B8A8_UNORM texture
```

## Synchronization model

Producer APIs return frames after GPU command submission by default. Returned frames carry a ready fence/value and expose:

```cpp
bool isReady() const;
void waitReady() const;
UINT64 readyFenceValue() const noexcept;
D3D12_RESOURCE_STATES resourceState() const noexcept;
```

Consumers should call `waitReady()` before using the texture outside the producer path or from a different queue.

## Resource lifetime model

Frames are move-only handles over pooled storage. The pool slot is leased by the returned frame and is released when the frame is destroyed or overwritten. Before reusing a slot, the previous ready fence is respected.

Threaded fan-out does not push the same D3D12 texture to multiple queues. Each queue receives a GPU-copied frame with independent resource lifetime.

## Exact format policy

Camera input uses strict native media type selection. The request must specify:

```txt
width
height
fps numerator/denominator
Media Foundation subtype
```

Video input requests an exact decoded output format from Media Foundation. No best-match fallback is performed.

## Current intentional non-goals

The following are not part of the current D3D12 backend scope:

- D3D11 backend
- D3D11/D3D12 shared GPU sample path
- direct GPU hardware decode surface import
- video writer / encoder
- additional packed/planar formats such as YUY2, UYVY, I420, and YV12

These are extension points, not incomplete stubs in the current implementation.
