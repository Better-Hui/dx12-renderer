#pragma once

//Modify Begin:2026-08-07 by BestHui
#include <string>

class FrameFeaturesRuntime
{
public:
    virtual ~FrameFeaturesRuntime() = default;

    [[nodiscard]] virtual bool IsInitialized() const = 0;
    [[nodiscard]] virtual bool IsRayReconstructionSupported() const = 0;
    [[nodiscard]] virtual bool IsFrameGenerationSupported() const = 0;
    [[nodiscard]] virtual const std::string& GetStatusMessage() const = 0;
};

class FrameGenerationController
{
public:
    virtual ~FrameGenerationController() = default;

    virtual bool SetFrameGenerationEnabled(bool enabled) = 0;
};
//Modify End
