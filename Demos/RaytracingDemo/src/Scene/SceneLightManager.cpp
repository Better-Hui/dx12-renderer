//Modify Begin:2026-07-27 by BestHui
#include <Scene/SceneLightManager.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>

#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Pipeline/ComputeShader.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Core/FrameworkDeviceContext.h>
//Modify End
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>

#include <DirectXMath.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace DirectX;

namespace
{
//Modify Begin:2026-07-30 by BestHui
    constexpr size_t InitialLightBufferCapacity = 1;
//Modify End

    size_t GrowLightBufferCapacity(const size_t currentCapacity, const size_t requiredCapacity)
    {
//Modify Begin:2026-07-30 by BestHui
        size_t newCapacity = std::max<size_t>(InitialLightBufferCapacity, currentCapacity);
//Modify End
        while (newCapacity < requiredCapacity)
        {
            newCapacity *= 2;
        }
        return newCapacity;
    }

    void MarkDirtyRange(
        const size_t beginIndex,
        const size_t endIndex,
        size_t& dirtyBegin,
        size_t& dirtyEnd)
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
    bool EnsureStructuredBufferCapacity(
        CommandList& commandList,
        StructuredBuffer& buffer,
        size_t& currentCapacity,
        const std::vector<T>& values)
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

//Modify Begin:2026-07-30 by BestHui
    XMFLOAT3 NormalizeVector(const XMFLOAT3& value)
    {
        const XMVECTOR vector = XMLoadFloat3(&value);
        if (XMVectorGetX(XMVector3LengthSq(vector)) <= 1.0e-8f)
        {
            return { 0.0f, 1.0f, 0.0f };
        }

        XMFLOAT3 result{};
        XMStoreFloat3(&result, XMVector3Normalize(vector));
        return result;
    }

