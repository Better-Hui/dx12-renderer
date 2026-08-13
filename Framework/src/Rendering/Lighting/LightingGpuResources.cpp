//Modify Begin:2026-08-06 by BestHui
#include <Framework/Rendering/Lighting/LightingGpuResources.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>

#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/SharedUploadBuffer.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace DirectX;

namespace
{
    constexpr size_t InitialLightBufferCapacity = 1;

    size_t GrowLightBufferCapacity(const size_t currentCapacity, const size_t requiredCapacity)
    {
        size_t newCapacity = std::max<size_t>(InitialLightBufferCapacity, currentCapacity);
        while (newCapacity < requiredCapacity)
        {
            newCapacity *= 2;
        }
        return newCapacity;
    }

    void MarkDirtyRange(const size_t beginIndex, const size_t endIndex, size_t& dirtyBegin, size_t& dirtyEnd)
    {
        if (beginIndex >= endIndex)
        {
            return;
        }

        if (dirtyBegin == dirtyEnd)
        {
            dirtyBegin = beginIndex;
            dirtyEnd = endIndex;
            return;
        }

        dirtyBegin = std::min(dirtyBegin, beginIndex);
        dirtyEnd = std::max(dirtyEnd, endIndex);
    }

    template<typename T>
    std::vector<T> CreateBufferCapacityData(const size_t count)
    {
        return std::vector<T>(std::max<size_t>(1, count));
    }

    template<typename T>
    bool EnsureStructuredBufferCapacity(CommandList& commandList, StructuredBuffer& buffer, size_t& currentCapacity, const std::vector<T>& values)
    {
        if (values.size() <= currentCapacity)
        {
            return false;
        }

        currentCapacity = GrowLightBufferCapacity(currentCapacity, values.size());
        std::vector<T> capacityData(currentCapacity);
        std::copy(values.begin(), values.end(), capacityData.begin());
        commandList.CopyStructuredBuffer(buffer, capacityData);
        return true;
    }

    template<typename T>
    void UploadGpuLightRange(
        CommandList& commandList,
        SharedUploadBuffer& uploadBuffer,
        StructuredBuffer& destination,
        const std::vector<T>& values,
        const size_t beginIndex,
        const size_t endIndex)
    {
        const size_t clampedBegin = std::min(beginIndex, values.size());
        const size_t clampedEnd = std::min(endIndex, values.size());
        if (clampedBegin >= clampedEnd)
        {
            return;
        }

        const size_t elementCount = clampedEnd - clampedBegin;
        const uint64_t destinationOffset = static_cast<uint64_t>(clampedBegin * sizeof(T));
        uploadBuffer.Upload(commandList, destination, values.data() + clampedBegin, elementCount * sizeof(T), sizeof(T), destinationOffset);
    }

    SurfaceEmitterTriangleData CreateSurfaceEmitterTriangle(
        const XMFLOAT3& position0,
        const XMFLOAT3& position1,
        const XMFLOAT3& position2,
        const XMFLOAT2& uv0,
        const XMFLOAT2& uv1,
        const XMFLOAT2& uv2)
    {
        SurfaceEmitterTriangleData triangle{};
        triangle.Position0 = { position0.x, position0.y, position0.z, 0.0f };
        triangle.Position1 = { position1.x, position1.y, position1.z, 0.0f };
        triangle.Position2 = { position2.x, position2.y, position2.z, 0.0f };
        triangle.Uv0Uv1 = { uv0.x, uv0.y, uv1.x, uv1.y };
        triangle.Uv2AndPadding = { uv2.x, uv2.y, 0.0f, 0.0f };
        return triangle;
    }

    SurfaceEmitterInstanceData CreateRectangleSurfaceEmitterInstance(const AreaLightData& light, const uint32_t geometryIndex)
    {
        SurfaceEmitterInstanceData instance{};
        instance.OriginAndRange = light.PositionAndRange;
        instance.AxisX = {
            light.AxisUAndExtent.x * light.AxisUAndExtent.w,
            light.AxisUAndExtent.y * light.AxisUAndExtent.w,
            light.AxisUAndExtent.z * light.AxisUAndExtent.w,
            0.0f
        };
        instance.AxisY = {
            light.AxisVAndExtent.x * light.AxisVAndExtent.w,
            light.AxisVAndExtent.y * light.AxisVAndExtent.w,
            light.AxisVAndExtent.z * light.AxisVAndExtent.w,
            0.0f
        };
        instance.AxisZ = { light.NormalAndType.x, light.NormalAndType.y, light.NormalAndType.z, 0.0f };
        instance.EmissionAndIntensity = light.ColorAndIntensity;
        instance.GeometryIndex = geometryIndex;
        instance.MaterialIndex = SurfaceEmitterInvalidMaterialIndex;
        return instance;
    }

