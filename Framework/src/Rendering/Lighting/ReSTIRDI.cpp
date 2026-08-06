//Modify Begin:2026-07-30 by BestHui
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
    m_Settings.TemporalMaxHistoryLength = std::clamp(m_Settings.TemporalMaxHistoryLength, 1u, 16383u);
    m_Settings.BoilingFilterStrength = std::clamp(m_Settings.BoilingFilterStrength, 0.01f, 1.0f);
    m_Settings.SpatialNeighborCount = std::clamp(m_Settings.SpatialNeighborCount, 1u, 32u);
    m_Settings.SpatialDisocclusionBoostSampleCount = std::clamp(m_Settings.SpatialDisocclusionBoostSampleCount, 1u, 32u);
    m_Settings.SpatialTargetHistoryLength = std::clamp(m_Settings.SpatialTargetHistoryLength, 0u, 16383u);
    m_Settings.SpatialSamplingRadius = std::clamp(m_Settings.SpatialSamplingRadius, 1.0f, 64.0f);
    m_Settings.TemporalNormalSimilarityThreshold = std::clamp(m_Settings.TemporalNormalSimilarityThreshold, -1.0f, 1.0f);
    m_Settings.TemporalDepthSimilarityThreshold = std::clamp(m_Settings.TemporalDepthSimilarityThreshold, 0.0f, 1.0f);
    m_Settings.SpatialNormalSimilarityThreshold = std::clamp(m_Settings.SpatialNormalSimilarityThreshold, -1.0f, 1.0f);
    m_Settings.SpatialDepthSimilarityThreshold = std::clamp(m_Settings.SpatialDepthSimilarityThreshold, 0.0f, 1.0f);
    m_Settings.SpatialMaterialSimilarityThreshold = std::clamp(m_Settings.SpatialMaterialSimilarityThreshold, 0.0f, 2.0f);
    m_Settings.FinalVisibilityMaxAge = std::clamp(m_Settings.FinalVisibilityMaxAge, 0u, 255u);
    m_Settings.FinalVisibilityMaxDistance = std::clamp(m_Settings.FinalVisibilityMaxDistance, 0.0f, 127.0f);
}

const ReSTIRDISettings& ReSTIRDI::GetSettings() const
{
    return m_Settings;
}

ReSTIRDIFrameConstants ReSTIRDI::GetFrameConstants(const bool historyValid) const
{
    return {
        m_Settings.CandidateCount,
        m_Settings.EnableInitialVisibility ? 1u : 0u,
        m_Settings.EnableTemporalResampling ? 1u : 0u,
        static_cast<uint32_t>(m_Settings.TemporalBiasCorrection),

        historyValid ? 1u : 0u,
        m_Settings.EnableTemporalVisibilityShortcut ? 1u : 0u,
        m_Settings.EnableTemporalPermutationSampling ? 1u : 0u,
        m_Settings.EnableBoilingFilter ? 1u : 0u,

        m_Settings.TemporalMaxHistoryLength,
        m_Settings.EnableSpatialResampling ? 1u : 0u,
        m_Settings.EnableSpatialResampling ? m_Settings.SpatialNeighborCount : 0u,
        m_Settings.EnableSpatialResampling ? m_Settings.SpatialDisocclusionBoostSampleCount : 0u,

        m_Settings.SpatialTargetHistoryLength,
        static_cast<uint32_t>(m_Settings.SpatialBiasCorrection),
        m_Settings.EnableSpatialMaterialSimilarityTest ? 1u : 0u,
        m_Settings.DiscountNaiveSpatialSamples ? 1u : 0u,

        m_Settings.EnableFinalVisibility ? 1u : 0u,
        m_Settings.ReuseFinalVisibility ? 1u : 0u,
        m_Settings.EnableTemporalVisibilityShortcut ? 1u : 0u,
        m_Settings.FinalVisibilityMaxAge,

        m_Settings.BoilingFilterStrength,
        m_Settings.TemporalNormalSimilarityThreshold,
        m_Settings.TemporalDepthSimilarityThreshold,
        0.0f,

        m_Settings.SpatialSamplingRadius,
        m_Settings.SpatialNormalSimilarityThreshold,
        m_Settings.SpatialDepthSimilarityThreshold,
        m_Settings.SpatialMaterialSimilarityThreshold,

        m_Settings.FinalVisibilityMaxDistance,
        0.0f,
        0.0f,
        0.0f,
    };
}
//Modify End