    void BuildAreaLightAxes(const XMFLOAT3& normal, XMFLOAT3& axisU, XMFLOAT3& axisV)
    {
        const XMVECTOR normalVector = XMLoadFloat3(&normal);
        const XMVECTOR reference = std::abs(normal.y) < 0.99f ?
            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) :
            XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        const XMVECTOR axisUVector = XMVector3Normalize(XMVector3Cross(reference, normalVector));
        const XMVECTOR axisVVector = XMVector3Normalize(XMVector3Cross(normalVector, axisUVector));
        XMStoreFloat3(&axisU, axisUVector);
        XMStoreFloat3(&axisV, axisVVector);
    }

    template<typename T>
    void EraseAt(std::vector<T>& values, const size_t index)
    {
        if (index < values.size())
        {
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

//Modify Begin:2026-08-06 by BestHui
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

    SurfaceEmitterInstanceData CreateRectangleSurfaceEmitterInstance(
        const AreaLightData& light,
        const uint32_t geometryIndex)
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
//Modify End
//Modify End
}

SceneLightManager::SceneLightManager(FrameworkDeviceContext& deviceContext)
    : m_DeviceContext(deviceContext)
    , m_DirectionalLightBuffer(L"Ray Tracing Directional Lights")
    , m_PointLightBuffer(L"Ray Tracing Point Lights")
    , m_SurfaceEmitterGeometryBuffer(L"Surface Emitter Geometries")
    , m_SurfaceEmitterTriangleBuffer(L"Surface Emitter Triangles")
    , m_SurfaceEmitterTriangleCdfBuffer(L"Surface Emitter Triangle CDF")
    , m_SurfaceEmitterInstanceBuffer(L"Surface Emitter Instances")
//Modify Begin:2026-08-06 by BestHui
    , m_DirectLightCdfBuffer(L"Ray Tracing Direct Light CDF")
//Modify End
{
}

void SceneLightManager::CreateDemoLights()
{
    m_DirectionalLights.clear();
    m_PointLights.clear();
    m_AreaLights.clear();
//Modify Begin:2026-08-06 by BestHui
    m_MeshSurfaceEmitterData = {};
//Modify End
    m_PointLightBaseY.clear();
    m_PointLightPhase.clear();
    m_PointLightOrbitRadius.clear();
    m_PointLightOrbitSpeed.clear();
    m_PointLightOrbitCenter.clear();
    m_PointLightAnimated.clear();

    m_SkyLight.ColorAndIntensity = { 0.85f, 0.9f, 1.0f, 0.35f };
    m_SkyLightColor = { m_SkyLight.ColorAndIntensity.x, m_SkyLight.ColorAndIntensity.y, m_SkyLight.ColorAndIntensity.z };
    m_SkyLightIntensity = m_SkyLight.ColorAndIntensity.w;

    DirectionalLight sunLight{};
    sunLight.m_DirectionWs = { -0.35f, 0.8f, -0.48f, 0.0f };
    sunLight.m_Color = { 1.0f, 0.95f, 0.82f, 0.8f };
    m_DirectionalLights.push_back(sunLight);

    constexpr uint32_t DemoPointLightCount = 1;
    const XMFLOAT3 orbitCenter = { -12.0f, 6.0f, 18.0f };
    const XMFLOAT4 lightColors[DemoPointLightCount] = {
        { 1.0f, 0.35f, 0.28f, 20.0f },
    };

    m_PointLights.reserve(DemoPointLightCount);
    m_PointLightBaseY.reserve(DemoPointLightCount);
    m_PointLightPhase.reserve(DemoPointLightCount);
    m_PointLightOrbitRadius.reserve(DemoPointLightCount);
    m_PointLightOrbitSpeed.reserve(DemoPointLightCount);
    m_PointLightOrbitCenter.reserve(DemoPointLightCount);
    m_PointLightAnimated.reserve(DemoPointLightCount);

    for (uint32_t i = 0; i < DemoPointLightCount; ++i)
    {
        const float baseY = orbitCenter.y + static_cast<float>(i % 2) * 1.25f;
        const float phase = XM_2PI * static_cast<float>(i) / static_cast<float>(DemoPointLightCount);
        const float radius = 13.0f + static_cast<float>(i) * 2.0f;
        const float speed = 0.35f + static_cast<float>(i) * 0.07f;

        PointLight light(
            {
                orbitCenter.x + std::cos(phase) * radius,
                baseY,
                orbitCenter.z + std::sin(phase) * radius,
                1.0f
            },
            26.0f);
        light.Color = lightColors[i];
        light.RecalculateAttenuationCoefficients();

        m_PointLights.push_back(light);
        m_PointLightBaseY.push_back(baseY);
        m_PointLightPhase.push_back(phase);
        m_PointLightOrbitRadius.push_back(radius);
        m_PointLightOrbitSpeed.push_back(speed);
        m_PointLightOrbitCenter.push_back(orbitCenter);
        m_PointLightAnimated.push_back(1);
    }

    AreaLightData areaLight{};
    areaLight.PositionAndRange = { -18.0f, 10.0f, 18.0f, 35.0f };
    areaLight.NormalAndType = { 0.0f, -1.0f, 0.0f, 0.0f };
    areaLight.AxisUAndExtent = { 1.0f, 0.0f, 0.0f, 4.0f };
    areaLight.AxisVAndExtent = { 0.0f, 0.0f, 1.0f, 3.0f };
    areaLight.ColorAndIntensity = { 1.0f, 0.82f, 0.55f, 6.0f };
    m_AreaLights.push_back(areaLight);

    BuildGpuData();
    MarkDirectionalLightsDirty();
    MarkPointLightsDirty(0, m_PointLightGpuData.size());
    MarkDirtyRange(0, m_SurfaceEmitterGeometryGpuData.size(), m_SurfaceEmitterGeometryDirtyBegin, m_SurfaceEmitterGeometryDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleGpuData.size(), m_SurfaceEmitterTriangleDirtyBegin, m_SurfaceEmitterTriangleDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleCdfGpuData.size(), m_SurfaceEmitterTriangleCdfDirtyBegin, m_SurfaceEmitterTriangleCdfDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterInstanceGpuData.size(), m_SurfaceEmitterInstanceDirtyBegin, m_SurfaceEmitterInstanceDirtyEnd);
}

void SceneLightManager::CreateFromScene(const Scene& scene)
{
    m_DirectionalLights.clear();
    m_PointLights.clear();
    m_AreaLights.clear();
//Modify Begin:2026-08-06 by BestHui
    m_MeshSurfaceEmitterData = {};
//Modify End
    m_PointLightBaseY.clear();
    m_PointLightPhase.clear();
    m_PointLightOrbitRadius.clear();
    m_PointLightOrbitSpeed.clear();
    m_PointLightOrbitCenter.clear();
    m_PointLightAnimated.clear();

//Modify Begin:2026-08-06 by BestHui
    const SceneLightGroupSettings& lightGroups = scene.GetLightGroupSettings();
    m_DirectionalLightsEnabled = lightGroups.DirectionalLightsEnabled;
    m_PointLightsEnabled = lightGroups.PointLightsEnabled;
    m_AreaLightsEnabled = lightGroups.AreaLightsEnabled;
//Modify End

//Modify Begin:2026-07-30 by BestHui
    m_SkyLight.ColorAndIntensity = scene.GetSkybox().AmbientColorAndIntensity;
    if (scene.GetSkybox().Texture.IsValid() &&
        m_SkyLight.ColorAndIntensity.x <= 0.0f &&
        m_SkyLight.ColorAndIntensity.y <= 0.0f &&
        m_SkyLight.ColorAndIntensity.z <= 0.0f)
    {
        m_SkyLight.ColorAndIntensity = { 1.0f, 1.0f, 1.0f, std::max(0.001f, m_SkyLight.ColorAndIntensity.w) };
    }
    m_SkyLightColor = { m_SkyLight.ColorAndIntensity.x, m_SkyLight.ColorAndIntensity.y, m_SkyLight.ColorAndIntensity.z };
    m_SkyLightIntensity = m_SkyLight.ColorAndIntensity.w;
//Modify End

    for (const DirectionalLight& light : scene.GetDirectionalLights())
    {
        m_DirectionalLights.push_back(light);
    }

    for (const PointLight& light : scene.GetPointLights())
    {
        m_PointLights.push_back(light);
        m_PointLightBaseY.push_back(light.PositionWs.y);
        m_PointLightPhase.push_back(0.0f);
        m_PointLightOrbitRadius.push_back(0.0f);
        m_PointLightOrbitSpeed.push_back(0.0f);
        m_PointLightOrbitCenter.push_back({ light.PositionWs.x, light.PositionWs.y, light.PositionWs.z });
        m_PointLightAnimated.push_back(0);
    }

    for (const AreaLight& light : scene.GetAreaLights())
    {
        AreaLightData areaLight{};
        areaLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
        areaLight.NormalAndType = light.NormalWs;
        areaLight.AxisUAndExtent = light.AxisUWsAndExtent;
        areaLight.AxisVAndExtent = light.AxisVWsAndExtent;
        areaLight.ColorAndIntensity = light.Color;
        m_AreaLights.push_back(areaLight);
    }

    BuildGpuData();
    MarkDirectionalLightsDirty();
    MarkPointLightsDirty(0, m_PointLightGpuData.size());
    MarkDirtyRange(0, m_SurfaceEmitterGeometryGpuData.size(), m_SurfaceEmitterGeometryDirtyBegin, m_SurfaceEmitterGeometryDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleGpuData.size(), m_SurfaceEmitterTriangleDirtyBegin, m_SurfaceEmitterTriangleDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleCdfGpuData.size(), m_SurfaceEmitterTriangleCdfDirtyBegin, m_SurfaceEmitterTriangleCdfDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterInstanceGpuData.size(), m_SurfaceEmitterInstanceDirtyBegin, m_SurfaceEmitterInstanceDirtyEnd);
}

//Modify Begin:2026-08-06 by BestHui
void SceneLightManager::SetEmissiveMeshSurfaceEmitters(SurfaceEmitterSceneData emitterData)
{
    m_MeshSurfaceEmitterData = std::move(emitterData);
    RebuildSurfaceEmitterGpuData();
    MarkDirtyRange(0, m_SurfaceEmitterGeometryGpuData.size(), m_SurfaceEmitterGeometryDirtyBegin, m_SurfaceEmitterGeometryDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleGpuData.size(), m_SurfaceEmitterTriangleDirtyBegin, m_SurfaceEmitterTriangleDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleCdfGpuData.size(), m_SurfaceEmitterTriangleCdfDirtyBegin, m_SurfaceEmitterTriangleCdfDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterInstanceGpuData.size(), m_SurfaceEmitterInstanceDirtyBegin, m_SurfaceEmitterInstanceDirtyEnd);
    MarkDirectLightSamplingDirty();
}
//Modify End

void SceneLightManager::InitializeGpuBuffers(CommandList& commandList)
{
    m_UploadBuffer = std::make_unique<SharedUploadBuffer>(m_DeviceContext);
//Modify Begin:2026-07-30 by BestHui
    m_DirectionalLightBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_DirectionalLightGpuData.size());
    m_PointLightBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_PointLightGpuData.size());
    m_SurfaceEmitterGeometryBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_SurfaceEmitterGeometryGpuData.size());
    m_SurfaceEmitterTriangleBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_SurfaceEmitterTriangleGpuData.size());
    m_SurfaceEmitterTriangleCdfBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_SurfaceEmitterTriangleCdfGpuData.size());
    m_SurfaceEmitterInstanceBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_SurfaceEmitterInstanceGpuData.size());
