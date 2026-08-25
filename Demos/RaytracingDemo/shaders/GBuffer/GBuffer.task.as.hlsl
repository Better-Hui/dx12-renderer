//Modify Begin:2026-08-25 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <Meshlet/MeshletCommon.hlsli>

cbuffer MeshletCullCBuffer : register(b1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
{
    float4 MeshletCull_FrustumPlanes[6];
    uint MeshletCull_InstanceCount;
    uint MeshletCull_DebugDisableCulling;
    uint2 MeshletCull_Padding0;
};

StructuredBuffer<Meshlet> Meshlets : register(t2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletTransformData> MeshletTransforms : register(t3, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletInstanceData> MeshletInstances : register(t4, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

groupshared uint VisibleCount;
groupshared MeshletTaskPayload Payload;

[numthreads(MeshletTaskGroupSize, 1, 1)]
void main(
    uint groupThreadId : SV_GroupThreadID,
    uint groupId : SV_GroupID)
{
    if (groupThreadId == 0)
    {
        VisibleCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint instanceIndex = groupId * MeshletTaskGroupSize + groupThreadId;
    if (instanceIndex < MeshletCull_InstanceCount)
    {
        const MeshletInstanceData instance = MeshletInstances[instanceIndex];
        const Meshlet meshlet = Meshlets[instance.MeshletIndex];
        const MeshletTransformData transform = MeshletTransforms[instance.TransformIndex];

        const float3 centerWs = mul(float4(meshlet.Bounds.Center, 1.0f), transform.Model).xyz;
        const float radiusWs = meshlet.Bounds.Radius * MeshletGetMaxScale(transform.Model);
        if (MeshletCull_DebugDisableCulling != 0u || MeshletFrustumCullSphere(MeshletCull_FrustumPlanes, centerWs, radiusWs))
        {
            uint payloadIndex = 0;
            InterlockedAdd(VisibleCount, 1, payloadIndex);
            Payload.MeshletInstanceIndices[payloadIndex] = instanceIndex;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    DispatchMesh(VisibleCount, 1, 1, Payload);
}
//Modify End
