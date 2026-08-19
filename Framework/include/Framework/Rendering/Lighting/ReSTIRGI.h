#pragma once

#include <cstdint>

//Modify Begin:2026-08-11 by Hui
struct ReSTIRGISettings
{
    uint32_t InitialCandidateCount = 1;
    bool EnableTemporalResampling = true;
    bool EnableSpatialResampling = true;
    bool EnableRayTracedSpatialBiasCorrection = false;
    bool EnableTemporalJacobian = true;
    uint32_t TemporalMaxHistoryLength = 20;
    uint32_t SpatialMaxHistoryLength = 32;
    uint32_t MaxSampleAge = 20;
    uint32_t SpatialNeighborCount = 5;
    float TemporalNormalSimilarityThreshold = 0.8f;
    float TemporalPositionSimilarityThreshold = 0.1f;
    float SpatialNormalSimilarityThreshold = 0.7f;
    float SpatialPositionSimilarityThreshold = 0.2f;
    float SpatialSamplingRadius = 30.0f;
    float MaxJacobian = 10.0f;
    float MaxSpatialWeight = 32.0f;
};

struct ReSTIRGIVariantConfig
{
    uint32_t MaxPathBounces = 1;
    bool EnableTemporalResampling = true;
    bool EnableSpatialResampling = true;
    bool EnableTemporalJacobian = true;
    bool EnableRayTracedSpatialBiasCorrection = false;
};

struct ReSTIRGIFrameConstants
{
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t FrameIndex = 0;
    uint32_t HistoryValid = 0;

    uint32_t InitialCandidateCount = 1;
    uint32_t TemporalMaxHistoryLength = 20;
    uint32_t SpatialMaxHistoryLength = 32;
    uint32_t MaxSampleAge = 20;
    uint32_t SpatialNeighborCount = 5;
    uint32_t Padding0 = 0;
    uint32_t Padding1 = 0;
    uint32_t Padding2 = 0;

    float TemporalNormalSimilarityThreshold = 0.8f;
    float TemporalPositionSimilarityThreshold = 0.1f;
    float SpatialNormalSimilarityThreshold = 0.7f;
    float SpatialPositionSimilarityThreshold = 0.2f;

    float SpatialSamplingRadius = 30.0f;
    float MaxJacobian = 10.0f;
    float MaxSpatialWeight = 32.0f;
    float Padding3 = 0.0f;
};
static_assert(sizeof(ReSTIRGIFrameConstants) == 80u);

class ReSTIRGI final
{
public:
    explicit ReSTIRGI(ReSTIRGISettings settings = {});

    void SetSettings(const ReSTIRGISettings& settings);
    const ReSTIRGISettings& GetSettings() const;
    ReSTIRGIFrameConstants GetFrameConstants(
        uint32_t width,
        uint32_t height,
        uint32_t frameIndex,
        bool historyValid) const;
    ReSTIRGIVariantConfig GetVariantConfig(uint32_t maxPathBounces) const;

private:
    ReSTIRGISettings m_Settings;
};
//Modify End
