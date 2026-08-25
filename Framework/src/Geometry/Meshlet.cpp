#include <Framework/Geometry/Meshlet.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
//Modify Begin:2026-08-18 by Hui
#include <DX12Library/CommandListInternalAccess.h>
#include <DX12Library/ResourceUploader.h>
//Modify End

#include <meshoptimizer.h>

#include <algorithm>
#include <cfloat>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

using namespace DirectX;

//Modify Begin:2026-08-25 by Hui
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

    void RecalculateMeshletBounds(
        Meshlet& meshlet,
        const std::vector<uint16_t>& indices,
        const std::vector<VertexAttributes>& vertices)
    {
        XMVECTOR min = XMVectorReplicate(FLT_MAX);
        XMVECTOR max = XMVectorReplicate(-FLT_MAX);
        for (uint32_t indexNumber = 0; indexNumber < meshlet.IndexCount; ++indexNumber)
        {
            const uint32_t compactVertexIndex = indices[meshlet.IndexOffset + indexNumber];
            Assert(compactVertexIndex < meshlet.VertexCount, "Meshlet compact vertex index is invalid.");
            const VertexAttributes& vertex = vertices[meshlet.VertexOffset + compactVertexIndex];
            const XMVECTOR position = XMVectorSet(vertex.Position.x, vertex.Position.y, vertex.Position.z, 0.0f);
            min = XMVectorMin(position, min);
            max = XMVectorMax(position, max);
        }

        const XMVECTOR halfSize = (max - min) * 0.5f;
        const XMVECTOR center = min + halfSize;
        XMStoreFloat3(&meshlet.Bounds.Center, center);
        meshlet.Bounds.Radius = XMVectorGetX(XMVector3Length(halfSize));
        XMStoreFloat3(&meshlet.Bounds.AabbCenter, center);
        XMStoreFloat3(&meshlet.Bounds.AabbHalfSize, halfSize);
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
    result.Mesh.m_SourceMeshIndex = meshPrototype.m_SourceMeshIndex;
    result.Meshlets = std::move(meshlets);
    result.CompactToSourceVertexIndices.assign(vertices.begin(), vertices.end());
    return result;
}
//Modify End

//Modify Begin:2026-08-25 by Hui
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
    m_GeometryEntries.clear();
    m_Draws.clear();
    m_Transforms.clear();
    m_Instances.clear();
    m_GeometryDataDirty = true;
    m_IndexDataDirty = true;
    m_InstanceDataDirty = true;
}

void MeshletGeometrySet::ClearDraws()
{
    m_Draws.clear();
    m_Transforms.clear();
    m_Instances.clear();
    m_InstanceDataDirty = true;
}

std::pair<uint32_t, uint32_t> MeshletGeometrySet::AddGeometry(const MeshPrototype& prototype)
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
        m_Meshlets.push_back(meshlet);
    }

    m_GeometryEntries.push_back({
        meshletOffset,
        static_cast<uint32_t>(buildResult.Meshlets.size()),
        baseVertex,
        std::move(buildResult.CompactToSourceVertexIndices)
    });

    m_GeometryDataDirty = true;
    m_IndexDataDirty = true;
    return { meshletOffset, static_cast<uint32_t>(buildResult.Meshlets.size()) };
}

void MeshletGeometrySet::AddDraw(
    const MeshPrototype& prototype,
    const XMMATRIX& worldMatrix,
    const uint32_t materialIndex)
{
    const auto [meshletOffset, meshletCount] = AddGeometry(prototype);
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
    m_InstanceDataDirty = true;
}

