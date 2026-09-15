//Modify Begin:2026-09-15 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <Meshlet/MeshletCommon.hlsli>

StructuredBuffer<Meshlet> Meshlets : register(t2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletTransformData> MeshletTransforms : register(t3, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletInstanceData> MeshletInstances : register(t4, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
ByteAddressBuffer MeshletVisibleCount : register(t6, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
groupshared MeshletTaskPayload Payload;
[numthreads(MeshletTaskGroupSize, 1, 1)]
void main(uint groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    const uint visibleCount = MeshletVisibleCount.Load(0u);
    const uint localVisibleCount = min(MeshletTaskGroupSize, visibleCount > groupId.x * MeshletTaskGroupSize ? visibleCount - groupId.x * MeshletTaskGroupSize : 0u);
    if (groupThreadId == 0u)
    {
        Payload.MeshletCount = localVisibleCount;
    }
    GroupMemoryBarrierWithGroupSync();
    const uint instanceIndex = groupId.x * MeshletTaskGroupSize + groupThreadId;
    if (groupThreadId < localVisibleCount)
    {
        Payload.MeshletInstanceIndices[groupThreadId] = instanceIndex;
    }
    DispatchMesh(max(localVisibleCount, 1u), 1, 1, Payload);
}
//Modify End
