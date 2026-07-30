#include <ShaderLibrary/Common/RootSignature.hlsli>

//Modify Begin:2026-07-30 by BestHui

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
    float4 PositionCs : SV_POSITION;
};

cbuffer MeshletDrawCBuffer : register(b0, COMMON_ROOT_SIGNATURE_MODEL_SPACE)
{
    matrix g_MeshletDraw_Model;
    matrix g_MeshletDraw_ModelViewProjection;
    matrix g_MeshletDraw_InverseTransposeModel;
    matrix g_MeshletDraw_PreviousModelViewProjection;
    uint g_MeshletDraw_MeshletOffset;
    uint g_MeshletDraw_MeshletCount;
    uint2 g_MeshletDraw_Padding;
};

StructuredBuffer<VertexAttributes> MeshletVertices : register(t0, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
ByteAddressBuffer MeshletIndices : register(t1, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);
StructuredBuffer<Meshlet> Meshlets : register(t2, COMMON_ROOT_SIGNATURE_PIPELINE_SPACE);

uint LoadIndex(uint indexOffset, uint indexNumber)
{
    uint byteOffset = (indexOffset + indexNumber) * 2;
    uint alignedByteOffset = byteOffset & ~3u;
    uint packed = MeshletIndices.Load(alignedByteOffset);
    uint shift = (byteOffset & 2u) * 8u;
    return (packed >> shift) & 0xffffu;
}

[outputtopology("triangle")]
[numthreads(MeshShaderThreadCount, 1, 1)]
void main(
    uint groupThreadId : SV_GroupThreadID,
    uint groupId : SV_GroupID,
    out vertices MeshletVertexOutput vertices[MaxMeshletVertices],
    out indices uint3 primitives[MaxMeshletPrimitives])
{
    Meshlet meshlet = (Meshlet)0;
    const bool isValidMeshlet = groupId < g_MeshletDraw_MeshletCount;
    if (isValidMeshlet)
    {
        meshlet = Meshlets[g_MeshletDraw_MeshletOffset + groupId];
    }

    const uint vertexCount = isValidMeshlet ? meshlet.VertexCount : 0;
    const uint primitiveCount = isValidMeshlet ? meshlet.IndexCount / 3 : 0;
    SetMeshOutputCounts(vertexCount, primitiveCount);

    if (groupThreadId < vertexCount)
    {
        const VertexAttributes input = MeshletVertices[meshlet.VertexOffset + groupThreadId];
        MeshletVertexOutput output;
        const float4 positionWs = mul(g_MeshletDraw_Model, float4(input.Position.xyz, 1.0f));
        output.PositionWs = positionWs.xyz;
        output.NormalWs = normalize(mul((float3x3)g_MeshletDraw_InverseTransposeModel, input.Normal.xyz));
        output.TangentWs = normalize(mul((float3x3)g_MeshletDraw_Model, input.Tangent.xyz));
        output.BitangentWs = normalize(mul((float3x3)g_MeshletDraw_Model, input.Bitangent.xyz));
        output.Uv = input.Uv.xy;
        output.PositionCs = mul(g_MeshletDraw_ModelViewProjection, float4(input.Position.xyz, 1.0f));
        output.CurrentPositionCs = output.PositionCs;
        output.PreviousPositionCs = mul(g_MeshletDraw_PreviousModelViewProjection, float4(input.Position.xyz, 1.0f));
        output.MeshletDebugId = g_MeshletDraw_MeshletOffset + groupId;
        vertices[groupThreadId] = output;
    }

    if (groupThreadId < primitiveCount)
    {
        const uint baseIndex = groupThreadId * 3;
        primitives[groupThreadId] = uint3(
            LoadIndex(meshlet.IndexOffset, baseIndex + 0),
            LoadIndex(meshlet.IndexOffset, baseIndex + 1),
            LoadIndex(meshlet.IndexOffset, baseIndex + 2));
    }
}

//Modify End