bool MeshletGeometrySet::UpdateGeometryVertices(
    const uint32_t meshletOffset,
    const uint32_t meshletCount,
    const std::span<const VertexAttributes> sourceVertices)
{
    const auto geometry = std::ranges::find_if(
        m_GeometryEntries,
        [meshletOffset, meshletCount](const GeometryEntry& entry)
        {
            return entry.MeshletOffset == meshletOffset && entry.MeshletCount == meshletCount;
        });
    if (geometry == m_GeometryEntries.end())
    {
        return false;
    }

    for (const uint32_t sourceVertexIndex : geometry->CompactToSourceVertexIndices)
    {
        if (sourceVertexIndex >= sourceVertices.size())
        {
            return false;
        }
    }

    Assert(
        geometry->VertexOffset + geometry->CompactToSourceVertexIndices.size() <= m_Vertices.size(),
        "Meshlet compact vertex range is invalid.");
    for (uint32_t compactVertexIndex = 0;
        compactVertexIndex < geometry->CompactToSourceVertexIndices.size();
        ++compactVertexIndex)
    {
        m_Vertices[geometry->VertexOffset + compactVertexIndex] =
            sourceVertices[geometry->CompactToSourceVertexIndices[compactVertexIndex]];
    }

    Assert(
        geometry->MeshletOffset + geometry->MeshletCount <= m_Meshlets.size(),
        "Meshlet geometry range is invalid.");
    for (uint32_t meshletIndex = 0; meshletIndex < geometry->MeshletCount; ++meshletIndex)
    {
        RecalculateMeshletBounds(m_Meshlets[geometry->MeshletOffset + meshletIndex], m_Indices, m_Vertices);
    }
    m_GeometryDataDirty = true;
    return true;
}