//Modify Begin:2026-08-06 by BestHui
    m_DirectLightCdfBufferCapacity = std::max<size_t>(InitialLightBufferCapacity, m_DirectLightCdfGpuData.size());
//Modify End
//Modify End
    commandList.CopyStructuredBuffer(m_DirectionalLightBuffer, CreateBufferCapacityData<DirectionalLightData>(m_DirectionalLightBufferCapacity));
    commandList.CopyStructuredBuffer(m_PointLightBuffer, CreateBufferCapacityData<PointLightData>(m_PointLightBufferCapacity));
    commandList.CopyStructuredBuffer(m_SurfaceEmitterGeometryBuffer, CreateBufferCapacityData<SurfaceEmitterGeometryData>(m_SurfaceEmitterGeometryBufferCapacity));
    commandList.CopyStructuredBuffer(m_SurfaceEmitterTriangleBuffer, CreateBufferCapacityData<SurfaceEmitterTriangleData>(m_SurfaceEmitterTriangleBufferCapacity));
    commandList.CopyStructuredBuffer(m_SurfaceEmitterTriangleCdfBuffer, CreateBufferCapacityData<float>(m_SurfaceEmitterTriangleCdfBufferCapacity));
    commandList.CopyStructuredBuffer(m_SurfaceEmitterInstanceBuffer, CreateBufferCapacityData<SurfaceEmitterInstanceData>(m_SurfaceEmitterInstanceBufferCapacity));
//Modify Begin:2026-08-06 by BestHui
    commandList.CopyStructuredBuffer(m_DirectLightCdfBuffer, CreateBufferCapacityData<float>(m_DirectLightCdfBufferCapacity));
//Modify End
}

//Modify Begin:2026-07-30 by BestHui
bool SceneLightManager::Upload(CommandList& commandList, const uint64_t frameIndex)
//Modify End
{
    Assert(m_UploadBuffer != nullptr, "Light upload buffer is not initialized.");

    const bool directionalLightsRecreated = EnsureStructuredBufferCapacity(commandList, m_DirectionalLightBuffer, m_DirectionalLightBufferCapacity, m_DirectionalLightGpuData);
    const bool pointLightsRecreated = EnsureStructuredBufferCapacity(commandList, m_PointLightBuffer, m_PointLightBufferCapacity, m_PointLightGpuData);
    const bool surfaceEmitterGeometriesRecreated = EnsureStructuredBufferCapacity(commandList, m_SurfaceEmitterGeometryBuffer, m_SurfaceEmitterGeometryBufferCapacity, m_SurfaceEmitterGeometryGpuData);
    const bool surfaceEmitterTrianglesRecreated = EnsureStructuredBufferCapacity(commandList, m_SurfaceEmitterTriangleBuffer, m_SurfaceEmitterTriangleBufferCapacity, m_SurfaceEmitterTriangleGpuData);
    const bool surfaceEmitterTriangleCdfRecreated = EnsureStructuredBufferCapacity(commandList, m_SurfaceEmitterTriangleCdfBuffer, m_SurfaceEmitterTriangleCdfBufferCapacity, m_SurfaceEmitterTriangleCdfGpuData);
    const bool surfaceEmitterInstancesRecreated = EnsureStructuredBufferCapacity(commandList, m_SurfaceEmitterInstanceBuffer, m_SurfaceEmitterInstanceBufferCapacity, m_SurfaceEmitterInstanceGpuData);
//Modify Begin:2026-08-06 by BestHui
    const bool directLightCdfRecreated = EnsureStructuredBufferCapacity(commandList, m_DirectLightCdfBuffer, m_DirectLightCdfBufferCapacity, m_DirectLightCdfGpuData);
//Modify End

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
//Modify Begin:2026-08-06 by BestHui
    if (directLightCdfRecreated)
    {
        m_DirectLightCdfDirtyBegin = 0;
        m_DirectLightCdfDirtyEnd = 0;
    }
//Modify End

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
//Modify Begin:2026-08-06 by BestHui
    if (m_DirectLightCdfDirtyBegin < m_DirectLightCdfDirtyEnd)
    {
        UploadGpuLightRange(commandList, *m_UploadBuffer, m_DirectLightCdfBuffer, m_DirectLightCdfGpuData, m_DirectLightCdfDirtyBegin, m_DirectLightCdfDirtyEnd);
        m_DirectLightCdfDirtyBegin = 0;
        m_DirectLightCdfDirtyEnd = 0;
    }
//Modify End
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-08-06 by BestHui
    return directionalLightsRecreated || pointLightsRecreated ||
        surfaceEmitterGeometriesRecreated || surfaceEmitterTrianglesRecreated ||
        surfaceEmitterTriangleCdfRecreated || surfaceEmitterInstancesRecreated || directLightCdfRecreated;
//Modify End
//Modify End
}

