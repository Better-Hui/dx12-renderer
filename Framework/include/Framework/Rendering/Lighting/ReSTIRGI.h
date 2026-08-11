#pragma once

#include <cstdint>

//Modify Begin:2026-08-10 by BestHui
struct ReSTIRGISettings
{
//Modify Begin:2026-07-30 by BestHui
    uint32_t InitialCandidateCount = 1;
//Modify End
    bool EnableTemporalResampling = true;
    bool EnableSpatialResampling = true;
//Modify Begin:2026-07-30 by BestHui
    bool EnableUnbiasedSpatialResampling = false;
//Modify End
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

struct ReSTIRGIFrameConstants
{
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t FrameIndex = 0;
    uint32_t HistoryValid = 0;

//Modify Begin:2026-07-30 by BestHui
    uint32_t InitialCandidateCount = 1;
//Modify End
    uint32_t TemporalResamplingEnabled = 1;
    uint32_t SpatialResamplingEnabled = 1;
    uint32_t TemporalJacobianEnabled = 1;

    uint32_t TemporalMaxHistoryLength = 20;
    uint32_t SpatialMaxHistoryLength = 32;
    uint32_t MaxSampleAge = 20;
    uint32_t SpatialNeighborCount = 5;
//Modify Begin:2026-07-30 by BestHui
    uint32_t SpatialUnbiasedResamplingEnabled = 1;
    uint32_t Padding0 = 0;
    uint32_t Padding1 = 0;
    uint32_t Padding2 = 0;
//Modify End

    float TemporalNormalSimilarityThreshold = 0.8f;
    float TemporalPositionSimilarityThreshold = 0.1f;
    float SpatialNormalSimilarityThreshold = 0.7f;
    float SpatialPositionSimilarityThreshold = 0.2f;

    float SpatialSamplingRadius = 30.0f;
    float MaxJacobian = 10.0f;
    float MaxSpatialWeight = 32.0f;
//Modify Begin:2026-07-30 by BestHui
    float Padding3 = 0.0f;
//Modify End
};
//Modify Begin:2026-07-30 by BestHui
static_assert(sizeof(ReSTIRGIFrameConstants) == 96u);
//Modify End

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

private:
    ReSTIRGISettings m_Settings;
};
//Modify End