    XMVECTOR TransformSurfaceEmitterVector(const SurfaceEmitterInstanceData& instance, const XMVECTOR localVector)
    {
        return XMVectorAdd(
            XMVectorAdd(
                XMVectorScale(XMLoadFloat4(&instance.AxisX), XMVectorGetX(localVector)),
                XMVectorScale(XMLoadFloat4(&instance.AxisY), XMVectorGetY(localVector))),
            XMVectorScale(XMLoadFloat4(&instance.AxisZ), XMVectorGetZ(localVector)));
    }

    float GetSurfaceEmitterWorldArea(
        const SurfaceEmitterInstanceData& instance,
        const SurfaceEmitterGeometryData& geometry,
        const std::vector<SurfaceEmitterTriangleData>& triangles)
    {
        const size_t triangleBegin = geometry.TriangleOffset;
        const size_t triangleEnd = triangleBegin + geometry.TriangleCount;
        if (triangleBegin >= triangles.size() || triangleEnd > triangles.size())
        {
            return 0.0f;
        }

        float totalArea = 0.0f;
        for (size_t triangleIndex = triangleBegin; triangleIndex < triangleEnd; ++triangleIndex)
        {
            const SurfaceEmitterTriangleData& triangle = triangles[triangleIndex];
            const XMVECTOR edge0 = XMVectorSubtract(XMLoadFloat4(&triangle.Position1), XMLoadFloat4(&triangle.Position0));
            const XMVECTOR edge1 = XMVectorSubtract(XMLoadFloat4(&triangle.Position2), XMLoadFloat4(&triangle.Position0));
            const XMVECTOR worldEdge0 = TransformSurfaceEmitterVector(instance, edge0);
            const XMVECTOR worldEdge1 = TransformSurfaceEmitterVector(instance, edge1);
            totalArea += 0.5f * XMVectorGetX(XMVector3Length(XMVector3Cross(worldEdge0, worldEdge1)));
        }
        return totalArea;
    }
}

LightingGpuResources::LightingGpuResources(FrameworkDeviceContext& deviceContext)
    : m_DeviceContext(deviceContext)
    , m_DirectionalLightBuffer(L"Ray Tracing Directional Lights")
    , m_PointLightBuffer(L"Ray Tracing Point Lights")
    , m_SurfaceEmitterGeometryBuffer(L"Surface Emitter Geometries")
    , m_SurfaceEmitterTriangleBuffer(L"Surface Emitter Triangles")
    , m_SurfaceEmitterTriangleCdfBuffer(L"Surface Emitter Triangle CDF")
    , m_SurfaceEmitterInstanceBuffer(L"Surface Emitter Instances")
    , m_DirectLightCdfBuffer(L"Ray Tracing Direct Light CDF")
{
}

LightingGpuResources::~LightingGpuResources() = default;

void LightingGpuResources::Rebuild(const LightingGpuInput& input)
{
    m_AreaLights = input.AreaLights;
    m_MeshSurfaceEmitterData = input.MeshSurfaceEmitters;
    m_DirectionalLightsEnabled = input.DirectionalLightsEnabled;
    m_PointLightsEnabled = input.PointLightsEnabled;
    m_AreaLightsEnabled = input.AreaLightsEnabled;
    BuildGpuData(input.DirectionalLights, input.PointLights);
    MarkAllGpuDataDirty();
}

