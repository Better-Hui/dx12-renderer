#pragma once

#include <memory>
#include <string>

//Modify Begin:2026-08-18 by Hui
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

struct FrameFeatureServices
{
    std::shared_ptr<FrameFeaturesRuntime> Runtime;
    std::shared_ptr<FrameGenerationController> FrameGeneration;
};
//Modify End
