#include <Framework/Geometry/Meshlet.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>

#include <meshoptimizer.h>

#include <cfloat>
#include <stdexcept>

using namespace DirectX;

//Modify Begin:2026-07-30 by BestHui
namespace
{
    void CopyFloat3(XMFLOAT3& destination, const float source[3])
    {
        destination.x = source[0];
        destination.y = source[1];
        destination.z = source[2];
    }

    void ComputeMeshletAabb(
        MeshletBounds& bounds,
        const meshopt_Meshlet& meshlet,
        const std::vector<unsigned int>& vertices,
        const std::vector<unsigned char>& indices,
        const std::vector<VertexAttributes>& vertexAttributes)
    {
        XMVECTOR min = XMVectorReplicate(FLT_MAX);
        XMVECTOR max = XMVectorReplicate(-FLT_MAX);

        const uint32_t indexCount = meshlet.triangle_count * 3;
        for (uint32_t i = 0; i < indexCount; ++i)
        {
            const auto index = indices[meshlet.triangle_offset + i];
            const auto& vertex = vertexAttributes[vertices[meshlet.vertex_offset + index]];
            const XMVECTOR position = XMLoadFloat4(&vertex.Position);
            min = XMVectorMin(position, min);
            max = XMVectorMax(position, max);
        }

        const XMVECTOR halfSize = (max - min) * 0.5f;
        XMStoreFloat3(&bounds.AabbCenter, min + halfSize);
        XMStoreFloat3(&bounds.AabbHalfSize, halfSize);
    }
}

MeshletBuildResult MeshletBuilder::Build(const MeshPrototype& meshPrototype, const MeshletBuildOptions& options)
{
    if (meshPrototype.m_Vertices.empty() || meshPrototype.m_Indices.empty())
    {
        throw std::runtime_error("Cannot build meshlets from an empty mesh prototype.");
    }

    const size_t maxMeshlets = meshopt_buildMeshletsBound(
        meshPrototype.m_Indices.size(),
        options.MaxVertices,
        options.MaxTriangles);

    std::vector<meshopt_Meshlet> sourceMeshlets(maxMeshlets);
    std::vector<unsigned int> vertices(maxMeshlets * options.MaxVertices);
    std::vector<unsigned char> indices(maxMeshlets * options.MaxTriangles * 3);

    const size_t meshletCount = meshopt_buildMeshlets(
        sourceMeshlets.data(),
        vertices.data(),
        indices.data(),
        meshPrototype.m_Indices.data(),
        meshPrototype.m_Indices.size(),
        &meshPrototype.m_Vertices[0].Position.x,
        meshPrototype.m_Vertices.size(),
        sizeof(VertexAttributes),
        options.MaxVertices,
        options.MaxTriangles,
        options.ConeWeight);

    if (meshletCount == 0)
    {
        return {};
    }

    const meshopt_Meshlet& lastMeshlet = sourceMeshlets[meshletCount - 1];
    sourceMeshlets.resize(meshletCount);
    vertices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
    indices.resize(lastMeshlet.triangle_offset + ((lastMeshlet.triangle_count * 3 + 3) & ~3));

    std::vector<Meshlet> meshlets(meshletCount);
    for (size_t i = 0; i < meshletCount; ++i)
    {
        const meshopt_Meshlet& sourceMeshlet = sourceMeshlets[i];
        Meshlet meshlet;

        const meshopt_Bounds sourceBounds = meshopt_computeMeshletBounds(
            &vertices[sourceMeshlet.vertex_offset],
            &indices[sourceMeshlet.triangle_offset],
            sourceMeshlet.triangle_count,
            &meshPrototype.m_Vertices[0].Position.x,
            meshPrototype.m_Vertices.size(),
            sizeof(VertexAttributes));

        CopyFloat3(meshlet.Bounds.Center, sourceBounds.center);
        meshlet.Bounds.Radius = sourceBounds.radius;
        CopyFloat3(meshlet.Bounds.ConeApex, sourceBounds.cone_apex);
        CopyFloat3(meshlet.Bounds.ConeAxis, sourceBounds.cone_axis);
        meshlet.Bounds.ConeCutoff = sourceBounds.cone_cutoff;
        ComputeMeshletAabb(meshlet.Bounds, sourceMeshlet, vertices, indices, meshPrototype.m_Vertices);

        meshlet.VertexOffset = sourceMeshlet.vertex_offset;
        meshlet.VertexCount = sourceMeshlet.vertex_count;
        meshlet.IndexOffset = sourceMeshlet.triangle_offset;
        meshlet.IndexCount = sourceMeshlet.triangle_count * 3;
        meshlets[i] = meshlet;
    }

    VertexCollectionType compactVertices(vertices.size());
    IndexCollectionType compactIndices(indices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        compactVertices[i] = meshPrototype.m_Vertices[vertices[i]];
    }
    for (size_t i = 0; i < indices.size(); ++i)
    {
        compactIndices[i] = indices[i];
    }

    MeshletBuildResult result;
    result.Mesh = MeshPrototype(std::move(compactVertices), std::move(compactIndices), true, false);
    result.Mesh.m_Name = meshPrototype.m_Name;
    result.Meshlets = std::move(meshlets);
    return result;
}
//Modify End

//Modify Begin:2026-07-31 by BestHui
MeshletGeometrySet::MeshletGeometrySet()
    : m_VertexBuffer(L"MeshletGeometrySet Vertices")
    , m_IndexBuffer(L"MeshletGeometrySet Indices")
    , m_MeshletBuffer(L"MeshletGeometrySet Meshlets")
    , m_TransformBuffer(L"MeshletGeometrySet Transforms")
    , m_InstanceBuffer(L"MeshletGeometrySet Instances")
    , m_IndirectCommandBuffer(L"MeshletGeometrySet Indirect Commands")
{
}

