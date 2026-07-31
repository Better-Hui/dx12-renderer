//Modify Begin:2026-07-31 by BestHui
#include <ShaderLibrary/Common/RootSignature.hlsli>

static const uint MeshletTaskGroupSize = 32;

struct MeshletBounds
{
    float3 Center;
    float Radius;
    float3 ConeApex;
    float ConeCutoff;
    float3 ConeAxis;
    float Padding0;
    float3 AabbCenter;
    float Padding1;
    float3 AabbHalfSize;
    float Padding2;
};

struct Meshlet
{
    MeshletBounds Bounds;
    uint VertexOffset;
    uint VertexCount;
    uint IndexOffset;
    uint IndexCount;
    uint TransformIndex;
    uint MaterialIndex;
    uint VertexBufferIndex;
    uint IndexBufferIndex;
};

struct MeshletTransformData
{
    matrix Model;
    matrix InverseTransposeModel;
};

struct MeshletInstanceData
{
    uint MeshletIndex;
    uint TransformIndex;
    uint MaterialIndex;
    uint Padding0;
};

struct MeshletTaskPayload
{
    uint MeshletInstanceIndices[MeshletTaskGroupSize];
};

cbuffer MeshletCullCBuffer : register(b0)
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

float DistanceToPlane(float4 plane, float3 position)
{
    return dot(float4(position, -1.0f), plane);
}

bool FrustumCullSphere(float3 center, float radius)
{
    [unroll]
    for (uint planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
        if (DistanceToPlane(MeshletCull_FrustumPlanes[planeIndex], center) + radius <= 0.0f)
        {
            return false;
        }
    }
    return true;
}

float GetMaxScale(matrix model)
{
    const float3 axisX = float3(model._11, model._12, model._13);
    const float3 axisY = float3(model._21, model._22, model._23);
    const float3 axisZ = float3(model._31, model._32, model._33);
    return max(length(axisX), max(length(axisY), length(axisZ)));
}

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

        const float3 centerWs = mul(transform.Model, float4(meshlet.Bounds.Center, 1.0f)).xyz;
        const float radiusWs = meshlet.Bounds.Radius * GetMaxScale(transform.Model);
        if (MeshletCull_DebugDisableCulling != 0u || FrustumCullSphere(centerWs, radiusWs))
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
