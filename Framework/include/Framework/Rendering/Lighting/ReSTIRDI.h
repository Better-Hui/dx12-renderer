#pragma once

#include <cstdint>

//Modify Begin:2026-08-05 by BestHui
struct ReSTIRDISettings
{
    uint32_t CandidateCount = 8;
    bool EnableTemporalResampling = true;
    bool EnableSpatialResampling = true;
    bool EnableBoilingFilter = false;
    bool EnableCandidateVisibility = false;
    bool EnableTemporalVisibility = false;
    bool EnableSpatialVisibility = false;
    bool EnableFinalVisibility = true;
    uint32_t SpatialNeighborCount = 1;
    uint32_t TemporalMaxHistoryLength = 20;
    float BoilingFilterStrength = 0.2f;
    float SpatialSamplingRadius = 32.0f;
    float TemporalNormalSimilarityThreshold = 0.5f;
    float SpatialNormalSimilarityThreshold = 0.5f;
    float DepthSimilarityThreshold = 0.1f;
    float MaterialSimilarityThreshold = 0.5f;
};

struct ReSTIRDIFrameConstants
{
    uint32_t CandidateCount = 1;
    uint32_t TemporalResamplingEnabled = 0;
    uint32_t SpatialNeighborCount = 1;
    uint32_t HistoryValid = 0;
    uint32_t BoilingFilterEnabled = 0;
    uint32_t VisibilityTestMask = 0;
    uint32_t TemporalMaxHistoryLength = 1;
    float BoilingFilterStrength = 0.2f;
    float SpatialSamplingRadius = 1.0f;
    float TemporalNormalSimilarityThreshold = 0.5f;
    float SpatialNormalSimilarityThreshold = 0.9f;
    float DepthSimilarityThreshold = 0.1f;
    float MaterialSimilarityThreshold = 0.5f;
    float Padding0 = 0.0f;
    float Padding1 = 0.0f;
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
