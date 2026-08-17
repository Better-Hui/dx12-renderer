//Modify Begin:2026-07-27 by Hui
#pragma once

#include <cstdint>
#include <memory>

#include <DirectXMath.h>
#include <d3d12.h>

class CommandList;
class ComputeShader;
//Modify Begin:2026-07-30 by Hui
class FrameworkDeviceContext;
//Modify End
class Resource;
class Texture;

class NRD
{
public:
    struct FrameMatrices
    {
        DirectX::XMMATRIX WorldToView = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX ViewToClip = DirectX::XMMatrixIdentity();
    };

    enum class DenoiserMode : uint32_t
    {
        RelaxDiffuse = 0,
        ReblurDiffuse = 1,
    };

    struct Settings
    {
        DenoiserMode Mode = DenoiserMode::ReblurDiffuse;
        float DenoisingRange = 100000.0f;
        float ReblurHitDistanceA = 3.0f;
        float ReblurHitDistanceB = 0.1f;
        float ReblurHitDistanceC = 20.0f;

        uint32_t RelaxDiffuseMaxAccumulatedFrameNum = 32;
        uint32_t RelaxDiffuseMaxFastAccumulatedFrameNum = 6;
        uint32_t RelaxHistoryFixFrameNum = 3;
        uint32_t RelaxHistoryFixBasePixelStride = 14;
        float RelaxFastHistoryClampingSigmaScale = 2.0f;
        float RelaxDiffusePrepassBlurRadius = 30.0f;
        float RelaxMinHitDistanceWeight = 0.1f;
        uint32_t RelaxSpatialVarianceEstimationHistoryThreshold = 3;
        float RelaxDiffusePhiLuminance = 2.0f;
        float RelaxLobeAngleFraction = 0.5f;
        float RelaxRoughnessFraction = 0.15f;
        uint32_t RelaxAtrousIterationNum = 5;
        float RelaxDepthThreshold = 0.003f;
        float RelaxLuminanceEdgeStoppingRelaxation = 0.5f;
        float RelaxNormalEdgeStoppingRelaxation = 0.3f;
        float RelaxRoughnessEdgeStoppingRelaxation = 1.0f;
        bool RelaxEnableAntiFirefly = false;
        bool RelaxEnableRoughnessEdgeStopping = true;

        uint32_t ReblurMaxAccumulatedFrameNum = 30;
        uint32_t ReblurMaxFastAccumulatedFrameNum = 6;
        uint32_t ReblurHistoryFixFrameNum = 3;
        uint32_t ReblurHistoryFixBasePixelStride = 14;
        float ReblurFastHistoryClampingSigmaScale = 2.0f;
        float ReblurDiffusePrepassBlurRadius = 30.0f;
        float ReblurMinHitDistanceWeight = 0.1f;
        float ReblurMinBlurRadius = 1.0f;
        float ReblurMaxBlurRadius = 30.0f;
        float ReblurLobeAngleFraction = 0.15f;
        float ReblurRoughnessFraction = 0.15f;
        float ReblurPlaneDistanceSensitivity = 0.02f;
        float ReblurFireflySuppressorMinRelativeScale = 2.0f;
        bool ReblurEnableAntiFirefly = true;
    };

    explicit NRD(FrameworkDeviceContext& deviceContext);
    ~NRD();

    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }
    Settings& GetSettings() { return m_Settings; }
    const Settings& GetSettings() const { return m_Settings; }
    void ResetHistory();

    void PrepareDenoiserInputs(
        CommandList& commandList,
        const FrameMatrices& frameMatrices,
        const std::shared_ptr<Texture>& gBufferSpecularSmoothness,
        const std::shared_ptr<Texture>& gBufferNormal,
        const std::shared_ptr<Texture>& gBufferPosition,
        const std::shared_ptr<Texture>& depthTexture,
        const std::shared_ptr<Texture>& motionVector,
        const std::shared_ptr<Texture>& nrdNormalRoughness,
        const std::shared_ptr<Texture>& nrdViewZ,
        const std::shared_ptr<Texture>& nrdMotion,
        uint32_t width,
        uint32_t height);

    void Execute(
        CommandList& commandList,
        const FrameMatrices& frameMatrices,
        const std::shared_ptr<Texture>& noisyRadiance,
        const std::shared_ptr<Texture>& gBufferAlbedoOcclusion,
        const std::shared_ptr<Texture>& gBufferEmissionMetallic,
        const std::shared_ptr<Texture>& depthTexture,
        const std::shared_ptr<Texture>& nrdNormalRoughness,
        const std::shared_ptr<Texture>& nrdViewZ,
        const std::shared_ptr<Texture>& nrdMotion,
        const std::shared_ptr<Texture>& denoisedRadiance,
        const std::shared_ptr<Texture>& output,
        uint32_t width,
        uint32_t height);

private:
    struct PrepareConstants
    {
        DirectX::XMMATRIX WorldToView = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX PreviousWorldToView = DirectX::XMMatrixIdentity();
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
    };

    struct CompositeConstants
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t DenoiserMode = 0;
        uint32_t Padding1 = 0;
    };

    bool EnsureCreated(uint32_t width, uint32_t height);
    void PrepareInputs(
        CommandList& commandList,
        const FrameMatrices& frameMatrices,
        const std::shared_ptr<Texture>& gBufferSpecularSmoothness,
        const std::shared_ptr<Texture>& gBufferNormal,
        const std::shared_ptr<Texture>& gBufferPosition,
        const std::shared_ptr<Texture>& depthTexture,
        const std::shared_ptr<Texture>& motionVector,
        const std::shared_ptr<Texture>& nrdNormalRoughness,
        const std::shared_ptr<Texture>& nrdViewZ,
        const std::shared_ptr<Texture>& nrdMotion,
        uint32_t width,
        uint32_t height);
    void Denoise(
        CommandList& commandList,
        const FrameMatrices& frameMatrices,
        const std::shared_ptr<Texture>& noisyRadiance,
        const std::shared_ptr<Texture>& nrdNormalRoughness,
        const std::shared_ptr<Texture>& nrdViewZ,
        const std::shared_ptr<Texture>& nrdMotion,
        const std::shared_ptr<Texture>& denoisedRadiance,
        uint32_t width,
        uint32_t height);
    void Composite(
        CommandList& commandList,
        const std::shared_ptr<Texture>& denoisedRadiance,
        const std::shared_ptr<Texture>& depthTexture,
        const std::shared_ptr<Texture>& gBufferAlbedoOcclusion,
        const std::shared_ptr<Texture>& gBufferEmissionMetallic,
        const std::shared_ptr<Texture>& output,
        uint32_t width,
        uint32_t height);

    //Modify Begin:2026-07-30 by Hui
    FrameworkDeviceContext& m_DeviceContext;
    //Modify End
    std::unique_ptr<ComputeShader> m_PrepareShader;
    std::unique_ptr<ComputeShader> m_CompositeShader;
    std::unique_ptr<class NRDImpl> m_Impl;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_FrameIndex = 0;
    DirectX::XMMATRIX m_PreviousView = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX m_PreviousProjection = DirectX::XMMatrixIdentity();
    DenoiserMode m_CreatedMode = DenoiserMode::ReblurDiffuse;
    Settings m_Settings = {};
    bool m_Enabled = false;
    bool m_HasPreviousFrame = false;
};
//Modify End
