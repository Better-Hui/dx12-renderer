//Modify Begin:2026-07-31 by BestHui
#include <ShaderLibrary/Common/RootSignature.hlsli>

static const uint MeshletTaskGroupSize = 32;
static const uint MaxMeshletVertices = 64;
static const uint MaxMeshletPrimitives = 124;
static const uint MeshShaderThreadCount = 128;

struct VertexAttributes
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

struct MeshletVertexOutput
{
    float3 PositionWs : POSITION_WS;
    float3 NormalWs : NORMAL;
    float3 TangentWs : TANGENT;
    float3 BitangentWs : BINORMAL;
    float2 Uv : TEXCOORD0;
    float4 CurrentPositionCs : TEXCOORD1;
    float4 PreviousPositionCs : TEXCOORD2;
    nointerpolation uint MeshletDebugId : TEXCOORD3;
    nointerpolation uint MaterialIndex : TEXCOORD4;
    nointerpolation uint DebugMeshletClusters : TEXCOORD5;
    float4 PositionCs : SV_POSITION;
};

cbuffer PipelineCBuffer : register(b0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE)
{
    matrix g_Pipeline_View;
    matrix g_Pipeline_Projection;
    matrix g_Pipeline_ViewProjection;
    float4 g_Pipeline_CameraPosition;
    matrix g_Pipeline_InverseView;
    matrix g_Pipeline_InverseProjection;
    float2 g_Pipeline_ScreenResolution;
    float2 g_Pipeline_ScreenTexelSize;
    matrix g_Pipeline_PreviousViewProjection;
    uint g_Pipeline_DebugMeshletClusters;
    uint3 g_Pipeline_Padding0;
};

StructuredBuffer<VertexAttributes> MeshletVertices : register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
ByteAddressBuffer MeshletIndices : register(t1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<Meshlet> Meshlets : register(t2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletTransformData> MeshletTransforms : register(t3, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<MeshletInstanceData> MeshletInstances : register(t4, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

uint LoadMeshletIndex(uint indexOffset, uint indexNumber)
{
    const uint byteOffset = (indexOffset + indexNumber) * 2;
    const uint alignedByteOffset = byteOffset & ~3u;
    const uint packed = MeshletIndices.Load(alignedByteOffset);
    const uint shift = (byteOffset & 2u) * 8u;
    return (packed >> shift) & 0xffffu;
}

[outputtopology("triangle")]
[numthreads(MeshShaderThreadCount, 1, 1)]
void main(
    uint groupThreadId : SV_GroupThreadID,
    uint groupId : SV_GroupID,
    in payload MeshletTaskPayload payload,
    out vertices MeshletVertexOutput vertices[MaxMeshletVertices],
    out indices uint3 primitives[MaxMeshletPrimitives])
{
    const uint instanceIndex = payload.MeshletInstanceIndices[groupId];
    const MeshletInstanceData instance = MeshletInstances[instanceIndex];
    const Meshlet meshlet = Meshlets[instance.MeshletIndex];
    const MeshletTransformData transform = MeshletTransforms[instance.TransformIndex];

    const uint vertexCount = meshlet.VertexCount;
    const uint primitiveCount = meshlet.IndexCount / 3;
    SetMeshOutputCounts(vertexCount, primitiveCount);

    if (groupThreadId < vertexCount)
    {
        const VertexAttributes input = MeshletVertices[meshlet.VertexOffset + groupThreadId];
        MeshletVertexOutput output;
        const float4 positionWs = mul(transform.Model, float4(input.Position.xyz, 1.0f));
        output.PositionWs = positionWs.xyz;
        output.NormalWs = normalize(mul((float3x3)transform.InverseTransposeModel, input.Normal.xyz));
        output.TangentWs = normalize(mul((float3x3)transform.Model, input.Tangent.xyz));
        output.BitangentWs = normalize(mul((float3x3)transform.Model, input.Bitangent.xyz));
        output.Uv = input.Uv.xy;
        output.PositionCs = mul(g_Pipeline_ViewProjection, positionWs);
        output.CurrentPositionCs = output.PositionCs;
        output.PreviousPositionCs = mul(g_Pipeline_PreviousViewProjection, positionWs);
        output.MeshletDebugId = instance.MeshletIndex;
        output.MaterialIndex = instance.MaterialIndex;
        output.DebugMeshletClusters = g_Pipeline_DebugMeshletClusters;
        vertices[groupThreadId] = output;
    }

    if (groupThreadId < primitiveCount)
    {
        const uint baseIndex = groupThreadId * 3;
        primitives[groupThreadId] = uint3(
            LoadMeshletIndex(meshlet.IndexOffset, baseIndex + 0),
            LoadMeshletIndex(meshlet.IndexOffset, baseIndex + 1),
            LoadMeshletIndex(meshlet.IndexOffset, baseIndex + 2));
    }
}
//Modify End
