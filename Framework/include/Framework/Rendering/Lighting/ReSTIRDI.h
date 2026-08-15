#pragma once

#include <cstdint>

//Modify Begin:2026-07-30 by BestHui
enum class ReSTIRDITemporalBiasCorrectionMode : uint32_t
{
    Off = 0,
    Basic = 1,
    RayTraced = 2,
};

enum class ReSTIRDISpatialBiasCorrectionMode : uint32_t
{
    Off = 0,
    Basic = 1,
    Pairwise = 2,
    RayTraced = 3,
};

struct ReSTIRDISettings
{
    uint32_t CandidateCount = 8;
    bool EnableInitialVisibility = true;

    bool EnableTemporalResampling = true;
    ReSTIRDITemporalBiasCorrectionMode TemporalBiasCorrection = ReSTIRDITemporalBiasCorrectionMode::Basic;
    uint32_t TemporalMaxHistoryLength = 20;
    bool EnableTemporalVisibilityShortcut = false;
    bool EnableTemporalPermutationSampling = true;
    float TemporalNormalSimilarityThreshold = 0.5f;
    float TemporalDepthSimilarityThreshold = 0.1f;

    bool EnableBoilingFilter = true;
    float BoilingFilterStrength = 0.2f;

    bool EnableSpatialResampling = true;
    ReSTIRDISpatialBiasCorrectionMode SpatialBiasCorrection = ReSTIRDISpatialBiasCorrectionMode::Basic;
    uint32_t SpatialNeighborCount = 1;
    uint32_t SpatialDisocclusionBoostSampleCount = 8;
    uint32_t SpatialTargetHistoryLength = 0;
    float SpatialSamplingRadius = 32.0f;
    float SpatialNormalSimilarityThreshold = 0.5f;
    float SpatialDepthSimilarityThreshold = 0.1f;
    bool EnableSpatialMaterialSimilarityTest = true;
    float SpatialMaterialSimilarityThreshold = 0.5f;
    bool DiscountNaiveSpatialSamples = true;

    bool EnableFinalVisibility = true;
    bool ReuseFinalVisibility = true;
    bool DiscardInvisibleFinalSamples = false;
    uint32_t FinalVisibilityMaxAge = 4;
    float FinalVisibilityMaxDistance = 16.0f;
};

struct ReSTIRDIFrameConstants
{
    uint32_t CandidateCount = 1;
    uint32_t InitialVisibilityEnabled = 0;
    uint32_t TemporalResamplingEnabled = 0;
    uint32_t TemporalBiasCorrectionMode = 0;

    uint32_t HistoryValid = 0;
    uint32_t TemporalVisibilityShortcutEnabled = 0;
    uint32_t TemporalPermutationSamplingEnabled = 0;
    uint32_t BoilingFilterEnabled = 0;

    uint32_t TemporalMaxHistoryLength = 1;
    uint32_t SpatialResamplingEnabled = 0;
    uint32_t SpatialNeighborCount = 0;
    uint32_t SpatialDisocclusionBoostSampleCount = 0;

    uint32_t SpatialTargetHistoryLength = 0;
    uint32_t SpatialBiasCorrectionMode = 0;
    uint32_t SpatialMaterialSimilarityTestEnabled = 0;
    uint32_t SpatialDiscountNaiveSamples = 0;

    uint32_t FinalVisibilityEnabled = 1;
    uint32_t FinalVisibilityReuseEnabled = 0;
    uint32_t FinalVisibilityDiscardInvisibleSamples = 0;
    uint32_t FinalVisibilityMaxAge = 0;

    float BoilingFilterStrength = 0.2f;
    float TemporalNormalSimilarityThreshold = 0.5f;
    float TemporalDepthSimilarityThreshold = 0.1f;
    float Padding0 = 0.0f;

    float SpatialSamplingRadius = 1.0f;
    float SpatialNormalSimilarityThreshold = 0.5f;
    float SpatialDepthSimilarityThreshold = 0.1f;
    float SpatialMaterialSimilarityThreshold = 0.5f;

    float FinalVisibilityMaxDistance = 1.0f;
    float Padding1 = 0.0f;
    float Padding2 = 0.0f;
    float Padding3 = 0.0f;
};

class ReSTIRDI final
{
public:
    explicit ReSTIRDI(ReSTIRDISettings settings = {});

    void SetSettings(const ReSTIRDISettings& settings);
    const ReSTIRDISettings& GetSettings() const;
    ReSTIRDIFrameConstants GetFrameConstants(bool historyValid) const;

private:
    ReSTIRDISettings m_Settings;
};
//Modify End