void LightingGpuResources::SetMeshSurfaceEmitters(SurfaceEmitterSceneData emitterData)
{
    m_MeshSurfaceEmitterData = std::move(emitterData);
    RebuildSurfaceEmitterGpuData();
    MarkDirtyRange(0, m_SurfaceEmitterGeometryGpuData.size(), m_SurfaceEmitterGeometryDirtyBegin, m_SurfaceEmitterGeometryDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleGpuData.size(), m_SurfaceEmitterTriangleDirtyBegin, m_SurfaceEmitterTriangleDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleCdfGpuData.size(), m_SurfaceEmitterTriangleCdfDirtyBegin, m_SurfaceEmitterTriangleCdfDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterInstanceGpuData.size(), m_SurfaceEmitterInstanceDirtyBegin, m_SurfaceEmitterInstanceDirtyEnd);
    MarkDirectLightSamplingDirty();
}

void LightingGpuResources::SetLightGroupSettings(
    const bool directionalLightsEnabled,
    const bool pointLightsEnabled,
    const bool areaLightsEnabled)
{
    if (m_DirectionalLightsEnabled == directionalLightsEnabled &&
        m_PointLightsEnabled == pointLightsEnabled &&
        m_AreaLightsEnabled == areaLightsEnabled)
    {
        return;
    }

    m_DirectionalLightsEnabled = directionalLightsEnabled;
    m_PointLightsEnabled = pointLightsEnabled;
    m_AreaLightsEnabled = areaLightsEnabled;
    MarkDirectLightSamplingDirty();
}

void LightingGpuResources::UpdateDirectionalLight(const DirectionalLight& light, const size_t lightIndex)
{
    if (m_DirectionalLightGpuData.size() <= lightIndex)
    {
        m_DirectionalLightGpuData.resize(lightIndex + 1);
    }

    DirectionalLightData& gpuLight = m_DirectionalLightGpuData[lightIndex];
    gpuLight.DirectionAndAngularRadius = light.m_DirectionWs;
    gpuLight.ColorAndIntensity = light.m_Color;
    MarkDirectionalLightsDirty(lightIndex, lightIndex + 1, true);
}

void LightingGpuResources::UpdatePointLight(
    const PointLight& light,
    const size_t lightIndex,
    const bool updateSamplingDistribution)
{
    if (m_PointLightGpuData.size() <= lightIndex)
    {
        m_PointLightGpuData.resize(lightIndex + 1);
    }

    PointLightData& gpuLight = m_PointLightGpuData[lightIndex];
    gpuLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
    gpuLight.ColorAndIntensity = light.Color;
    gpuLight.Attenuation = { light.ConstantAttenuation, light.LinearAttenuation, light.QuadraticAttenuation, light.SourceRadius };
    MarkPointLightsDirty(lightIndex, lightIndex + 1, updateSamplingDistribution);
}

void LightingGpuResources::UpdateAreaLight(const AreaLightData& light, const size_t lightIndex)
{
    if (m_AreaLights.size() <= lightIndex)
    {
        return;
    }

    m_AreaLights[lightIndex] = light;
    UpdateAreaLightSurfaceEmitter(lightIndex);
}

void LightingGpuResources::Initialize(CommandList& commandList)
{
    m_UploadBuffer = std::make_unique<SharedUploadBuffer>(m_DeviceContext);
    m_DirectionalLightBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_DirectionalLightGpuData.size());
    m_PointLightBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_PointLightGpuData.size());
    m_SurfaceEmitterGeometryBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_SurfaceEmitterGeometryGpuData.size());
    m_SurfaceEmitterTriangleBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_SurfaceEmitterTriangleGpuData.size());
    m_SurfaceEmitterTriangleCdfBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_SurfaceEmitterTriangleCdfGpuData.size());
    m_SurfaceEmitterInstanceBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_SurfaceEmitterInstanceGpuData.size());
    m_DirectLightCdfBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_DirectLightCdfGpuData.size());
    commandList.CopyStructuredBuffer(m_DirectionalLightBuffer, CreateBufferCapacityData<DirectionalLightData>(m_DirectionalLightBufferCapacity));
    commandList.CopyStructuredBuffer(m_PointLightBuffer, CreateBufferCapacityData<PointLightData>(m_PointLightBufferCapacity));
    commandList.CopyStructuredBuffer(m_SurfaceEmitterGeometryBuffer, CreateBufferCapacityData<SurfaceEmitterGeometryData>(m_SurfaceEmitterGeometryBufferCapacity));
    commandList.CopyStructuredBuffer(m_SurfaceEmitterTriangleBuffer, CreateBufferCapacityData<SurfaceEmitterTriangleData>(m_SurfaceEmitterTriangleBufferCapacity));
    commandList.CopyStructuredBuffer(m_SurfaceEmitterTriangleCdfBuffer, CreateBufferCapacityData<float>(m_SurfaceEmitterTriangleCdfBufferCapacity));
    commandList.CopyStructuredBuffer(m_SurfaceEmitterInstanceBuffer, CreateBufferCapacityData<SurfaceEmitterInstanceData>(m_SurfaceEmitterInstanceBufferCapacity));
    commandList.CopyStructuredBuffer(m_DirectLightCdfBuffer, CreateBufferCapacityData<float>(m_DirectLightCdfBufferCapacity));
}

