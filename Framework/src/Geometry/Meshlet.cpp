#include <Framework/Geometry/Meshlet.h>

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
