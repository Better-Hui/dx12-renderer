#ifndef FRAMEWORK_RESTIR_DI_CONSTANTS_HLSLI
#define FRAMEWORK_RESTIR_DI_CONSTANTS_HLSLI

//Modify Begin:2026-08-11 by Hui
#ifndef RESTIR_DI_USE_INITIAL_VISIBILITY
#define RESTIR_DI_USE_INITIAL_VISIBILITY 0
#endif

#ifndef RESTIR_DI_USE_TEMPORAL_REUSE
#define RESTIR_DI_USE_TEMPORAL_REUSE 0
#endif

#ifndef RESTIR_DI_TEMPORAL_BIAS_MODE
#define RESTIR_DI_TEMPORAL_BIAS_MODE 0
#endif

#ifndef RESTIR_DI_USE_TEMPORAL_VISIBILITY_SHORTCUT
#define RESTIR_DI_USE_TEMPORAL_VISIBILITY_SHORTCUT 0
#endif

#ifndef RESTIR_DI_USE_TEMPORAL_PERMUTATION_SAMPLING
#define RESTIR_DI_USE_TEMPORAL_PERMUTATION_SAMPLING 0
#endif

#ifndef RESTIR_DI_TEMPORAL_IGNORE_GEOMETRY
#define RESTIR_DI_TEMPORAL_IGNORE_GEOMETRY 0
#endif

#ifndef RESTIR_DI_USE_TEMPORAL_BOILING_FILTER
#define RESTIR_DI_USE_TEMPORAL_BOILING_FILTER 0
#endif

#ifndef RESTIR_DI_USE_SPATIAL_REUSE
#define RESTIR_DI_USE_SPATIAL_REUSE 0
#endif

#ifndef RESTIR_DI_SPATIAL_BIAS_MODE
#define RESTIR_DI_SPATIAL_BIAS_MODE 0
#endif

#ifndef RESTIR_DI_USE_SPATIAL_MATERIAL_SIMILARITY
#define RESTIR_DI_USE_SPATIAL_MATERIAL_SIMILARITY 0
#endif

#ifndef RESTIR_DI_USE_FINAL_VISIBILITY
#define RESTIR_DI_USE_FINAL_VISIBILITY 0
#endif

#ifndef RESTIR_DI_USE_FINAL_VISIBILITY_REUSE
#define RESTIR_DI_USE_FINAL_VISIBILITY_REUSE 0
#endif

#ifndef RESTIR_DI_DISCARD_INVISIBLE_FINAL_SAMPLES
#define RESTIR_DI_DISCARD_INVISIBLE_FINAL_SAMPLES 0
#endif

// RTXDI-compatible geometric reuse thresholds. These are intentionally shader constants:
// changing them changes the statistical validity/quality of the reuse heuristic and should
// not be exposed as a per-frame UI control.
#ifndef RESTIR_DI_TEMPORAL_NORMAL_SIMILARITY_THRESHOLD
#define RESTIR_DI_TEMPORAL_NORMAL_SIMILARITY_THRESHOLD 0.5f
#endif

#ifndef RESTIR_DI_TEMPORAL_DEPTH_SIMILARITY_THRESHOLD
#define RESTIR_DI_TEMPORAL_DEPTH_SIMILARITY_THRESHOLD 0.1f
#endif

#ifndef RESTIR_DI_SPATIAL_NORMAL_SIMILARITY_THRESHOLD
#define RESTIR_DI_SPATIAL_NORMAL_SIMILARITY_THRESHOLD 0.9f
#endif

#ifndef RESTIR_DI_SPATIAL_DEPTH_SIMILARITY_THRESHOLD
#define RESTIR_DI_SPATIAL_DEPTH_SIMILARITY_THRESHOLD 0.1f
#endif
//Modify End

//Modify Begin:2026-07-30 by Hui
cbuffer ReSTIRDIConstants : register(b1)
{
    uint ReSTIRDI_CandidateCount;
    uint ReSTIRDI_InitialVisibilityEnabled;
    uint ReSTIRDI_TemporalResamplingEnabled;
    uint ReSTIRDI_TemporalBiasCorrectionMode;

    uint ReSTIRDI_HistoryValid;
    uint ReSTIRDI_TemporalVisibilityShortcutEnabled;
    uint ReSTIRDI_TemporalPermutationSamplingEnabled;
    uint ReSTIRDI_TemporalMaterialSimilarityTestEnabled;
    uint ReSTIRDI_BoilingFilterEnabled;

    uint ReSTIRDI_TemporalMaxHistoryLength;
    uint ReSTIRDI_SpatialResamplingEnabled;
    uint ReSTIRDI_SpatialNeighborCount;
    uint ReSTIRDI_SpatialDisocclusionBoostSampleCount;

    uint ReSTIRDI_SpatialTargetHistoryLength;
    uint ReSTIRDI_SpatialBiasCorrectionMode;
    uint ReSTIRDI_SpatialMaterialSimilarityTestEnabled;
    uint ReSTIRDI_FinalVisibilityEnabled;
    uint ReSTIRDI_FinalVisibilityReuseEnabled;
    uint ReSTIRDI_FinalVisibilityDiscardInvisibleSamples;
    uint ReSTIRDI_FinalVisibilityMaxAge;

    float ReSTIRDI_BoilingFilterStrength;
    float ReSTIRDI_TemporalNormalSimilarityThreshold;
    float ReSTIRDI_TemporalDepthSimilarityThreshold;
    float ReSTIRDI_TemporalMaterialSimilarityThreshold;

    float ReSTIRDI_SpatialSamplingRadius;
    float ReSTIRDI_SpatialNormalSimilarityThreshold;
    float ReSTIRDI_SpatialDepthSimilarityThreshold;
    float ReSTIRDI_SpatialMaterialSimilarityThreshold;

    float ReSTIRDI_FinalVisibilityMaxDistance;
    float ReSTIRDI_Padding1;
    float ReSTIRDI_Padding2;
    float ReSTIRDI_Padding3;
    uint ReSTIRDI_TemporalIgnoreGeometryEnabled;
};
//Modify End

#endif
