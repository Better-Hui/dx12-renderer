//Modify Begin:2026-08-23 by Hui
#pragma once

#include <cstdint>
#include <memory>

class ByteAddressBuffer;
class CommandList;
class ComputeShader;
class FrameworkDeviceContext;
class Texture;

class AutoExposure final
{
public:
//Modify Begin:2026-08-23 by Hui
    struct Settings
    {
        bool Enabled = true;
        float Tau = 1.1f;
        float MinLogLuminance = -10.0f;
        float MaxLogLuminance = 2.0f;
    };
//Modify End

    explicit AutoExposure(FrameworkDeviceContext& deviceContext);

//Modify Begin:2026-08-23 by Hui
    void SetSettings(const Settings& settings);
    const Settings& GetSettings() const;
//Modify End

    void Execute(
        CommandList& commandList,
        const std::shared_ptr<Texture>& source,
        const std::shared_ptr<Texture>& output,
        uint32_t inputWidth,
        uint32_t inputHeight,
        uint32_t outputWidth,
        uint32_t outputHeight,
        float deltaTime);

    void ResetHistory();

private:
    void EnsureResources(uint32_t outputWidth, uint32_t outputHeight);

    FrameworkDeviceContext& m_DeviceContext;
    std::unique_ptr<ComputeShader> m_BuildHistogramShader;
    std::unique_ptr<ComputeShader> m_AverageHistogramShader;
    std::unique_ptr<ComputeShader> m_ApplyShader;
    std::shared_ptr<ByteAddressBuffer> m_Histogram;
    std::shared_ptr<Texture> m_AdaptedLuminance;
    uint32_t m_OutputWidth = 0;
    uint32_t m_OutputHeight = 0;
    bool m_HistoryValid = false;
//Modify Begin:2026-08-23 by Hui
    Settings m_Settings;
//Modify End
};
//Modify End