void MeshletGeometrySet::Upload(CommandList& commandList)
{
    if (m_Vertices.empty() || m_Indices.empty() || m_Meshlets.empty())
    {
        return;
    }

    ResourceUploader uploader(commandList.GetDeviceContext());
    if (m_GeometryDataDirty)
    {
        const bool vertexBufferCanCopy =
            m_VertexBuffer.GetD3D12Resource() != nullptr &&
            m_VertexBuffer.GetD3D12ResourceDesc().Width >= m_Vertices.size() * sizeof(VertexAttributes);
        if (vertexBufferCanCopy)
        {
            uploader.CopyStructuredBuffer(commandList, m_VertexBuffer, m_Vertices);
        }
        else
        {
            uploader.UploadStructuredBuffer(commandList, m_VertexBuffer, m_Vertices);
        }
        const bool meshletBufferCanCopy =
            m_MeshletBuffer.GetD3D12Resource() != nullptr &&
            m_MeshletBuffer.GetD3D12ResourceDesc().Width >= m_Meshlets.size() * sizeof(Meshlet);
        if (meshletBufferCanCopy)
        {
            uploader.CopyStructuredBuffer(commandList, m_MeshletBuffer, m_Meshlets);
        }
        else
        {
            uploader.UploadStructuredBuffer(commandList, m_MeshletBuffer, m_Meshlets);
        }
        m_GeometryDataDirty = false;
    }

    if (m_IndexDataDirty)
    {
        uploader.UploadByteAddressBuffer(
            commandList, m_IndexBuffer, m_Indices.size() * sizeof(uint16_t), m_Indices.data());
        m_IndexDataDirty = false;
    }

    if (!m_InstanceDataDirty)
    {
        return;
    }

    BuildInstances();
    if (m_Instances.empty())
    {
        m_InstanceDataDirty = false;
        return;
    }

    const bool transformBufferCanCopy =
        m_TransformBuffer.GetD3D12Resource() != nullptr &&
        m_TransformBuffer.GetD3D12ResourceDesc().Width >= m_Transforms.size() * sizeof(MeshletTransformData);
    if (transformBufferCanCopy)
    {
        uploader.CopyStructuredBuffer(commandList, m_TransformBuffer, m_Transforms);
    }
    else
    {
        uploader.UploadStructuredBuffer(commandList, m_TransformBuffer, m_Transforms);
    }
    const bool instanceBufferCanCopy =
        m_InstanceBuffer.GetD3D12Resource() != nullptr &&
        m_InstanceBuffer.GetD3D12ResourceDesc().Width >= m_Instances.size() * sizeof(MeshletInstanceData);
    if (instanceBufferCanCopy)
    {
        uploader.CopyStructuredBuffer(commandList, m_InstanceBuffer, m_Instances);
    }
    else
    {
        uploader.UploadStructuredBuffer(commandList, m_InstanceBuffer, m_Instances);
    }

    const uint64_t requiredCommandBufferSize = sizeof(MeshletIndirectCommand) * m_Instances.size();
    const D3D12_RESOURCE_DESC currentCommandBufferDesc = m_IndirectCommandBuffer.GetD3D12ResourceDesc();
    if (m_IndirectCommandBuffer.GetD3D12Resource() == nullptr ||
        currentCommandBufferDesc.Width < requiredCommandBufferSize)
    {
        if (m_IndirectCommandBuffer.GetD3D12Resource() != nullptr)
        {
            CommandListInternalAccess::TrackResourceLifetime(commandList, m_IndirectCommandBuffer);
        }

        const D3D12_RESOURCE_DESC commandBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
            requiredCommandBufferSize,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        m_IndirectCommandBuffer = StructuredBuffer(
            commandBufferDesc,
            m_Instances.size(),
            sizeof(MeshletIndirectCommand),
            L"MeshletGeometrySet Indirect Commands",
            commandList.GetDeviceContext());
    }
    else
    {
        m_IndirectCommandBuffer.CreateViews(m_Instances.size(), sizeof(MeshletIndirectCommand));
    }
    m_InstanceDataDirty = false;
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

void MeshletSceneResources::Clear()
{
    m_GeometrySet.Clear();
    m_GeometryMeshletRanges.clear();
    m_Instances.clear();
    m_InstanceIndices.clear();
    m_NextInstanceHandle = 1;
    m_UpdateStatistics = {};
    m_DrawsDirty = true;
}

void MeshletSceneResources::InitializeGeometries(const std::vector<MeshletSceneGeometrySource>& geometries)
{
    Assert(m_Instances.empty(), "Meshlet scene geometries cannot change while instances are registered.");

    m_GeometrySet.Clear();
    m_GeometryMeshletRanges.clear();
    m_GeometryMeshletRanges.reserve(geometries.size());
    for (const MeshletSceneGeometrySource& geometry : geometries)
    {
        Assert(geometry.MeshPrototypes != nullptr, "Meshlet scene geometry prototypes must not be null.");

        std::vector<std::pair<uint32_t, uint32_t>> meshletRanges;
        meshletRanges.reserve(geometry.MeshPrototypes->size());
        for (const MeshPrototype& prototype : *geometry.MeshPrototypes)
        {
            meshletRanges.push_back(m_GeometrySet.AddGeometry(prototype));
        }
        m_GeometryMeshletRanges.push_back(std::move(meshletRanges));
    }
    m_DrawsDirty = true;
}

void MeshletSceneResources::Rebuild(
    const std::vector<MeshletSceneGeometrySource>& geometries,
    const std::vector<MeshletSceneInstanceSource>& instances)
{
    Clear();
    InitializeGeometries(geometries);
    for (const MeshletSceneInstanceSource& instance : instances)
    {
        AddInstance(instance);
    }
}

MeshletSceneInstanceHandle MeshletSceneResources::AddInstance(const MeshletSceneInstanceSource& instance)
{
    Assert(instance.GeometryIndex < m_GeometryMeshletRanges.size(), "Meshlet scene instance geometry index is invalid.");

    const MeshletSceneInstanceHandle handle = m_NextInstanceHandle++;
    const uint32_t instanceIndex = static_cast<uint32_t>(m_Instances.size());
    m_Instances.push_back({ handle, instance });
    m_InstanceIndices.emplace(handle, instanceIndex);
    m_DrawsDirty = true;
    return handle;
}

bool MeshletSceneResources::UpdateInstance(
    const MeshletSceneInstanceHandle handle,
    const MeshletSceneInstanceSource& instance)
{
    if (instance.GeometryIndex >= m_GeometryMeshletRanges.size())
    {
        return false;
    }

    const auto indexResult = m_InstanceIndices.find(handle);
    if (indexResult == m_InstanceIndices.end())
    {
        return false;
    }

    m_Instances[indexResult->second].Source = instance;
    m_DrawsDirty = true;
    ++m_UpdateStatistics.InstanceUpdateCount;
    return true;
}

bool MeshletSceneResources::UpdateGeometryVertices(
    const uint32_t geometryIndex,
    const uint32_t prototypeIndex,
    const std::span<const VertexAttributes> sourceVertices)
{
    if (geometryIndex >= m_GeometryMeshletRanges.size() ||
        prototypeIndex >= m_GeometryMeshletRanges[geometryIndex].size())
    {
        return false;
    }

    const auto [meshletOffset, meshletCount] = m_GeometryMeshletRanges[geometryIndex][prototypeIndex];
    if (!m_GeometrySet.UpdateGeometryVertices(meshletOffset, meshletCount, sourceVertices))
    {
        return false;
    }

    ++m_UpdateStatistics.GeometryUpdateCount;
    return true;
}

bool MeshletSceneResources::RemoveInstance(const MeshletSceneInstanceHandle handle)
{
    const auto indexResult = m_InstanceIndices.find(handle);
    if (indexResult == m_InstanceIndices.end())
    {
        return false;
    }

    const uint32_t instanceIndex = indexResult->second;
    const uint32_t lastIndex = static_cast<uint32_t>(m_Instances.size() - 1);
    if (instanceIndex != lastIndex)
    {
        m_Instances[instanceIndex] = std::move(m_Instances[lastIndex]);
        m_InstanceIndices[m_Instances[instanceIndex].Handle] = instanceIndex;
    }

    m_Instances.pop_back();
    // Updating the moved instance may rehash the unordered map and invalidate indexResult.
    m_InstanceIndices.erase(handle);
    m_DrawsDirty = true;
    return true;
}

void MeshletSceneResources::RemoveInstances(const std::span<const MeshletSceneInstanceHandle> handles)
{
    if (handles.empty())
    {
        return;
    }

    std::unordered_set<MeshletSceneInstanceHandle> handlesToRemove(handles.begin(), handles.end());
    Assert(handlesToRemove.size() == handles.size(), "Meshlet scene instance removal contains duplicate handles.");
    Assert(handlesToRemove.size() <= m_Instances.size(), "Meshlet scene instance removal exceeds the instance count.");

    for (const MeshletSceneInstanceHandle handle : handlesToRemove)
    {
        Assert(m_InstanceIndices.contains(handle), "Meshlet scene instance handle is invalid.");
    }

    std::vector<InstanceEntry> survivingInstances;
    survivingInstances.reserve(m_Instances.size() - handlesToRemove.size());
    for (InstanceEntry& instance : m_Instances)
    {
        if (!handlesToRemove.contains(instance.Handle))
        {
            survivingInstances.push_back(std::move(instance));
        }
    }

    m_Instances = std::move(survivingInstances);
    m_InstanceIndices.clear();
    m_InstanceIndices.reserve(m_Instances.size());
    for (uint32_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
    {
        const bool inserted = m_InstanceIndices.emplace(m_Instances[instanceIndex].Handle, instanceIndex).second;
        Assert(inserted, "Meshlet scene instance handle is duplicated.");
    }
    m_DrawsDirty = true;
}

void MeshletSceneResources::Upload(CommandList& commandList)
{
    if (m_DrawsDirty)
    {
        RebuildDraws();
    }
    m_GeometrySet.Upload(commandList);
}

MeshletGpuResources MeshletSceneResources::GetGpuResources()
{
    return m_GeometrySet.GetGpuResources();
}

void MeshletSceneResources::RebuildDraws()
{
    m_GeometrySet.ClearDraws();
    for (const InstanceEntry& instance : m_Instances)
    {
        const std::vector<std::pair<uint32_t, uint32_t>>& meshletRanges =
            m_GeometryMeshletRanges[instance.Source.GeometryIndex];
        for (const auto [meshletOffset, meshletCount] : meshletRanges)
        {
            m_GeometrySet.AddDraw(
                meshletOffset,
                meshletCount,
                instance.Source.WorldMatrix,
                instance.Source.MaterialIndex);
        }
    }
    m_DrawsDirty = false;
}
//Modify End
