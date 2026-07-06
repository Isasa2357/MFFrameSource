#pragma once

#include <condition_variable>
#include <mutex>

namespace MFFrameSource::internal {

struct MFD3D12FrameLeaseControl {
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool closing = false;
};

} // namespace MFFrameSource::internal