bool LightingGpuResources::Upload(CommandList& commandList, const uint64_t frameIndex)
{
    Assert(m_UploadBuffer != nullptr, "Light upload buffer is not initialized.");

    const bool directionalLightsRecreated = EnsureStructuredBufferCapacity(commandList, m_DirectionalLightBuffer, m_DirectionalLightBufferCapacity, m_DirectionalLightGpuData);
    const bool pointLightsRecreated = EnsureStructuredBufferCapacity(commandList, m_PointLightBuffer, m_PointLightBufferCapacity, m_PointLightGpuData);
    const bool surfaceEmitterGeometriesRecreated = EnsureStructuredBufferCapacity(commandList, m_SurfaceEmitterGeometryBuffer, m_SurfaceEmitterGeometryBufferCapacity, m_SurfaceEmitterGeometryGpuData);
    const bool surfaceEmitterTrianglesRecreated = EnsureStructuredBufferCapacity(commandList, m_SurfaceEmitterTriangleBuffer, m_SurfaceEmitterTriangleBufferCapacity, m_SurfaceEmitterTriangleGpuData);
    const bool surfaceEmitterTriangleCdfRecreated = EnsureStructuredBufferCapacity(commandList, m_SurfaceEmitterTriangleCdfBuffer, m_SurfaceEmitterTriangleCdfBufferCapacity, m_SurfaceEmitterTriangleCdfGpuData);
    const bool surfaceEmitterInstancesRecreated = EnsureStructuredBufferCapacity(commandList, m_SurfaceEmitterInstanceBuffer, m_SurfaceEmitterInstanceBufferCapacity, m_SurfaceEmitterInstanceGpuData);
    const bool directLightCdfRecreated = EnsureStructuredBufferCapacity(commandList, m_DirectLightCdfBuffer, m_DirectLightCdfBufferCapacity, m_DirectLightCdfGpuData);

    if (directionalLightsRecreated)
    {
        m_DirectionalLightDirtyBegin = 0;
        m_DirectionalLightDirtyEnd = 0;
    }
    if (pointLightsRecreated)
    {
        m_PointLightDirtyBegin = 0;
        m_PointLightDirtyEnd = 0;
    }
    if (surfaceEmitterGeometriesRecreated)
    {
        m_SurfaceEmitterGeometryDirtyBegin = 0;
        m_SurfaceEmitterGeometryDirtyEnd = 0;
    }
    if (surfaceEmitterTrianglesRecreated)
    {
        m_SurfaceEmitterTriangleDirtyBegin = 0;
        m_SurfaceEmitterTriangleDirtyEnd = 0;
    }
    if (surfaceEmitterTriangleCdfRecreated)
    {
        m_SurfaceEmitterTriangleCdfDirtyBegin = 0;
        m_SurfaceEmitterTriangleCdfDirtyEnd = 0;
    }
    if (surfaceEmitterInstancesRecreated)
    {
        m_SurfaceEmitterInstanceDirtyBegin = 0;
        m_SurfaceEmitterInstanceDirtyEnd = 0;
    }
    if (directLightCdfRecreated)
    {
        m_DirectLightCdfDirtyBegin = 0;
        m_DirectLightCdfDirtyEnd = 0;
    }

    m_UploadBuffer->BeginFrame(frameIndex);
    if (m_DirectionalLightDirtyBegin < m_DirectionalLightDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_DirectionalLightBuffer, m_DirectionalLightGpuData, m_DirectionalLightDirtyBegin, m_DirectionalLightDirtyEnd);
        m_DirectionalLightDirtyBegin = 0;
        m_DirectionalLightDirtyEnd = 0;
    }
    if (m_PointLightDirtyBegin < m_PointLightDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_PointLightBuffer, m_PointLightGpuData, m_PointLightDirtyBegin, m_PointLightDirtyEnd);
        m_PointLightDirtyBegin = 0;
        m_PointLightDirtyEnd = 0;
    }
    if (m_SurfaceEmitterGeometryDirtyBegin < m_SurfaceEmitterGeometryDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_SurfaceEmitterGeometryBuffer, m_SurfaceEmitterGeometryGpuData, m_SurfaceEmitterGeometryDirtyBegin, m_SurfaceEmitterGeometryDirtyEnd);
        m_SurfaceEmitterGeometryDirtyBegin = 0;
        m_SurfaceEmitterGeometryDirtyEnd = 0;
    }
    if (m_SurfaceEmitterTriangleDirtyBegin < m_SurfaceEmitterTriangleDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_SurfaceEmitterTriangleBuffer, m_SurfaceEmitterTriangleGpuData, m_SurfaceEmitterTriangleDirtyBegin, m_SurfaceEmitterTriangleDirtyEnd);
        m_SurfaceEmitterTriangleDirtyBegin = 0;
        m_SurfaceEmitterTriangleDirtyEnd = 0;
    }
    if (m_SurfaceEmitterTriangleCdfDirtyBegin < m_SurfaceEmitterTriangleCdfDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_SurfaceEmitterTriangleCdfBuffer, m_SurfaceEmitterTriangleCdfGpuData, m_SurfaceEmitterTriangleCdfDirtyBegin, m_SurfaceEmitterTriangleCdfDirtyEnd);
        m_SurfaceEmitterTriangleCdfDirtyBegin = 0;
        m_SurfaceEmitterTriangleCdfDirtyEnd = 0;
    }
    if (m_SurfaceEmitterInstanceDirtyBegin < m_SurfaceEmitterInstanceDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_SurfaceEmitterInstanceBuffer, m_SurfaceEmitterInstanceGpuData, m_SurfaceEmitterInstanceDirtyBegin, m_SurfaceEmitterInstanceDirtyEnd);
        m_SurfaceEmitterInstanceDirtyBegin = 0;
        m_SurfaceEmitterInstanceDirtyEnd = 0;
    }
    if (m_DirectLightCdfDirtyBegin < m_DirectLightCdfDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_DirectLightCdfBuffer, m_DirectLightCdfGpuData, m_DirectLightCdfDirtyBegin, m_DirectLightCdfDirtyEnd);
        m_DirectLightCdfDirtyBegin = 0;
        m_DirectLightCdfDirtyEnd = 0;
    }

    return directionalLightsRecreated || pointLightsRecreated ||
        surfaceEmitterGeometriesRecreated || surfaceEmitterTrianglesRecreated ||
        surfaceEmitterTriangleCdfRecreated || surfaceEmitterInstancesRecreated || directLightCdfRecreated;
}

