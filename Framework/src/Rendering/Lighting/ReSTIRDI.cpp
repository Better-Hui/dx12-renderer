//Modify Begin:2026-08-05 by BestHui
#include <Framework/Rendering/Lighting/ReSTIRDI.h>

#include <algorithm>

ReSTIRDI::ReSTIRDI(const ReSTIRDISettings settings)
{
    SetSettings(settings);
}

void ReSTIRDI::SetSettings(const ReSTIRDISettings& settings)
{
    m_Settings = settings;
    m_Settings.CandidateCount = std::clamp(m_Settings.CandidateCount, 1u, 32u);
    m_Settings.SpatialNeighborCount = std::clamp(m_Settings.SpatialNeighborCount, 0u, 16u);
    m_Settings.TemporalMaxHistoryLength = std::clamp(m_Settings.TemporalMaxHistoryLength, 1u, 64u);
    m_Settings.BoilingFilterStrength = std::clamp(m_Settings.BoilingFilterStrength, 0.01f, 1.0f);
    m_Settings.SpatialSamplingRadius = std::clamp(m_Settings.SpatialSamplingRadius, 1.0f, 64.0f);
    m_Settings.TemporalNormalSimilarityThreshold = std::clamp(m_Settings.TemporalNormalSimilarityThreshold, -1.0f, 1.0f);
    m_Settings.SpatialNormalSimilarityThreshold = std::clamp(m_Settings.SpatialNormalSimilarityThreshold, -1.0f, 1.0f);
    m_Settings.DepthSimilarityThreshold = std::clamp(m_Settings.DepthSimilarityThreshold, 0.0f, 1.0f);
    m_Settings.MaterialSimilarityThreshold = std::clamp(m_Settings.MaterialSimilarityThreshold, 0.0f, 2.0f);
}

const ReSTIRDISettings& ReSTIRDI::GetSettings() const
{
    return m_Settings;
}

ReSTIRDIFrameConstants ReSTIRDI::GetFrameConstants(const bool historyValid) const
{
    uint32_t visibilityTestMask = 0u;
    visibilityTestMask |= m_Settings.EnableCandidateVisibility ? 1u << 0u : 0u;
    visibilityTestMask |= m_Settings.EnableTemporalVisibility ? 1u << 1u : 0u;
    visibilityTestMask |= m_Settings.EnableSpatialVisibility ? 1u << 2u : 0u;
    visibilityTestMask |= m_Settings.EnableFinalVisibility ? 1u << 3u : 0u;

    return {
        m_Settings.CandidateCount,
        m_Settings.EnableTemporalResampling ? 1u : 0u,
        m_Settings.EnableSpatialResampling ? m_Settings.SpatialNeighborCount : 0u,
        historyValid ? 1u : 0u,
        m_Settings.EnableBoilingFilter ? 1u : 0u,
        visibilityTestMask,
        m_Settings.TemporalMaxHistoryLength,
        m_Settings.BoilingFilterStrength,
        m_Settings.SpatialSamplingRadius,
        m_Settings.TemporalNormalSimilarityThreshold,
        m_Settings.SpatialNormalSimilarityThreshold,
        m_Settings.DepthSimilarityThreshold,
        m_Settings.MaterialSimilarityThreshold,
        0.0f,
        0.0f,
    };
}
//Modify End
