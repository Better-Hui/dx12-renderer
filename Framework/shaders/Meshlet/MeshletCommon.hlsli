//Modify Begin:2026-07-31 by BestHui
#ifndef FRAMEWORK_MESHLET_COMMON_HLSLI
#define FRAMEWORK_MESHLET_COMMON_HLSLI

static const uint MeshletTaskGroupSize = 32;
static const uint MeshletCullThreadCount = 64;
static const uint MeshletMaxVertices = 64;
static const uint MeshletMaxPrimitives = 124;
static const uint MeshletShaderThreadCount = 128;

struct MeshletVertexAttributes
{
    float4 Position;
    float4 Normal;
    float4 Uv;
    float4 Tangent;
    float4 Bitangent;
};

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

struct MeshletIndirectCommand
{
    uint MeshletInstanceIndex;
    uint Flags;
    uint Padding0;
    uint Padding1;
    uint VertexCountPerInstance;
    uint InstanceCount;
    uint StartVertexLocation;
    uint StartInstanceLocation;
};

uint MeshletLoadIndex(ByteAddressBuffer meshletIndices, uint indexOffset, uint indexNumber)
{
    const uint byteOffset = (indexOffset + indexNumber) * 2;
    const uint alignedByteOffset = byteOffset & ~3u;
    const uint packed = meshletIndices.Load(alignedByteOffset);
    const uint shift = (byteOffset & 2u) * 8u;
    return (packed >> shift) & 0xffffu;
}

float MeshletDistanceToPlane(float4 plane, float3 position)
{
    return dot(float4(position, -1.0f), plane);
}

bool MeshletFrustumCullSphere(float4 frustumPlanes[6], float3 center, float radius)
{
    [unroll]
    for (uint planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
        if (MeshletDistanceToPlane(frustumPlanes[planeIndex], center) + radius <= 0.0f)
        {
            return false;
        }
    }
    return true;
}

float MeshletGetMaxScale(matrix model)
{
    const float3 axisX = float3(model._11, model._12, model._13);
    const float3 axisY = float3(model._21, model._22, model._23);
    const float3 axisZ = float3(model._31, model._32, model._33);
    return max(length(axisX), max(length(axisY), length(axisZ)));
}

#endif
//Modify End