void SceneLightManager::UpdateDynamicLights(const float timeSeconds)
{
    const size_t pointLightCount = std::min(m_PointLights.size(), m_PointLightBaseY.size());
    for (size_t i = 0; i < pointLightCount; ++i)
    {
        if (i < m_PointLightAnimated.size() && m_PointLightAnimated[i] == 0)
        {
            continue;
        }

        const float radius = i < m_PointLightOrbitRadius.size() ? m_PointLightOrbitRadius[i] : 16.0f;
        const float speed = i < m_PointLightOrbitSpeed.size() ? m_PointLightOrbitSpeed[i] : 0.4f;
        const float phase = i < m_PointLightPhase.size() ? m_PointLightPhase[i] : 0.0f;
        const XMFLOAT3 orbitCenter = i < m_PointLightOrbitCenter.size() ? m_PointLightOrbitCenter[i] : XMFLOAT3{ -12.0f, 0.0f, 18.0f };
        const float angle = timeSeconds * speed + phase;
        m_PointLights[i].PositionWs.x = orbitCenter.x + std::cos(angle) * radius;
        m_PointLights[i].PositionWs.y = m_PointLightBaseY[i] + std::sin(angle * 1.7f) * 0.75f;
        m_PointLights[i].PositionWs.z = orbitCenter.z + std::sin(angle) * radius;
        UpdatePointLightGpuData(i);
    }

//Modify Begin:2026-08-06 by BestHui
    MarkDirtyRange(
        0,
        std::min(pointLightCount, m_PointLightGpuData.size()),
        m_PointLightDirtyBegin,
        m_PointLightDirtyEnd);
//Modify End
}

