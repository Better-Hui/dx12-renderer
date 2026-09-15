//Modify Begin:2026-09-15 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <Meshlet/MeshletCommon.hlsli>

cbuffer MeshletCullCBuffer : register(b0)
{
    float4 MeshletCull_FrustumPlanes[6];
    uint MeshletCull_DrawCount;
    uint MeshletCull_CandidateCapacity;
    uint MeshletCull_BackendGroupSize;
    uint MeshletCull_DebugDisableCulling;
    uint MeshletCull_EnableInstanceCull;
};

StructuredBuffer<MeshletDrawData> MeshletDraws : register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletTransformData> MeshletTransforms : register(t1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWStructuredBuffer<uint> MeshletVisibleDrawIndices : register(u0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWByteAddressBuffer MeshletCandidateExpandDispatchArguments : register(u1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

[numthreads(MeshletCullThreadCount, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x == 0u)
    {
        MeshletCandidateExpandDispatchArguments.Store(4u, 1u);
        MeshletCandidateExpandDispatchArguments.Store(8u, 1u);
    }
    const uint drawIndex = dispatchThreadId.x;
    if (drawIndex >= MeshletCull_DrawCount)
    {
        return;
    }

    const MeshletDrawData draw = MeshletDraws[drawIndex];
    const MeshletTransformData transform = MeshletTransforms[draw.TransformIndex];
    const float3 centerWs = mul(float4(draw.BoundsCenter, 1.0f), transform.Model).xyz;
    const float radiusWs = draw.BoundsRadius * MeshletGetMaxScale(transform.Model);
    const bool visible = MeshletFrustumCullSphere(MeshletCull_FrustumPlanes, centerWs, radiusWs);
    if (MeshletCull_DebugDisableCulling == 0u && MeshletCull_EnableInstanceCull != 0u && !visible)
    {
        return;
    }
    uint visibleDrawIndex = 0u;
    MeshletCandidateExpandDispatchArguments.InterlockedAdd(0u, 1u, visibleDrawIndex);
    MeshletVisibleDrawIndices[visibleDrawIndex] = drawIndex;
}
//Modify End