void LightingGpuResources::BindComputeResources(CommandContext& commandContext, ComputeShader& shader)
{
    if (shader.HasShaderResourceView("DirectionalLights"))
    {
        commandContext.SetShaderResource(shader, "DirectionalLights", 0u, m_DirectionalLightBuffer);
    }
    if (shader.HasShaderResourceView("PointLights"))
    {
        commandContext.SetShaderResource(shader, "PointLights", 0u, m_PointLightBuffer);
    }
    if (shader.HasShaderResourceView("SurfaceEmitterGeometries"))
    {
        commandContext.SetShaderResource(shader, "SurfaceEmitterGeometries", 0u, m_SurfaceEmitterGeometryBuffer);
    }
    if (shader.HasShaderResourceView("SurfaceEmitterInstances"))
    {
        commandContext.SetShaderResource(shader, "SurfaceEmitterInstances", 0u, m_SurfaceEmitterInstanceBuffer);
    }
    if (shader.HasShaderResourceView("SurfaceEmitterTriangles"))
    {
        commandContext.SetShaderResource(shader, "SurfaceEmitterTriangles", 0u, m_SurfaceEmitterTriangleBuffer);
    }
    if (shader.HasShaderResourceView("SurfaceEmitterTriangleCdf"))
    {
        commandContext.SetShaderResource(shader, "SurfaceEmitterTriangleCdf", 0u, m_SurfaceEmitterTriangleCdfBuffer);
    }
    if (shader.HasShaderResourceView("DirectLightCdf"))
    {
        commandContext.SetShaderResource(shader, "DirectLightCdf", 0u, m_DirectLightCdfBuffer);
    }
}