bool SceneLightManager::DrawImGui()
{
    bool changed = false;

    if (ImGui::CollapsingHeader("Light Counts"))
    {
        ImGui::Text("Directional: %zu", m_DirectionalLights.size());
        ImGui::Text("Point: %zu", m_PointLights.size());
        ImGui::Text("Area: %zu", m_AreaLights.size());
//Modify Begin:2026-08-06 by BestHui
        ImGui::Text("Mesh Surface Emitters: %zu", m_MeshSurfaceEmitterData.Instances.size());
//Modify End
    }

    if (ImGui::CollapsingHeader("Sky Light"))
    {
        bool skyChanged = false;
//Modify Begin:2026-07-30 by BestHui
        skyChanged |= ImGui::DragFloat3("Sky Color", &m_SkyLightColor.x, 0.01f, 0.0f, 0.0f, "%.3f");
        skyChanged |= ImGui::DragFloat("Sky Intensity", &m_SkyLightIntensity, 0.1f, 0.0f, 0.0f, "%.3f");
//Modify End
        if (skyChanged)
        {
            m_SkyLight.ColorAndIntensity = {
                std::max(0.0f, m_SkyLightColor.x),
                std::max(0.0f, m_SkyLightColor.y),
                std::max(0.0f, m_SkyLightColor.z),
                std::max(0.0f, m_SkyLightIntensity)
            };
            changed = true;
        }
    }

//Modify Begin:2026-08-03 by BestHui
    if (ImGui::CollapsingHeader("Directional Lights"))
//Modify End
    {
        if (ImGui::Checkbox("Enable Directional Lights", &m_DirectionalLightsEnabled))
        {
            MarkDirectLightSamplingDirty();
            changed = true;
        }
//Modify Begin:2026-08-03 by BestHui
        if (ImGui::CollapsingHeader("Directional Light List"))
//Modify End
        {
            for (size_t i = 0; i < m_DirectionalLights.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                DirectionalLight& light = m_DirectionalLights[i];
                const bool open = ImGui::TreeNodeEx(
                    "DirectionalLight",
                    ImGuiTreeNodeFlags_None,
                    "#%zu Dir(%.2f, %.2f, %.2f) Intensity %.2f",
                    i,
                    light.m_DirectionWs.x,
                    light.m_DirectionWs.y,
                    light.m_DirectionWs.z,
                    light.m_Color.w);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    RemoveDirectionalLight(i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                if (open)
                {
                    bool lightChanged = false;
                    XMFLOAT3 direction = { light.m_DirectionWs.x, light.m_DirectionWs.y, light.m_DirectionWs.z };
//Modify Begin:2026-07-30 by BestHui
                    lightChanged |= ImGui::DragFloat3("Direction", &direction.x, 0.01f, 0.0f, 0.0f, "%.4f");
                    lightChanged |= ImGui::DragFloat3("Color", &light.m_Color.x, 0.01f, 0.0f, 0.0f, "%.3f");
                    lightChanged |= ImGui::DragFloat("Intensity", &light.m_Color.w, 0.1f, 0.0f, 0.0f, "%.3f");
                    lightChanged |= ImGui::DragFloat("Angular Radius", &light.m_DirectionWs.w, 0.001f, 0.0f, 0.0f, "%.5f");
//Modify End
                    if (lightChanged)
                    {
                        direction = NormalizeVector(direction);
                        light.m_DirectionWs = { direction.x, direction.y, direction.z, std::max(0.0f, light.m_DirectionWs.w) };
                        light.m_Color.x = std::max(0.0f, light.m_Color.x);
                        light.m_Color.y = std::max(0.0f, light.m_Color.y);
                        light.m_Color.z = std::max(0.0f, light.m_Color.z);
                        light.m_Color.w = std::max(0.0f, light.m_Color.w);
                        m_DirectionalLightGpuData[i].DirectionAndAngularRadius = light.m_DirectionWs;
                        m_DirectionalLightGpuData[i].ColorAndIntensity = light.m_Color;
                        MarkDirectionalLightsDirty(i, i + 1);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

//Modify Begin:2026-07-30 by BestHui
        ImGui::PushID("NewDirectionalLight");
        if (ImGui::CollapsingHeader("New Directional Light"))
        {
//Modify Begin:2026-07-30 by BestHui
            ImGui::DragFloat3("Direction", &m_NewDirectionalLightDirection.x, 0.01f, 0.0f, 0.0f, "%.4f");
            ImGui::DragFloat3("Directional Color", &m_NewDirectionalLightColor.x, 0.01f, 0.0f, 0.0f, "%.3f");
            ImGui::DragFloat("Directional Intensity", &m_NewDirectionalLightIntensity, 0.1f, 0.0f, 0.0f, "%.3f");
            ImGui::DragFloat("Angular Radius", &m_NewDirectionalLightAngularRadius, 0.001f, 0.0f, 0.0f, "%.5f");
            if (ImGui::Button("Add Directional Light"))
            {
                AddDirectionalLight();
                changed = true;
            }
//Modify End
        }
        ImGui::PopID();
//Modify End
    }

//Modify Begin:2026-08-03 by BestHui
    if (ImGui::CollapsingHeader("Point Lights"))
//Modify End
    {
        if (ImGui::Checkbox("Enable Point Lights", &m_PointLightsEnabled))
        {
            MarkDirectLightSamplingDirty();
            changed = true;
        }
        if (ImGui::Checkbox("Animate Point Lights", &m_AnimatePointLights))
        {
            changed = true;
        }
//Modify Begin:2026-08-03 by BestHui
        if (ImGui::CollapsingHeader("Point Light List"))
//Modify End
        {
            for (size_t i = 0; i < m_PointLights.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
//Modify Begin:2026-07-30 by BestHui
                PointLight& light = m_PointLights[i];
                const bool isAnimated = i < m_PointLightAnimated.size() && m_PointLightAnimated[i] != 0;
//Modify Begin:2026-08-06 by BestHui
                const bool open = ImGui::TreeNodeEx(
                    "PointLight",
                    ImGuiTreeNodeFlags_None,
                    "#%zu %s I %.1f",
                    i,
                    isAnimated ? "Animated" : "Static",
                    light.Color.w);
                ImGui::SameLine();
                if (ImGui::SmallButton("X"))
                {
                    RemovePointLight(i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
//Modify End
                if (open)
                {
                    bool pointChanged = false;
                    bool positionChanged = false;
                    XMFLOAT3 position = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z };
//Modify Begin:2026-07-30 by BestHui
                    positionChanged |= ImGui::DragFloat3("Position", &position.x, 0.1f, 0.0f, 0.0f, "%.3f");
                    pointChanged |= ImGui::DragFloat3("Color", &light.Color.x, 0.01f, 0.0f, 0.0f, "%.3f");
                    pointChanged |= ImGui::DragFloat("Intensity", &light.Color.w, 0.1f, 0.0f, 0.0f, "%.3f");
                    pointChanged |= ImGui::DragFloat("Range", &light.Range, 0.1f, 0.0f, 0.0f, "%.3f");
//Modify Begin:2026-07-30 by BestHui
                    pointChanged |= ImGui::DragFloat("Source Radius", &light.SourceRadius, 0.01f, 0.0f, 0.0f, "%.4f");
//Modify End
//Modify End
                    bool animated = isAnimated;
                    if (ImGui::Checkbox("Animated", &animated))
                    {
                        if (i < m_PointLightAnimated.size())
                        {
                            m_PointLightAnimated[i] = animated ? 1u : 0u;
                        }
                        pointChanged = true;
                    }
                    if (positionChanged)
                    {
                        light.PositionWs = { position.x, position.y, position.z, 1.0f };
                        if (i < m_PointLightBaseY.size())
                        {
                            m_PointLightBaseY[i] = position.y;
                        }
                        if (i < m_PointLightOrbitCenter.size())
                        {
                            m_PointLightOrbitCenter[i] = { position.x, position.y, position.z };
                        }
                    }
                    if (pointChanged || positionChanged)
                    {
                        light.Color.x = std::max(0.0f, light.Color.x);
                        light.Color.y = std::max(0.0f, light.Color.y);
                        light.Color.z = std::max(0.0f, light.Color.z);
                        light.Color.w = std::max(0.0f, light.Color.w);
                        light.Range = std::max(0.1f, light.Range);
//Modify Begin:2026-07-30 by BestHui
                        light.SourceRadius = std::max(0.0f, light.SourceRadius);
//Modify End
                        light.RecalculateAttenuationCoefficients();
                        UpdatePointLightGpuData(i);
                        MarkPointLightsDirty(i, i + 1);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
//Modify End
                ImGui::PopID();
            }
        }
//Modify Begin:2026-07-30 by BestHui
        ImGui::PushID("NewPointLight");
        if (ImGui::CollapsingHeader("New Point Light"))
        {
//Modify Begin:2026-07-30 by BestHui
            ImGui::DragFloat3("Point Color", &m_NewPointLightColor.x, 0.01f, 0.0f, 0.0f, "%.3f");
            ImGui::DragFloat("Point Intensity", &m_NewPointLightIntensity, 0.1f, 0.0f, 0.0f, "%.3f");
            ImGui::DragFloat("Point Range", &m_NewPointLightRange, 0.1f, 0.0f, 0.0f, "%.3f");
            ImGui::DragFloat("Point Source Radius", &m_NewPointLightSourceRadius, 0.01f, 0.0f, 0.0f, "%.4f");
            ImGui::DragFloat("Random Spawn Radius", &m_RandomPointLightSpawnRadius, 0.1f, 1.0f, 80.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("Add At Origin"))
            {
                AddPointLightAtOrigin();
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Random"))
            {
                AddRandomPointLightInUpperHemisphere();
                changed = true;
            }
//Modify End
        }
        ImGui::PopID();
//Modify End
    }

//Modify Begin:2026-08-03 by BestHui
    if (ImGui::CollapsingHeader("Area Lights"))
//Modify End
    {
        if (ImGui::Checkbox("Enable Area Lights", &m_AreaLightsEnabled))
        {
            MarkDirectLightSamplingDirty();
            changed = true;
        }
//Modify Begin:2026-08-03 by BestHui
        if (ImGui::CollapsingHeader("Area Light List"))
//Modify End
        {
            for (size_t i = 0; i < m_AreaLights.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                AreaLightData& light = m_AreaLights[i];
                const bool open = ImGui::TreeNodeEx(
                    "AreaLight",
                    ImGuiTreeNodeFlags_None,
                    "#%zu Pos(%.2f, %.2f, %.2f) Size(%.2f, %.2f) Intensity %.2f",
                    i,
                    light.PositionAndRange.x,
                    light.PositionAndRange.y,
                    light.PositionAndRange.z,
                    light.AxisUAndExtent.w * 2.0f,
                    light.AxisVAndExtent.w * 2.0f,
                    light.ColorAndIntensity.w);
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete"))
                {
                    RemoveAreaLight(i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                if (open)
                {
                    bool lightChanged = false;
                    bool normalChanged = false;
                    XMFLOAT3 position = { light.PositionAndRange.x, light.PositionAndRange.y, light.PositionAndRange.z };
                    XMFLOAT3 normal = { light.NormalAndType.x, light.NormalAndType.y, light.NormalAndType.z };
                    XMFLOAT2 size = { light.AxisUAndExtent.w * 2.0f, light.AxisVAndExtent.w * 2.0f };
//Modify Begin:2026-07-30 by BestHui
                    lightChanged |= ImGui::DragFloat3("Position", &position.x, 0.1f, 0.0f, 0.0f, "%.3f");
                    normalChanged |= ImGui::DragFloat3("Normal", &normal.x, 0.01f, 0.0f, 0.0f, "%.4f");
                    lightChanged |= ImGui::DragFloat2("Size", &size.x, 0.1f, 0.0f, 0.0f, "%.3f");
                    lightChanged |= ImGui::DragFloat("Range", &light.PositionAndRange.w, 0.1f, 0.0f, 0.0f, "%.3f");
                    lightChanged |= ImGui::DragFloat3("Color", &light.ColorAndIntensity.x, 0.01f, 0.0f, 0.0f, "%.3f");
                    lightChanged |= ImGui::DragFloat("Intensity", &light.ColorAndIntensity.w, 0.1f, 0.0f, 0.0f, "%.3f");
//Modify End
                    if (lightChanged || normalChanged)
                    {
                        normal = NormalizeVector(normal);
                        XMFLOAT3 axisU{};
                        XMFLOAT3 axisV{};
                        BuildAreaLightAxes(normal, axisU, axisV);
                        light.PositionAndRange = { position.x, position.y, position.z, std::max(0.1f, light.PositionAndRange.w) };
                        light.NormalAndType = { normal.x, normal.y, normal.z, light.NormalAndType.w };
                        light.AxisUAndExtent = { axisU.x, axisU.y, axisU.z, std::max(0.1f, size.x) * 0.5f };
                        light.AxisVAndExtent = { axisV.x, axisV.y, axisV.z, std::max(0.1f, size.y) * 0.5f };
                        light.ColorAndIntensity.x = std::max(0.0f, light.ColorAndIntensity.x);
                        light.ColorAndIntensity.y = std::max(0.0f, light.ColorAndIntensity.y);
                        light.ColorAndIntensity.z = std::max(0.0f, light.ColorAndIntensity.z);
                        light.ColorAndIntensity.w = std::max(0.0f, light.ColorAndIntensity.w);
                        UpdateAreaLightSurfaceEmitter(i);
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

//Modify Begin:2026-07-30 by BestHui
        ImGui::PushID("NewAreaLight");
        if (ImGui::CollapsingHeader("New Area Light"))
        {
//Modify Begin:2026-07-30 by BestHui
            ImGui::DragFloat3("Area Position", &m_NewAreaLightPosition.x, 0.1f, 0.0f, 0.0f, "%.3f");
            ImGui::DragFloat3("Area Normal", &m_NewAreaLightNormal.x, 0.01f, 0.0f, 0.0f, "%.4f");
            ImGui::DragFloat2("Area Size", &m_NewAreaLightSize.x, 0.1f, 0.0f, 0.0f, "%.3f");
            ImGui::DragFloat3("Area Color", &m_NewAreaLightColor.x, 0.01f, 0.0f, 0.0f, "%.3f");
            ImGui::DragFloat("Area Intensity", &m_NewAreaLightIntensity, 0.1f, 0.0f, 0.0f, "%.3f");
            ImGui::DragFloat("Area Range", &m_NewAreaLightRange, 0.1f, 0.0f, 0.0f, "%.3f");
            if (ImGui::Button("Add Area Light"))
            {
                AddAreaLight();
                changed = true;
            }
//Modify End
        }
        ImGui::PopID();
//Modify End
    }
    return changed;
}

void SceneLightManager::ApplyToScene(Scene& scene) const
{
    std::vector<AreaLight> areaLights;
    areaLights.reserve(m_AreaLights.size());
    for (const AreaLightData& sourceLight : m_AreaLights)
    {
        AreaLight light{};
        light.PositionWs = sourceLight.PositionAndRange;
        light.NormalWs = sourceLight.NormalAndType;
        light.AxisUWsAndExtent = sourceLight.AxisUAndExtent;
        light.AxisVWsAndExtent = sourceLight.AxisVAndExtent;
        light.Color = sourceLight.ColorAndIntensity;
        light.Range = sourceLight.PositionAndRange.w;
        areaLights.push_back(light);
    }

    SceneSkybox skybox = scene.GetSkybox();
    skybox.AmbientColorAndIntensity = m_SkyLight.ColorAndIntensity;
    scene.SetSkybox(skybox);
//Modify Begin:2026-08-06 by BestHui
    scene.SetLightGroupSettings({
        m_DirectionalLightsEnabled,
        m_PointLightsEnabled,
        m_AreaLightsEnabled
    });
//Modify End
    scene.SetDirectionalLights(m_DirectionalLights);
    scene.SetPointLights(m_PointLights);
    scene.SetAreaLights(std::move(areaLights));
}

void SceneLightManager::BindComputeResources(CommandContext& commandContext, ComputeShader& shader)
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
//Modify Begin:2026-08-06 by BestHui
    if (shader.HasShaderResourceView("DirectLightCdf"))
    {
        commandContext.SetShaderResource(shader, "DirectLightCdf", 0u, m_DirectLightCdfBuffer);
    }
//Modify End
}

//Modify Begin:2026-08-03 by BestHui
void SceneLightManager::PrepareAsyncComputeResources(CommandList& commandList) const
{
    commandList.TransitionBarrier(m_DirectionalLightBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList.TransitionBarrier(m_PointLightBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList.TransitionBarrier(m_SurfaceEmitterGeometryBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList.TransitionBarrier(m_SurfaceEmitterInstanceBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList.TransitionBarrier(m_SurfaceEmitterTriangleBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList.TransitionBarrier(m_SurfaceEmitterTriangleCdfBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
//Modify Begin:2026-08-06 by BestHui
    commandList.TransitionBarrier(m_DirectLightCdfBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
//Modify End
}
//Modify End

void SceneLightManager::BindRayTracingResources(RayTracingBindingSet& bindingSet)
{
//Modify Begin:2026-07-30 by BestHui
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
//Modify Begin:2026-08-06 by BestHui
    if (bindingSet.HasBinding("DirectLightCdf"))
    {
        bindingSet.SetBuffer("DirectLightCdf", m_DirectLightCdfBuffer);
    }
//Modify End
//Modify End
}

void SceneLightManager::FillCameraConstants(
    uint32_t& directionalLightCount,
    uint32_t& pointLightCount,
    uint32_t& surfaceEmitterCount,
    SkyLightData& skyLight) const
{
    directionalLightCount = static_cast<uint32_t>(m_DirectionalLightGpuData.size());
    pointLightCount = static_cast<uint32_t>(m_PointLightGpuData.size());
    surfaceEmitterCount = static_cast<uint32_t>(m_SurfaceEmitterInstanceGpuData.size());
    skyLight = m_SkyLight;
}

void SceneLightManager::AddDirectionalLight()
{
    const XMFLOAT3 direction = NormalizeVector(m_NewDirectionalLightDirection);
    DirectionalLight light{};
    light.m_DirectionWs = {
        direction.x,
        direction.y,
        direction.z,
        std::max(0.0f, m_NewDirectionalLightAngularRadius)
    };
    light.m_Color = {
        std::max(0.0f, m_NewDirectionalLightColor.x),
        std::max(0.0f, m_NewDirectionalLightColor.y),
        std::max(0.0f, m_NewDirectionalLightColor.z),
        std::max(0.0f, m_NewDirectionalLightIntensity)
    };

    const size_t lightIndex = m_DirectionalLights.size();
    m_DirectionalLights.push_back(light);

    DirectionalLightData gpuLight{};
    gpuLight.DirectionAndAngularRadius = light.m_DirectionWs;
    gpuLight.ColorAndIntensity = light.m_Color;
    m_DirectionalLightGpuData.push_back(gpuLight);
    MarkDirectionalLightsDirty(lightIndex, lightIndex + 1);
}

void SceneLightManager::AddPointLightAtOrigin()
{
    PointLight light({ 0.0f, 0.0f, 0.0f, 1.0f }, std::max(0.1f, m_NewPointLightRange));
    light.Color = {
        std::max(0.0f, m_NewPointLightColor.x),
        std::max(0.0f, m_NewPointLightColor.y),
        std::max(0.0f, m_NewPointLightColor.z),
        std::max(0.0f, m_NewPointLightIntensity)
    };
//Modify Begin:2026-07-30 by BestHui
    light.SourceRadius = std::max(0.0f, m_NewPointLightSourceRadius);
//Modify End
    light.RecalculateAttenuationCoefficients();

    const size_t lightIndex = m_PointLights.size();
    m_PointLights.push_back(light);
    m_PointLightBaseY.push_back(0.0f);
    m_PointLightPhase.push_back(0.0f);
    m_PointLightOrbitRadius.push_back(0.0f);
    m_PointLightOrbitSpeed.push_back(0.0f);
    m_PointLightOrbitCenter.push_back({ 0.0f, 0.0f, 0.0f });
    m_PointLightAnimated.push_back(0);

    UpdatePointLightGpuData(lightIndex);
    MarkPointLightsDirty(lightIndex, lightIndex + 1);
}

void SceneLightManager::AddRandomPointLightInUpperHemisphere()
{
    static std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> unitDistribution(0.0f, 1.0f);
    std::uniform_real_distribution<float> colorDistribution(0.25f, 1.0f);
    std::uniform_real_distribution<float> intensityDistribution(8.0f, 36.0f);
    std::uniform_real_distribution<float> rangeScaleDistribution(0.45f, 1.15f);

    const float spawnRadius = std::max(1.0f, m_RandomPointLightSpawnRadius);
    const float radius = spawnRadius * std::cbrt(unitDistribution(rng));
    const float cosTheta = unitDistribution(rng);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = XM_2PI * unitDistribution(rng);
    const XMFLOAT3 position = {
        radius * sinTheta * std::cos(phi),
        radius * cosTheta,
        radius * sinTheta * std::sin(phi)
    };

    PointLight light({ position.x, position.y, position.z, 1.0f }, std::max(3.0f, m_NewPointLightRange * rangeScaleDistribution(rng)));
    light.Color = {
        colorDistribution(rng),
        colorDistribution(rng),
        colorDistribution(rng),
        intensityDistribution(rng)
    };
//Modify Begin:2026-07-30 by BestHui
    light.SourceRadius = std::max(0.0f, m_NewPointLightSourceRadius);
//Modify End
    light.RecalculateAttenuationCoefficients();

    const float orbitRadius = std::max(1.0f, std::sqrt(position.x * position.x + position.z * position.z));
    const size_t lightIndex = m_PointLights.size();
    m_PointLights.push_back(light);
    m_PointLightBaseY.push_back(position.y);
    m_PointLightPhase.push_back(phi);
    m_PointLightOrbitRadius.push_back(orbitRadius);
    m_PointLightOrbitSpeed.push_back(0.18f + unitDistribution(rng) * 0.55f);
    m_PointLightOrbitCenter.push_back({ 0.0f, 0.0f, 0.0f });
    m_PointLightAnimated.push_back(1);

    UpdatePointLightGpuData(lightIndex);
    MarkPointLightsDirty(lightIndex, lightIndex + 1);
}

void SceneLightManager::AddAreaLight()
{
    const XMFLOAT3 normal = NormalizeVector(m_NewAreaLightNormal);
    XMFLOAT3 axisU{};
    XMFLOAT3 axisV{};
    BuildAreaLightAxes(normal, axisU, axisV);
    AreaLightData areaLight{};
    areaLight.PositionAndRange = {
        m_NewAreaLightPosition.x,
        m_NewAreaLightPosition.y,
        m_NewAreaLightPosition.z,
        std::max(0.1f, m_NewAreaLightRange)
    };
    areaLight.NormalAndType = { normal.x, normal.y, normal.z, 0.0f };
    areaLight.AxisUAndExtent = { axisU.x, axisU.y, axisU.z, std::max(0.1f, m_NewAreaLightSize.x) * 0.5f };
    areaLight.AxisVAndExtent = { axisV.x, axisV.y, axisV.z, std::max(0.1f, m_NewAreaLightSize.y) * 0.5f };
    areaLight.ColorAndIntensity = {
        std::max(0.0f, m_NewAreaLightColor.x),
        std::max(0.0f, m_NewAreaLightColor.y),
        std::max(0.0f, m_NewAreaLightColor.z),
        std::max(0.0f, m_NewAreaLightIntensity)
    };

    const size_t lightIndex = m_AreaLights.size();
    m_AreaLights.push_back(areaLight);
//Modify Begin:2026-08-06 by BestHui
    RebuildSurfaceEmitterGpuData();
    MarkDirtyRange(0, m_SurfaceEmitterGeometryGpuData.size(), m_SurfaceEmitterGeometryDirtyBegin, m_SurfaceEmitterGeometryDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleGpuData.size(), m_SurfaceEmitterTriangleDirtyBegin, m_SurfaceEmitterTriangleDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleCdfGpuData.size(), m_SurfaceEmitterTriangleCdfDirtyBegin, m_SurfaceEmitterTriangleCdfDirtyEnd);
    MarkDirtyRange(lightIndex + m_RectangleSurfaceEmitterOffset, m_SurfaceEmitterInstanceGpuData.size(), m_SurfaceEmitterInstanceDirtyBegin, m_SurfaceEmitterInstanceDirtyEnd);
    MarkDirectLightSamplingDirty();
//Modify End
}

void SceneLightManager::RemoveDirectionalLight(const size_t lightIndex)
{
    if (lightIndex >= m_DirectionalLights.size())
    {
        return;
    }

    EraseAt(m_DirectionalLights, lightIndex);
    EraseAt(m_DirectionalLightGpuData, lightIndex);
    MarkDirectionalLightsDirty(lightIndex, m_DirectionalLightGpuData.size());
}

void SceneLightManager::RemovePointLight(const size_t lightIndex)
{
    if (lightIndex >= m_PointLights.size())
    {
        return;
    }

    EraseAt(m_PointLights, lightIndex);
    EraseAt(m_PointLightGpuData, lightIndex);
    EraseAt(m_PointLightBaseY, lightIndex);
    EraseAt(m_PointLightPhase, lightIndex);
    EraseAt(m_PointLightOrbitRadius, lightIndex);
    EraseAt(m_PointLightOrbitSpeed, lightIndex);
    EraseAt(m_PointLightOrbitCenter, lightIndex);
    EraseAt(m_PointLightAnimated, lightIndex);
    MarkPointLightsDirty(lightIndex, m_PointLightGpuData.size());
}

void SceneLightManager::RemoveAreaLight(const size_t lightIndex)
{
    if (lightIndex >= m_AreaLights.size())
    {
        return;
    }

    EraseAt(m_AreaLights, lightIndex);
//Modify Begin:2026-08-06 by BestHui
    RebuildSurfaceEmitterGpuData();
    MarkDirtyRange(0, m_SurfaceEmitterGeometryGpuData.size(), m_SurfaceEmitterGeometryDirtyBegin, m_SurfaceEmitterGeometryDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleGpuData.size(), m_SurfaceEmitterTriangleDirtyBegin, m_SurfaceEmitterTriangleDirtyEnd);
    MarkDirtyRange(0, m_SurfaceEmitterTriangleCdfGpuData.size(), m_SurfaceEmitterTriangleCdfDirtyBegin, m_SurfaceEmitterTriangleCdfDirtyEnd);
    MarkDirtyRange(lightIndex + m_RectangleSurfaceEmitterOffset, m_SurfaceEmitterInstanceGpuData.size(), m_SurfaceEmitterInstanceDirtyBegin, m_SurfaceEmitterInstanceDirtyEnd);
    MarkDirectLightSamplingDirty();
//Modify End
}

void SceneLightManager::BuildGpuData()
{
    m_DirectionalLightGpuData.clear();
    m_PointLightGpuData.clear();

    m_DirectionalLightGpuData.reserve(m_DirectionalLights.size());
    for (const DirectionalLight& light : m_DirectionalLights)
    {
        DirectionalLightData gpuLight{};
        gpuLight.DirectionAndAngularRadius = light.m_DirectionWs;
        gpuLight.ColorAndIntensity = light.m_Color;
        m_DirectionalLightGpuData.push_back(gpuLight);
    }

    m_PointLightGpuData.reserve(m_PointLights.size());
    for (const PointLight& light : m_PointLights)
    {
        PointLightData gpuLight{};
        gpuLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
        gpuLight.ColorAndIntensity = light.Color;
//Modify Begin:2026-07-30 by BestHui
        gpuLight.Attenuation = { light.ConstantAttenuation, light.LinearAttenuation, light.QuadraticAttenuation, light.SourceRadius };
//Modify End
        m_PointLightGpuData.push_back(gpuLight);
    }

//Modify Begin:2026-08-06 by BestHui
    RebuildSurfaceEmitterGpuData();
//Modify End
}

//Modify Begin:2026-08-06 by BestHui
void SceneLightManager::RebuildSurfaceEmitterGpuData()
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

void SceneLightManager::UpdateAreaLightSurfaceEmitter(const size_t lightIndex)
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
//Modify End

//Modify Begin:2026-08-06 by BestHui
void SceneLightManager::RebuildDirectLightSamplingCdf()
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

void SceneLightManager::MarkDirectLightSamplingDirty()
{
    RebuildDirectLightSamplingCdf();
    MarkDirtyRange(
        0,
        m_DirectLightCdfGpuData.size(),
        m_DirectLightCdfDirtyBegin,
        m_DirectLightCdfDirtyEnd);
}
//Modify End

void SceneLightManager::UpdatePointLightGpuData(const size_t lightIndex)
{
    if (lightIndex >= m_PointLights.size())
    {
        return;
    }

    if (m_PointLightGpuData.size() <= lightIndex)
    {
        m_PointLightGpuData.resize(lightIndex + 1);
    }

    const PointLight& light = m_PointLights[lightIndex];
    PointLightData& gpuLight = m_PointLightGpuData[lightIndex];
    gpuLight.PositionAndRange = { light.PositionWs.x, light.PositionWs.y, light.PositionWs.z, light.Range };
    gpuLight.ColorAndIntensity = light.Color;
//Modify Begin:2026-07-30 by BestHui
    gpuLight.Attenuation = { light.ConstantAttenuation, light.LinearAttenuation, light.QuadraticAttenuation, light.SourceRadius };
//Modify End
}

void SceneLightManager::MarkDirectionalLightsDirty()
{
    MarkDirectionalLightsDirty(0, m_DirectionalLightGpuData.size());
}

void SceneLightManager::MarkDirectionalLightsDirty(const size_t beginIndex, const size_t endIndex)
{
    MarkDirtyRange(beginIndex, endIndex, m_DirectionalLightDirtyBegin, m_DirectionalLightDirtyEnd);
//Modify Begin:2026-08-06 by BestHui
    MarkDirectLightSamplingDirty();
//Modify End
}

void SceneLightManager::MarkPointLightsDirty(const size_t beginIndex, const size_t endIndex)
{
    MarkDirtyRange(beginIndex, endIndex, m_PointLightDirtyBegin, m_PointLightDirtyEnd);
//Modify Begin:2026-08-06 by BestHui
    MarkDirectLightSamplingDirty();
//Modify End
}

//Modify End
