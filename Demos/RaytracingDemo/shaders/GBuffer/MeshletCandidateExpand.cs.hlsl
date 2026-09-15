//Modify Begin:2026-09-15 by Hui
#include <ShaderLibrary/Common/RootSignature.hlsli>
#include <Meshlet/MeshletCommon.hlsli>

StructuredBuffer<MeshletDrawData> MeshletDraws : register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<uint> MeshletVisibleDrawIndices : register(t1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWStructuredBuffer<MeshletInstanceData> MeshletCandidateInstances : register(u0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWByteAddressBuffer MeshletCandidateCount : register(u1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWByteAddressBuffer MeshletFineCullDispatchArguments : register(u2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

groupshared uint CandidateBase;

[numthreads(MeshletCullThreadCount, 1, 1)]
void main(uint groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    const uint drawIndex = MeshletVisibleDrawIndices[groupId.x];
    const MeshletDrawData draw = MeshletDraws[drawIndex];
    if (groupThreadId == 0u)
    {
        MeshletFineCullDispatchArguments.Store(4u, 1u);
        MeshletFineCullDispatchArguments.Store(8u, 1u);
        MeshletCandidateCount.InterlockedAdd(0u, draw.MeshletCount, CandidateBase);
        const uint fineCullGroupCount =
            (CandidateBase + draw.MeshletCount + MeshletCullThreadCount - 1u) / MeshletCullThreadCount;
        MeshletFineCullDispatchArguments.InterlockedMax(0u, fineCullGroupCount);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint localMeshletIndex = groupThreadId;
         localMeshletIndex < draw.MeshletCount;
         localMeshletIndex += MeshletCullThreadCount)
    {
        MeshletInstanceData candidate;
        candidate.MeshletIndex = draw.MeshletOffset + localMeshletIndex;
        candidate.TransformIndex = draw.TransformIndex;
        candidate.MaterialIndex = draw.MaterialIndex;
        candidate.Padding0 = 0u;
        MeshletCandidateInstances[CandidateBase + localMeshletIndex] = candidate;
    }
}
//Modify End