//Modify Begin:2026-08-13 by BestHui
void LightingGpuResources::ForEachShaderResource(
    const std::function<void(const Resource&)>& action) const
{
    action(m_DirectionalLightBuffer);
    action(m_PointLightBuffer);
    action(m_SurfaceEmitterGeometryBuffer);
    action(m_SurfaceEmitterInstanceBuffer);
    action(m_SurfaceEmitterTriangleBuffer);
    action(m_SurfaceEmitterTriangleCdfBuffer);
    action(m_DirectLightCdfBuffer);
}
//Modify End

void LightingGpuResources::BindRayTracingResources(RayTracingBindingSet& bindingSet)
{
    if (bindingSet.HasBinding("DirectionalLights"))
    {
        bindingSet.SetBuffer("DirectionalLights", m_DirectionalLightBuffer);
    }
    if (bindingSet.HasBinding("PointLights"))
    {
        bindingSet.SetBuffer("PointLights", m_PointLightBuffer);
    }
    if (bindingSet.HasBinding("SurfaceEmitterGeometries"))
    {
        bindingSet.SetBuffer("SurfaceEmitterGeometries", m_SurfaceEmitterGeometryBuffer);
    }
    if (bindingSet.HasBinding("SurfaceEmitterInstances"))
    {
        bindingSet.SetBuffer("SurfaceEmitterInstances", m_SurfaceEmitterInstanceBuffer);
    }
    if (bindingSet.HasBinding("SurfaceEmitterTriangles"))
    {
        bindingSet.SetBuffer("SurfaceEmitterTriangles", m_SurfaceEmitterTriangleBuffer);
    }
    if (bindingSet.HasBinding("SurfaceEmitterTriangleCdf"))
    {
        bindingSet.SetBuffer("SurfaceEmitterTriangleCdf", m_SurfaceEmitterTriangleCdfBuffer);
    }
    if (bindingSet.HasBinding("DirectLightCdf"))
    {
        bindingSet.SetBuffer("DirectLightCdf", m_DirectLightCdfBuffer);
    }
}

uint32_t LightingGpuResources::GetDirectionalLightCount() const
{
    return static_cast<uint32_t>(m_DirectionalLightGpuData.size());
}

uint32_t LightingGpuResources::GetPointLightCount() const
{
    return static_cast<uint32_t>(m_PointLightGpuData.size());
}

uint32_t LightingGpuResources::GetSurfaceEmitterCount() const
{
    return static_cast<uint32_t>(m_SurfaceEmitterInstanceGpuData.size());
}

size_t LightingGpuResources::GetMeshSurfaceEmitterCount() const
{
    return m_MeshSurfaceEmitterData.Instances.size();
}

void LightingGpuResources::BuildGpuData(
    const std::vector<DirectionalLight>& directionalLights,
    const std::vector<PointLight>& pointLights)
{
    m_DirectionalLightGpuData.clear();
    m_DirectionalLightGpuData.reserve(directionalLights.size());
    for (const DirectionalLight& light : directionalLights)
    {
        DirectionalLightData gpuLight{};
        gpuLight.DirectionAndAngularRadius = light.m_DirectionWs;
        gpuLight.ColorAndIntensity = light.m_Color;
        m_DirectionalLightGpuData.push_back(gpuLight);
    }

    m_PointLightGpuData.clear();
    m_PointLightGpuData.reserve(pointLights.size());
    for (const PointLight& light : pointLights)
    {
        PointLightData gpuLight{};
        gpuLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
        gpuLight.ColorAndIntensity = light.Color;
        gpuLight.Attenuation = { light.ConstantAttenuation, light.LinearAttenuation, light.QuadraticAttenuation, light.SourceRadius };
        m_PointLightGpuData.push_back(gpuLight);
    }

    RebuildSurfaceEmitterGpuData();
    RebuildDirectLightSamplingCdf();
}

