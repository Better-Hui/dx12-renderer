#include <Framework/Rendering/Lighting/ReSTIRGI.h>

#include <algorithm>

//Modify Begin:2026-08-10 by BestHui
ReSTIRGI::ReSTIRGI(const ReSTIRGISettings settings)
{
    SetSettings(settings);
}

void ReSTIRGI::SetSettings(const ReSTIRGISettings& settings)
{
    m_Settings = settings;
//Modify Begin:2026-07-30 by BestHui
    m_Settings.InitialCandidateCount = std::clamp(m_Settings.InitialCandidateCount, 1u, 32u);
//Modify End
    m_Settings.TemporalMaxHistoryLength = std::clamp(m_Settings.TemporalMaxHistoryLength, 1u, 1024u);
    m_Settings.SpatialMaxHistoryLength = std::clamp(m_Settings.SpatialMaxHistoryLength, 1u, 1024u);
    m_Settings.MaxSampleAge = std::clamp(m_Settings.MaxSampleAge, 1u, 1024u);
    m_Settings.SpatialNeighborCount = std::clamp(m_Settings.SpatialNeighborCount, 1u, 16u);
    m_Settings.TemporalNormalSimilarityThreshold = std::clamp(m_Settings.TemporalNormalSimilarityThreshold, -1.0f, 1.0f);
    m_Settings.TemporalPositionSimilarityThreshold = std::clamp(m_Settings.TemporalPositionSimilarityThreshold, 0.001f, 10.0f);
    m_Settings.SpatialNormalSimilarityThreshold = std::clamp(m_Settings.SpatialNormalSimilarityThreshold, -1.0f, 1.0f);
    m_Settings.SpatialPositionSimilarityThreshold = std::clamp(m_Settings.SpatialPositionSimilarityThreshold, 0.001f, 10.0f);
    m_Settings.SpatialSamplingRadius = std::clamp(m_Settings.SpatialSamplingRadius, 1.0f, 256.0f);
    m_Settings.MaxJacobian = std::clamp(m_Settings.MaxJacobian, 1.0f, 1000.0f);
    m_Settings.MaxSpatialWeight = std::clamp(m_Settings.MaxSpatialWeight, 0.001f, 100000.0f);
}

const ReSTIRGISettings& ReSTIRGI::GetSettings() const
{
    return m_Settings;
}

ReSTIRGIFrameConstants ReSTIRGI::GetFrameConstants(
    const uint32_t width,
    const uint32_t height,
    const uint32_t frameIndex,
    const bool historyValid,
    const uint32_t maxPathBounces) const
{
    return {
        width,
        height,
        frameIndex,
        historyValid ? 1u : 0u,

//Modify Begin:2026-07-30 by BestHui
        m_Settings.InitialCandidateCount,
//Modify End
        m_Settings.EnableTemporalResampling ? 1u : 0u,
        m_Settings.EnableSpatialResampling ? 1u : 0u,
        m_Settings.EnableSpatialVisibility ? 1u : 0u,
        m_Settings.EnableTemporalJacobian ? 1u : 0u,

        m_Settings.TemporalMaxHistoryLength,
        m_Settings.SpatialMaxHistoryLength,
        m_Settings.MaxSampleAge,
        m_Settings.SpatialNeighborCount,
//Modify Begin:2026-07-30 by BestHui
        m_Settings.EnableUnbiasedSpatialResampling ? 1u : 0u,
        std::max(2u, maxPathBounces),
        0u,
//Modify End

        m_Settings.TemporalNormalSimilarityThreshold,
        m_Settings.TemporalPositionSimilarityThreshold,
        m_Settings.SpatialNormalSimilarityThreshold,
        m_Settings.SpatialPositionSimilarityThreshold,

        m_Settings.SpatialSamplingRadius,
        m_Settings.MaxJacobian,
        m_Settings.MaxSpatialWeight,
//Modify Begin:2026-07-30 by BestHui
        0.0f,
//Modify End
    };
}
//Modify End
