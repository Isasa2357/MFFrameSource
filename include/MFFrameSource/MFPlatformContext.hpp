#pragma once

#include "MFCommon.hpp"

namespace MFFrameSource {

class MFPlatformContext {
public:
    MFPlatformContext();
    ~MFPlatformContext();

    MFPlatformContext(const MFPlatformContext&) = delete;
    MFPlatformContext& operator=(const MFPlatformContext&) = delete;

    bool initialized() const noexcept { return initialized_; }

private:
    bool coInitialized_ = false;
    bool mfStarted_ = false;
    bool initialized_ = false;
};

} // namespace MFFrameSource
