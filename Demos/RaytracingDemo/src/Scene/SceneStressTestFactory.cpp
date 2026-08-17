//Modify Begin:2026-08-06 by Hui
#include <Scene/SceneStressTestFactory.h>

#include <DX12Library/CommandList.h>

#include <Framework/Geometry/ModelLoader.h>

#include <DirectXMath.h>

#include <cmath>
#include <stdexcept>

using namespace DirectX;

MeshPrototype SceneStressTestFactory::CreateSpherePrototype(const float diameter, const size_t tessellation)
{
    VertexCollectionType vertices;
    IndexCollectionType indices;

    if (tessellation < 3)
    {
        throw std::out_of_range("tessellation parameter out of range");
    }

    const float radius = diameter * 0.5f;
    const size_t verticalSegments = tessellation;
    const size_t horizontalSegments = tessellation * 2;

    vertices.emplace_back(
        XMFLOAT3(0.0f, radius, 0.0f),
        XMFLOAT3(0.0f, 1.0f, 0.0f),
        XMFLOAT2(0.5f, 0.0f));

    for (size_t verticalIndex = 1; verticalIndex < verticalSegments; ++verticalIndex)
    {
        const float v = static_cast<float>(verticalIndex) / static_cast<float>(verticalSegments);
        const float polar = static_cast<float>(verticalIndex) * XM_PI / static_cast<float>(verticalSegments);
        float sinPolar = 0.0f;
        float cosPolar = 0.0f;
        XMScalarSinCos(&sinPolar, &cosPolar, polar);

        for (size_t horizontalIndex = 0; horizontalIndex < horizontalSegments; ++horizontalIndex)
        {
            const float u = static_cast<float>(horizontalIndex) / static_cast<float>(horizontalSegments);
            const float longitude = static_cast<float>(horizontalIndex) * XM_2PI / static_cast<float>(horizontalSegments);
            float dx = 0.0f;
            float dz = 0.0f;
            XMScalarSinCos(&dx, &dz, longitude);
            const XMVECTOR normal = XMVectorSet(dx * sinPolar, cosPolar, dz * sinPolar, 0.0f);
            const XMVECTOR position = normal * radius;
            XMFLOAT3 positionFloat{};
            XMFLOAT3 normalFloat{};
            XMStoreFloat3(&positionFloat, position);
            XMStoreFloat3(&normalFloat, normal);
            vertices.emplace_back(positionFloat, normalFloat, XMFLOAT2(u, v));
        }
    }

    const uint16_t bottomIndex = static_cast<uint16_t>(vertices.size());
    vertices.emplace_back(
        XMFLOAT3(0.0f, -radius, 0.0f),
        XMFLOAT3(0.0f, -1.0f, 0.0f),
        XMFLOAT2(0.5f, 1.0f));

    const auto ringIndex = [horizontalSegments](const size_t ring, const size_t segment)
    {
        return static_cast<uint16_t>(1 + ring * horizontalSegments + (segment % horizontalSegments));
    };

    for (size_t horizontalIndex = 0; horizontalIndex < horizontalSegments; ++horizontalIndex)
    {
        indices.push_back(0);
        indices.push_back(ringIndex(0, horizontalIndex));
        indices.push_back(ringIndex(0, horizontalIndex + 1));
    }

    for (size_t verticalIndex = 0; verticalIndex + 1 < verticalSegments - 1; ++verticalIndex)
    {
        for (size_t horizontalIndex = 0; horizontalIndex < horizontalSegments; ++horizontalIndex)
        {
            indices.push_back(ringIndex(verticalIndex, horizontalIndex));
            indices.push_back(ringIndex(verticalIndex + 1, horizontalIndex));
            indices.push_back(ringIndex(verticalIndex, horizontalIndex + 1));

            indices.push_back(ringIndex(verticalIndex, horizontalIndex + 1));
            indices.push_back(ringIndex(verticalIndex + 1, horizontalIndex));
            indices.push_back(ringIndex(verticalIndex + 1, horizontalIndex + 1));
        }
    }

    const size_t lastRing = verticalSegments - 2;
    for (size_t horizontalIndex = 0; horizontalIndex < horizontalSegments; ++horizontalIndex)
    {
        indices.push_back(ringIndex(lastRing, horizontalIndex + 1));
        indices.push_back(ringIndex(lastRing, horizontalIndex));
        indices.push_back(bottomIndex);
    }

    MeshPrototype prototype(std::move(vertices), std::move(indices), true, true);
    prototype.m_Name = "StressSphere";
    return prototype;
}