void LightingGpuResources::RebuildSurfaceEmitterGpuData()
{
    m_SurfaceEmitterGeometryGpuData = m_MeshSurfaceEmitterData.Geometries;
    m_SurfaceEmitterTriangleGpuData = m_MeshSurfaceEmitterData.Triangles;
    m_SurfaceEmitterTriangleCdfGpuData = m_MeshSurfaceEmitterData.TriangleCdf;
    m_SurfaceEmitterInstanceGpuData = m_MeshSurfaceEmitterData.Instances;
    m_RectangleSurfaceEmitterOffset = m_SurfaceEmitterInstanceGpuData.size();
    m_RectangleEmitterGeometryIndex = SurfaceEmitterInvalidMaterialIndex;

    if (m_AreaLights.empty())
    {
        return;
    }

    SurfaceEmitterGeometryData rectangleGeometry{};
    rectangleGeometry.TriangleOffset = static_cast<uint32_t>(m_SurfaceEmitterTriangleGpuData.size());
    rectangleGeometry.TriangleCdfOffset = static_cast<uint32_t>(m_SurfaceEmitterTriangleCdfGpuData.size());
    rectangleGeometry.TriangleCount = 2u;
    m_RectangleEmitterGeometryIndex = static_cast<uint32_t>(m_SurfaceEmitterGeometryGpuData.size());
    m_SurfaceEmitterGeometryGpuData.push_back(rectangleGeometry);
    m_SurfaceEmitterTriangleGpuData.push_back(CreateSurfaceEmitterTriangle(
        { -1.0f, -1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }));
    m_SurfaceEmitterTriangleGpuData.push_back(CreateSurfaceEmitterTriangle(
        { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, { -1.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }));
    m_SurfaceEmitterTriangleCdfGpuData.push_back(0.5f);
    m_SurfaceEmitterTriangleCdfGpuData.push_back(1.0f);

    m_SurfaceEmitterInstanceGpuData.reserve(m_RectangleSurfaceEmitterOffset + m_AreaLights.size());
    for (const AreaLightData& light : m_AreaLights)
    {
        m_SurfaceEmitterInstanceGpuData.push_back(CreateRectangleSurfaceEmitterInstance(light, m_RectangleEmitterGeometryIndex));
    }

    for (SurfaceEmitterInstanceData& instance : m_SurfaceEmitterInstanceGpuData)
    {
        if (instance.GeometryIndex < m_SurfaceEmitterGeometryGpuData.size())
        {
            instance.SurfaceArea = GetSurfaceEmitterWorldArea(
                instance,
                m_SurfaceEmitterGeometryGpuData[instance.GeometryIndex],
                m_SurfaceEmitterTriangleGpuData);
        }
    }
}

void LightingGpuResources::UpdateAreaLightSurfaceEmitter(const size_t lightIndex)
{
    const size_t instanceIndex = m_RectangleSurfaceEmitterOffset + lightIndex;
    if (lightIndex >= m_AreaLights.size() ||
        m_RectangleEmitterGeometryIndex == SurfaceEmitterInvalidMaterialIndex ||
        instanceIndex >= m_SurfaceEmitterInstanceGpuData.size())
    {
        return;
    }

    m_SurfaceEmitterInstanceGpuData[instanceIndex] = CreateRectangleSurfaceEmitterInstance(
        m_AreaLights[lightIndex],
        m_RectangleEmitterGeometryIndex);
    m_SurfaceEmitterInstanceGpuData[instanceIndex].SurfaceArea = GetSurfaceEmitterWorldArea(
        m_SurfaceEmitterInstanceGpuData[instanceIndex],
        m_SurfaceEmitterGeometryGpuData[m_RectangleEmitterGeometryIndex],
        m_SurfaceEmitterTriangleGpuData);
    MarkDirtyRange(instanceIndex, instanceIndex + 1, m_SurfaceEmitterInstanceDirtyBegin, m_SurfaceEmitterInstanceDirtyEnd);
    MarkDirectLightSamplingDirty();
}

void LightingGpuResources::RebuildDirectLightSamplingCdf()
{
    m_DirectLightCdfGpuData.clear();
    m_DirectLightCdfGpuData.reserve(
        m_DirectionalLightGpuData.size() +
        m_PointLightGpuData.size() +
        m_SurfaceEmitterInstanceGpuData.size());

    float totalWeight = 0.0f;
    const auto appendWeight = [this, &totalWeight](const float weight, const bool enabled)
    {
        if (enabled)
        {
            totalWeight += std::max(1.0e-4f, weight);
        }
        m_DirectLightCdfGpuData.push_back(totalWeight);
    };

    for (const DirectionalLightData& light : m_DirectionalLightGpuData)
    {
        appendWeight(
            (light.ColorAndIntensity.x * 0.2126f +
             light.ColorAndIntensity.y * 0.7152f +
             light.ColorAndIntensity.z * 0.0722f) *
            light.ColorAndIntensity.w,
            m_DirectionalLightsEnabled);
    }
    for (const PointLightData& light : m_PointLightGpuData)
    {
        appendWeight(
            (light.ColorAndIntensity.x * 0.2126f +
             light.ColorAndIntensity.y * 0.7152f +
             light.ColorAndIntensity.z * 0.0722f) *
            light.ColorAndIntensity.w,
            m_PointLightsEnabled);
    }
    for (const SurfaceEmitterInstanceData& instance : m_SurfaceEmitterInstanceGpuData)
    {
        const float luminance =
            instance.EmissionAndIntensity.x * 0.2126f +
            instance.EmissionAndIntensity.y * 0.7152f +
            instance.EmissionAndIntensity.z * 0.0722f;
        const float intensity = std::max(0.0f, instance.EmissionAndIntensity.w);
        appendWeight(luminance * intensity * instance.SurfaceArea, m_AreaLightsEnabled);
    }

    if (totalWeight > 0.0f)
    {
        for (float& value : m_DirectLightCdfGpuData)
        {
            value /= totalWeight;
        }
    }
}

void LightingGpuResources::MarkDirectLightSamplingDirty()
{
    RebuildDirectLightSamplingCdf();
    MarkDirtyRange(0, m_DirectLightCdfGpuData.size(), m_DirectLightCdfDirtyBegin, m_DirectLightCdfDirtyEnd);
}

void LightingGpuResources::MarkAllGpuDataDirty()
{
    MarkDirtyRange(0, m_DirectionalLightGpuData.size(), m_DirectionalLightDirtyBegin, m_DirectionalLightDirtyEnd);
    MarkDirtyRange(0, m_PointLightGpuData.size(), m_PointLightDirtyBegin, m_PointLightDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterGeometryGpuData.size(), m_SurfaceEmitterGeometryDirtyBegin, m_SurfaceEmitterGeometryDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleGpuData.size(), m_SurfaceEmitterTriangleDirtyBegin, m_SurfaceEmitterTriangleDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleCdfGpuData.size(), m_SurfaceEmitterTriangleCdfDirtyBegin, m_SurfaceEmitterTriangleCdfDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterInstanceGpuData.size(), m_SurfaceEmitterInstanceDirtyBegin, m_SurfaceEmitterInstanceDirtyEnd);
    MarkDirtyRange(0, m_DirectLightCdfGpuData.size(), m_DirectLightCdfDirtyBegin, m_DirectLightCdfDirtyEnd);
}

void LightingGpuResources::MarkDirectionalLightsDirty(
    const size_t beginIndex,
    const size_t endIndex,
    const bool updateSamplingDistribution)
{
    MarkDirtyRange(beginIndex, endIndex, m_DirectionalLightDirtyBegin, m_DirectionalLightDirtyEnd);
    if (updateSamplingDistribution)
    {
        MarkDirectLightSamplingDirty();
    }
}

void LightingGpuResources::MarkPointLightsDirty(
    const size_t beginIndex,
    const size_t endIndex,
    const bool updateSamplingDistribution)
{
    MarkDirtyRange(beginIndex, endIndex, m_PointLightDirtyBegin, m_PointLightDirtyEnd);
    if (updateSamplingDistribution)
    {
        MarkDirectLightSamplingDirty();
    }
}
//Modify End
