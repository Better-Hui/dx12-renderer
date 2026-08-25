//Modify Begin:2026-08-25 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <Meshlet/MeshletCommon.hlsli>

cbuffer MeshletCullCBuffer : register(b0)
{
    float4 MeshletCull_FrustumPlanes[6];
    uint MeshletCull_InstanceCount;
    uint MeshletCull_DebugDisableCulling;
    uint2 MeshletCull_Padding0;
};

StructuredBuffer<Meshlet> Meshlets : register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletInstanceData> MeshletInstances : register(t1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletTransformData> MeshletTransforms : register(t2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWStructuredBuffer<MeshletIndirectCommand> MeshletIndirectCommands : register(u0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWByteAddressBuffer MeshletIndirectCount : register(u1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

[numthreads(MeshletCullThreadCount, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint instanceIndex = dispatchThreadId.x;
    if (instanceIndex >= MeshletCull_InstanceCount)
    {
        return;
    }

    const MeshletInstanceData instance = MeshletInstances[instanceIndex];
    const Meshlet meshlet = Meshlets[instance.MeshletIndex];
    const MeshletTransformData transform = MeshletTransforms[instance.TransformIndex];

    const float3 centerWs = mul(float4(meshlet.Bounds.Center, 1.0f), transform.Model).xyz;
    const float radiusWs = meshlet.Bounds.Radius * MeshletGetMaxScale(transform.Model);
    const bool visible = MeshletFrustumCullSphere(MeshletCull_FrustumPlanes, centerWs, radiusWs);
    if (MeshletCull_DebugDisableCulling == 0u && !visible)
    {
        return;
    }

    uint commandIndex = 0;
    MeshletIndirectCount.InterlockedAdd(0, 1, commandIndex);
    MeshletIndirectCommand command;
    command.MeshletInstanceIndex = instanceIndex;
    command.Flags = 0u;
    command.Padding0 = 0u;
    command.Padding1 = 0u;
    command.VertexCountPerInstance = meshlet.IndexCount;
    command.InstanceCount = 1;
    command.StartVertexLocation = 0;
    command.StartInstanceLocation = 0;
    MeshletIndirectCommands[commandIndex] = command;
}
//Modify End