void MeshletGeometrySet::Clear()
{
    m_VertexBuffer = StructuredBuffer(L"MeshletGeometrySet Vertices");
    m_IndexBuffer = ByteAddressBuffer(L"MeshletGeometrySet Indices");
    m_MeshletBuffer = StructuredBuffer(L"MeshletGeometrySet Meshlets");
    m_TransformBuffer = StructuredBuffer(L"MeshletGeometrySet Transforms");
    m_InstanceBuffer = StructuredBuffer(L"MeshletGeometrySet Instances");
    m_IndirectCommandBuffer = StructuredBuffer(L"MeshletGeometrySet Indirect Commands");
    m_Vertices.clear();
    m_Indices.clear();
    m_Meshlets.clear();
    m_Draws.clear();
    m_Transforms.clear();
    m_Instances.clear();
}

std::pair<uint32_t, uint32_t> MeshletGeometrySet::AddGeometry(const MeshPrototype& prototype, const uint32_t materialIndex)
{
    MeshletBuildResult buildResult = MeshletBuilder::Build(prototype);
    if (buildResult.Meshlets.empty())
    {
        return { 0, 0 };
    }

    const uint32_t baseVertex = static_cast<uint32_t>(m_Vertices.size());
    const uint32_t baseIndex = static_cast<uint32_t>(m_Indices.size());
    const uint32_t meshletOffset = static_cast<uint32_t>(m_Meshlets.size());

    m_Vertices.insert(m_Vertices.end(), buildResult.Mesh.m_Vertices.begin(), buildResult.Mesh.m_Vertices.end());
    m_Indices.insert(m_Indices.end(), buildResult.Mesh.m_Indices.begin(), buildResult.Mesh.m_Indices.end());

    for (Meshlet meshlet : buildResult.Meshlets)
    {
        meshlet.VertexOffset += baseVertex;
        meshlet.IndexOffset += baseIndex;
        meshlet.MaterialIndex = materialIndex;
        m_Meshlets.push_back(meshlet);
    }

    return { meshletOffset, static_cast<uint32_t>(buildResult.Meshlets.size()) };
}

void MeshletGeometrySet::AddDraw(
    const MeshPrototype& prototype,
    const XMMATRIX& worldMatrix,
    const uint32_t materialIndex)
{
    const auto [meshletOffset, meshletCount] = AddGeometry(prototype, materialIndex);
    AddDraw(meshletOffset, meshletCount, worldMatrix, materialIndex);
}

void MeshletGeometrySet::AddDraw(
    const uint32_t meshletOffset,
    const uint32_t meshletCount,
    const XMMATRIX& worldMatrix,
    const uint32_t materialIndex)
{
    if (meshletCount == 0)
    {
        return;
    }

    MeshletDraw draw;
    draw.WorldMatrix = worldMatrix;
    draw.MaterialIndex = materialIndex;
    draw.MeshletOffset = meshletOffset;
    draw.MeshletCount = meshletCount;
    m_Draws.push_back(draw);
}

void MeshletGeometrySet::Upload(CommandList& commandList)
{
    if (m_Vertices.empty() || m_Indices.empty() || m_Meshlets.empty())
    {
        return;
    }

    BuildInstances();
    if (m_Instances.empty())
    {
        return;
    }

    commandList.CopyStructuredBuffer(m_VertexBuffer, m_Vertices);
    commandList.CopyByteAddressBuffer(m_IndexBuffer, m_Indices.size() * sizeof(uint16_t), m_Indices.data());
    commandList.CopyStructuredBuffer(m_MeshletBuffer, m_Meshlets);
    commandList.CopyStructuredBuffer(m_TransformBuffer, m_Transforms);
    commandList.CopyStructuredBuffer(m_InstanceBuffer, m_Instances);

    const D3D12_RESOURCE_DESC commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
        sizeof(MeshletIndirectCommand) * m_Instances.size(),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_IndirectCommandBuffer = StructuredBuffer(
        commandBufferDesc,
        m_Instances.size(),
        sizeof(MeshletIndirectCommand),
        L"MeshletGeometrySet Indirect Commands");
}

MeshletGpuResources MeshletGeometrySet::GetGpuResources()
{
    return {
        &m_VertexBuffer,
        &m_IndexBuffer,
        &m_MeshletBuffer,
        &m_TransformBuffer,
        &m_InstanceBuffer,
        &m_IndirectCommandBuffer,
        static_cast<uint32_t>(m_Instances.size()),
    };
}

void MeshletGeometrySet::BuildInstances()
{
    m_Transforms.clear();
    m_Instances.clear();
    m_Transforms.reserve(m_Draws.size());

    for (const MeshletDraw& draw : m_Draws)
    {
        MeshletTransformData transform;
        transform.Model = draw.WorldMatrix;
        transform.InverseTransposeModel = XMMatrixTranspose(XMMatrixInverse(nullptr, draw.WorldMatrix));
        const uint32_t transformIndex = static_cast<uint32_t>(m_Transforms.size());
        m_Transforms.push_back(transform);

        for (uint32_t meshletIndex = 0; meshletIndex < draw.MeshletCount; ++meshletIndex)
        {
            MeshletInstanceData instance;
            instance.MeshletIndex = draw.MeshletOffset + meshletIndex;
            instance.TransformIndex = transformIndex;
            instance.MaterialIndex = draw.MaterialIndex;
            m_Instances.push_back(instance);
        }
    }
}
//Modify End
