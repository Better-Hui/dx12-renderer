//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <d3d12.h>

class CommandList;
class ComputeShader;
class Texture;

class SVGF
{
public:
    struct Settings
    {
        uint32_t AtrousIterations = 1;
        float TemporalAlpha = 0.08f;
        float MomentsAlpha = 0.2f;
        float PhiColor = 4.0f;
        float PhiNormal = 64.0f;
        float PhiDepth = 1.0f;
    };

    SVGF();
    ~SVGF();

    Settings& GetSettings() { return m_Settings; }
    const Settings& GetSettings() const { return m_Settings; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }
    void ResetHistory();

    void Execute(
        CommandList& commandList,
        const std::shared_ptr<Texture>& noisyRadiance,
        const std::shared_ptr<Texture>& gBufferNormal,
        const std::shared_ptr<Texture>& gBufferPosition,
        const std::shared_ptr<Texture>& motionVector,
        const std::shared_ptr<Texture>& depthTexture,
        const std::shared_ptr<Texture>& output,
        uint32_t width,
        uint32_t height);

private:
    struct TemporalConstants
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t ResetHistory = 1;
        uint32_t Padding1 = 0;
        float TemporalAlpha = 0.08f;
        float MomentsAlpha = 0.2f;
        float PhiNormal = 64.0f;
        float PhiDepth = 1.0f;
    };

    struct AtrousConstants
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t StepSize = 1;
//Modify Begin:2026-07-27 by BestHui
        uint32_t Direction = 0;
//Modify End
        float PhiColor = 4.0f;
        float PhiNormal = 64.0f;
        float PhiDepth = 1.0f;
        float Padding1 = 0.0f;
    };

    struct CompositeConstants
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
    };

    bool EnsureCreated(uint32_t width, uint32_t height);
    void Temporal(
        CommandList& commandList,
        const std::shared_ptr<Texture>& noisyRadiance,
        const std::shared_ptr<Texture>& gBufferNormal,
        const std::shared_ptr<Texture>& gBufferPosition,
        const std::shared_ptr<Texture>& motionVector,
        const std::shared_ptr<Texture>& depthTexture,
        uint32_t width,
        uint32_t height);
    std::shared_ptr<Texture> Atrous(
        CommandList& commandList,
        const std::shared_ptr<Texture>& gBufferNormal,
        const std::shared_ptr<Texture>& gBufferPosition,
        const std::shared_ptr<Texture>& depthTexture,
        uint32_t width,
        uint32_t height);
//Modify Begin:2026-07-27 by BestHui
    void AtrousPass(
        CommandList& commandList,
        const std::shared_ptr<Texture>& input,
        const std::shared_ptr<Texture>& output,
        const std::shared_ptr<Texture>& gBufferNormal,
        const std::shared_ptr<Texture>& gBufferPosition,
        const std::shared_ptr<Texture>& depthTexture,
        uint32_t width,
        uint32_t height,
        uint32_t stepSize,
        uint32_t direction);
//Modify End
    void Composite(
        CommandList& commandList,
        const std::shared_ptr<Texture>& input,
        const std::shared_ptr<Texture>& depthTexture,
        const std::shared_ptr<Texture>& output,
        uint32_t width,
        uint32_t height);

    std::unique_ptr<ComputeShader> m_TemporalShader;
    std::unique_ptr<ComputeShader> m_AtrousShader;
    std::unique_ptr<ComputeShader> m_CompositeShader;

    Settings m_Settings = {};
    bool m_Enabled = false;
    bool m_HistoryValid = false;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_HistoryIndex = 0;

    std::shared_ptr<Texture> m_HistoryColor[2];
    std::shared_ptr<Texture> m_HistoryMoments[2];
    std::shared_ptr<Texture> m_TemporalColor;
    std::shared_ptr<Texture> m_TemporalMoments;
    std::shared_ptr<Texture> m_Variance;
    std::shared_ptr<Texture> m_AtrousPing;
    std::shared_ptr<Texture> m_AtrousPong;
};
//Modify End