StressTestSceneData SceneStressTestFactory::Create(
    CommandList& commandList,
    SceneTextureMaterialResources& textureMaterialResources,
    SceneGeometryResources& geometryResources,
    const uint32_t whiteTextureIndex)
{
    ModelLoader modelLoader;
    MeshPrototype spherePrototype = CreateSpherePrototype(1.0f, 12);
    const auto sphereModel = modelLoader.Load(commandList, std::vector<MeshPrototype>{ spherePrototype });
    const uint32_t sphereGeometryIndex = geometryResources.AddGeometry(
        sphereModel,
        std::vector<MeshPrototype>{ std::move(spherePrototype) });

    constexpr XMFLOAT4 StressSphereBaseColor = { 1.0f, 0.48f, 0.18f, 1.0f };
    constexpr float StressSphereEmissionIntensity = 6.0f;
    StressTestSceneData result{};
    result.MaterialIndex = textureMaterialResources.AddPbrMaterial(
        StressSphereBaseColor,
        { 1.0f, 1.0f, 0.0f, 0.0f },
        whiteTextureIndex,
        whiteTextureIndex,
        whiteTextureIndex,
        whiteTextureIndex,
        whiteTextureIndex,
        0.0f,
        0.38f,
        true,
        false,
        false,
        false,
        false,
        {
            StressSphereBaseColor.x * StressSphereEmissionIntensity,
            StressSphereBaseColor.y * StressSphereEmissionIntensity,
            StressSphereBaseColor.z * StressSphereEmissionIntensity,
            1.0f
        },
        whiteTextureIndex,
        false);

    constexpr uint32_t Columns = 64;
    constexpr uint32_t Rows = 24;
    constexpr uint32_t DepthLayers = 8;
    constexpr float XSpacing = 0.62f;
    constexpr float YSpacing = 0.46f;
    constexpr float ZSpacing = 0.78f;
    constexpr float Radius = 0.17f;
    constexpr float CenterX = -4.50f;
    constexpr float CenterY = 2.85f;
    constexpr float CenterZ = -2.80f;
    const float startX = -static_cast<float>(Columns - 1) * XSpacing * 0.5f;
    const float startY = -static_cast<float>(Rows - 1) * YSpacing * 0.5f;
    const float startZ = -static_cast<float>(DepthLayers - 1) * ZSpacing * 0.5f;
    result.Objects.reserve(static_cast<size_t>(Columns) * Rows * DepthLayers);

    for (uint32_t layer = 0; layer < DepthLayers; ++layer)
    {
        for (uint32_t row = 0; row < Rows; ++row)
        {
            for (uint32_t column = 0; column < Columns; ++column)
            {
                const float wave = std::sin(
                    static_cast<float>(column) * 0.37f +
                    static_cast<float>(row) * 0.61f +
                    static_cast<float>(layer) * 0.83f);
                const float stagger = (static_cast<float>((row + layer) & 1u) - 0.5f) * XSpacing * 0.35f;
                result.Objects.push_back({
                    XMMatrixScaling(Radius, Radius, Radius) *
                        XMMatrixTranslation(
                            CenterX + startX + static_cast<float>(column) * XSpacing + stagger,
                            CenterY + startY + static_cast<float>(row) * YSpacing + wave * 0.045f,
                            CenterZ + startZ + static_cast<float>(layer) * ZSpacing),
                    sphereGeometryIndex,
                    result.MaterialIndex
                });
            }
        }
    }

    return result;
}
//Modify End
