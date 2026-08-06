#ifndef RAYTRACING_DEMO_SCENE_GEOMETRY_HLSLI
#define RAYTRACING_DEMO_SCENE_GEOMETRY_HLSLI

struct VertexAttributes
{
    float4 Position;
    float4 Normal;
    float4 Uv;
    float4 Tangent;
    float4 Bitangent;
};

struct MaterialData
{
    float4 Diffuse;
    float4 Specular;
    float4 Emission;
    float4 TilingOffset;
    uint DiffuseTextureIndex;
    uint NormalTextureIndex;
    uint MetallicTextureIndex;
    uint RoughnessTextureIndex;
    uint AmbientOcclusionTextureIndex;
    uint EmissionTextureIndex;
    uint HasDiffuseMap;
    uint HasNormalMap;
    uint HasMetallicMap;
    uint HasRoughnessMap;
    uint HasAmbientOcclusionMap;
    uint HasEmissionMap;
    float Metallic;
    float Roughness;
    uint Padding0;
    uint Padding1;
};

struct GeometryData
{
    uint VertexBufferIndex;
    uint IndexBufferIndex;
    uint MaterialIndex;
    uint Padding;
};

#endif
