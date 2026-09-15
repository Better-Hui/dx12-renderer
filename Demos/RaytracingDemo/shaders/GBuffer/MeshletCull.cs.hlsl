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
    uint MeshletCull_WriteDrawCommands;
    uint2 MeshletCull_Padding0;
};

StructuredBuffer<Meshlet> Meshlets : register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletInstanceData> MeshletCandidateInstances : register(t1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletTransformData> MeshletTransforms : register(t2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
ByteAddressBuffer MeshletCandidateCount : register(t3, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWStructuredBuffer<MeshletInstanceData> MeshletVisibleInstances : register(u0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWByteAddressBuffer MeshletVisibleCount : register(u1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWStructuredBuffer<MeshletIndirectCommand> MeshletIndirectCommands : register(u2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
RWByteAddressBuffer MeshletMeshDispatchArguments : register(u3, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

static const uint MeshletMaxCullWaves = MeshletCullThreadCount / 4u;
groupshared uint WaveVisibleCounts[MeshletMaxCullWaves];
groupshared uint WaveVisibleOffsets[MeshletMaxCullWaves];
groupshared uint GroupVisibleBase;
groupshared uint GroupVisibleCount;

[numthreads(MeshletCullThreadCount, 1, 1)]
void main(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint groupThreadId : SV_GroupThreadID,
    uint3 groupId : SV_GroupID)
{
    if (groupThreadId == 0u && groupId.x == 0u)
    {
        MeshletMeshDispatchArguments.Store(4u, 1u);
        MeshletMeshDispatchArguments.Store(8u, 1u);
    }
    const uint candidateIndex = dispatchThreadId.x;
    const uint candidateCount = min(MeshletCandidateCount.Load(0u), MeshletCull_CandidateCapacity);
    MeshletInstanceData instance;
    Meshlet meshlet;
    bool visible = false;
    if (candidateIndex < candidateCount)
    {
        instance = MeshletCandidateInstances[candidateIndex];
        meshlet = Meshlets[instance.MeshletIndex];
        const MeshletTransformData transform = MeshletTransforms[instance.TransformIndex];
        const float3 centerWs = mul(float4(meshlet.Bounds.Center, 1.0f), transform.Model).xyz;
        const float radiusWs = meshlet.Bounds.Radius * MeshletGetMaxScale(transform.Model);
        visible = MeshletCull_DebugDisableCulling != 0u ||
            MeshletFrustumCullSphere(MeshletCull_FrustumPlanes, centerWs, radiusWs);
    }

    const uint visibleValue = visible ? 1u : 0u;
    const uint waveLaneCount = WaveGetLaneCount();
    const uint waveIndex = groupThreadId / waveLaneCount;
    const uint waveVisibleOffset = WavePrefixCountBits(visibleValue);
    const uint waveVisibleCount = WaveActiveCountBits(visibleValue);
    if (WaveIsFirstLane())
    {
        WaveVisibleCounts[waveIndex] = waveVisibleCount;
    }
    GroupMemoryBarrierWithGroupSync();

    if (groupThreadId == 0u)
    {
        const uint waveCount = (MeshletCullThreadCount + waveLaneCount - 1u) / waveLaneCount;
        uint groupVisibleCount = 0u;
        for (uint currentWave = 0u; currentWave < waveCount; ++currentWave)
        {
            WaveVisibleOffsets[currentWave] = groupVisibleCount;
            groupVisibleCount += WaveVisibleCounts[currentWave];
        }
        GroupVisibleCount = groupVisibleCount;
        GroupVisibleBase = 0u;
        if (groupVisibleCount > 0u)
        {
            MeshletVisibleCount.InterlockedAdd(0u, groupVisibleCount, GroupVisibleBase);
            const uint dispatchGroupCount =
                (GroupVisibleBase + groupVisibleCount + MeshletCull_BackendGroupSize - 1u) /
                MeshletCull_BackendGroupSize;
            MeshletMeshDispatchArguments.InterlockedMax(0u, dispatchGroupCount);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (visibleValue != 0u)
    {
        const uint visibleIndex = GroupVisibleBase + WaveVisibleOffsets[waveIndex] + waveVisibleOffset;
        MeshletVisibleInstances[visibleIndex] = instance;
        if (MeshletCull_WriteDrawCommands != 0u)
        {
            MeshletIndirectCommand command;
            command.MeshletInstanceIndex = visibleIndex;
            command.Flags = 0u;
            command.Padding0 = 0u;
            command.Padding1 = 0u;
            command.IndexCountPerInstance = meshlet.IndexCount;
            command.InstanceCount = 1u;
            command.StartIndexLocation = meshlet.IndexOffset;
            command.BaseVertexLocation = int(meshlet.VertexOffset);
            command.StartInstanceLocation = 0u;
            MeshletIndirectCommands[visibleIndex] = command;
        }
    }
}
//Modify End
