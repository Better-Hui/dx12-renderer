#pragma once
//Modify Begin:2026-07-21 by Hui

#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>
//Modify Begin:2026-07-27 by Hui
#include <Framework/Core/FrameworkDeprecated.h>
//Modify End
//Modify Begin:2026-07-30 by Hui
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
//Modify End
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class CommandList;
class PipelineDescriptorSet;
//Modify Begin:2026-07-29 by Hui
class PipelineDescriptorPool;
//Modify End
class CommandContext;
class RayTracingPipelineState;
class ShaderBlob;
class StructuredBuffer;
class Texture;
class Resource;
class RayTracingShader;
//Modify Begin:2026-07-30 by Hui
class FrameworkDeviceContext;
//Modify End

enum class RayTracingShaderBindingType
{
    OutputTexture,
    AccelerationStructure,
    ConstantBuffer,
    StructuredBuffer,
    TextureArray
};

struct RayTracingShaderBindingDesc
{
    std::string Name;
    RayTracingShaderBindingType Type = RayTracingShaderBindingType::StructuredBuffer;
    uint32_t ShaderRegister = 0;
    uint32_t RegisterSpace = 0;
    uint32_t DescriptorCount = 1;
    //Modify Begin:2026-07-24 by Hui
    D3D12_UNORDERED_ACCESS_VIEW_DESC NullUnorderedAccessViewDesc = {};
    bool HasNullUnorderedAccessViewDesc = false;
    //Modify End
};

struct RayTracingHitGroupDesc
{
    std::wstring Name;
    std::wstring ClosestHitShader;
    std::wstring AnyHitShader;
    std::wstring IntersectionShader;
    D3D12_HIT_GROUP_TYPE Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
};

struct RayTracingShaderRecordDesc
{
    std::wstring ExportName;
    std::vector<uint8_t> LocalRootArguments;
};

struct RayTracingShaderPassDesc
{
    std::string Name;
    std::wstring RayGenerationShader;
    std::vector<RayTracingShaderRecordDesc> MissShaderRecords;
    std::vector<RayTracingShaderRecordDesc> HitGroupRecords;
};

struct RayTracingPipelineDesc
{
    std::vector<std::wstring> Exports;
    std::vector<RayTracingHitGroupDesc> HitGroups;
    std::vector<RayTracingShaderBindingDesc> Bindings;
//Modify Begin:2026-07-30 by Hui
    std::vector<PipelineRootSamplerDesc> RootSamplers;
//Modify End
    std::vector<RayTracingShaderPassDesc> Passes;
    uint32_t PayloadSizeInBytes = sizeof(float) * 8;
    uint32_t AttributeSizeInBytes = sizeof(float) * 2;
    uint32_t MaxTraceRecursionDepth = 1;
    uint32_t MaxDescriptorCount = 2048;
};

//Modify Begin:2026-07-23 by Hui
class RayTracingPipelineDescBuilder
{
public:
    RayTracingPipelineDescBuilder();
    explicit RayTracingPipelineDescBuilder(RayTracingPipelineDesc desc);

    static RayTracingPipelineDescBuilder ReflectedDefault(const ShaderBlob& shaderLibrary);

    RayTracingPipelineDescBuilder& WithExport(std::wstring exportName);
    RayTracingPipelineDescBuilder& WithTriangleHitGroup(
        std::wstring hitGroupName,
        std::wstring closestHitShader,
        std::wstring anyHitShader = L"",
        std::wstring intersectionShader = L"");
    RayTracingPipelineDescBuilder& WithRayGenerationPass(
        std::string passName,
        std::wstring rayGenerationShader,
        std::vector<std::wstring> missShaders,
        std::vector<std::wstring> hitGroups);

    RayTracingPipelineDescBuilder& WithOutputTexture(std::string name, uint32_t shaderRegister, uint32_t registerSpace = 0, uint32_t descriptorCount = 1);
    RayTracingPipelineDescBuilder& WithAccelerationStructure(std::string name, uint32_t shaderRegister, uint32_t registerSpace = 0);
    RayTracingPipelineDescBuilder& WithConstantBuffer(std::string name, uint32_t shaderRegister, uint32_t registerSpace = 0);
    RayTracingPipelineDescBuilder& WithStructuredBuffer(std::string name, uint32_t shaderRegister, uint32_t registerSpace = 0);
    RayTracingPipelineDescBuilder& WithTextureArray(std::string name, uint32_t shaderRegister, uint32_t registerSpace, uint32_t descriptorCount);
//Modify Begin:2026-07-30 by Hui
    RayTracingPipelineDescBuilder& WithStaticSamplerContract(PipelineStaticSamplerContract contract);
//Modify End

    RayTracingPipelineDescBuilder& WithPayloadSize(uint32_t payloadSizeInBytes);
    RayTracingPipelineDescBuilder& WithAttributeSize(uint32_t attributeSizeInBytes);
    RayTracingPipelineDescBuilder& WithMaxRecursionDepth(uint32_t maxTraceRecursionDepth);
    RayTracingPipelineDescBuilder& WithMaxDescriptorCount(uint32_t maxDescriptorCount);

    RayTracingPipelineDesc Build() const;

private:
    RayTracingPipelineDescBuilder& WithBinding(
        std::string name,
        RayTracingShaderBindingType type,
        uint32_t shaderRegister,
        uint32_t registerSpace,
        uint32_t descriptorCount);

    RayTracingPipelineDesc m_Desc;
//Modify Begin:2026-07-30 by Hui
    std::vector<std::pair<uint32_t, uint32_t>> m_ReflectedStaticSamplerCoordinates;
//Modify End
};
//Modify End

//Modify Begin:2026-07-24 by Hui
class RayTracingBindingSet
{
public:
    explicit RayTracingBindingSet(const RayTracingShader& shader);
    ~RayTracingBindingSet();

    RayTracingBindingSet(const RayTracingBindingSet&) = delete;
    RayTracingBindingSet& operator=(const RayTracingBindingSet&) = delete;
    RayTracingBindingSet(RayTracingBindingSet&&) noexcept;
    RayTracingBindingSet& operator=(RayTracingBindingSet&&) noexcept;

    bool HasBinding(std::string_view name) const;
    void SetTexture(std::string_view name, const ShaderResourceView& shaderResourceView);
    void SetTexture(std::string_view name, uint32_t arrayIndex, const ShaderResourceView& shaderResourceView);
    void SetTexture(std::string_view name, const std::shared_ptr<Resource>& texture);
    void SetShaderResourceView(std::string_view name, const ShaderResourceView& shaderResourceView);
    void SetShaderResourceView(std::string_view name, uint32_t arrayIndex, const ShaderResourceView& shaderResourceView);
    void SetUnorderedAccessView(std::string_view name, const UnorderedAccessView& unorderedAccessView);
    void SetBuffer(std::string_view name, const StructuredBuffer& buffer);
    void SetOutputTexture(std::string_view name, const std::shared_ptr<Texture>& texture);
    void SetAccelerationStructure(std::string_view name, const RayTracingAccelerationStructure& accelerationStructure);
    void SetConstantBufferData(std::string_view name, const void* data, size_t size);
    void SetStructuredBuffer(std::string_view name, const StructuredBuffer& buffer);
    void SetTextureArray(std::string_view name, const std::vector<std::shared_ptr<Texture>>& textures);
    void SetTextureArray(
        std::string_view name,
        const std::vector<std::shared_ptr<Texture>>& textures,
        const std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC>& srvDescs);
    void SetTextureArray(std::string_view name, const std::vector<ShaderResourceView>& shaderResourceViews);

private:
//Modify Begin:2026-07-29 by Hui
    friend class CommandContext;
    const RayTracingShader& GetShader() const;
    const PipelineDescriptorSet& GetDescriptorSet() const;
    const PipelineDescriptorPool& GetDescriptorPool() const;
//Modify End
    struct Impl;
    Impl& GetImpl();
    const Impl& GetImpl() const;
    std::unique_ptr<Impl> m_Impl;
};
//Modify End

class RayTracingShader
{
public:
    RayTracingShader(
        FrameworkDeviceContext& deviceContext,
        const ShaderBlob& shaderLibrary,
        RayTracingPipelineDesc desc);
    ~RayTracingShader();

    RayTracingShader(const RayTracingShader&) = delete;
    RayTracingShader& operator=(const RayTracingShader&) = delete;
    RayTracingShader(RayTracingShader&&) noexcept;
    RayTracingShader& operator=(RayTracingShader&&) noexcept;

    static bool IsSupported(const FrameworkDeviceContext& deviceContext);

    const RayTracingPipelineDesc& GetDesc() const;
    const PipelineLayout& GetPipelineLayout() const;
    //Modify Begin:2026-07-24 by Hui
    RayTracingBindingSet CreateBindingSet() const;
    //Modify End

    bool HasBinding(std::string_view name) const;

private:
//Modify Begin:2026-07-29 by Hui
    friend class CommandContext;
    FrameworkDeviceContext& GetDeviceContext() const;
    const RayTracingPipelineState& GetPipelineState() const;
    void PrepareDispatch(std::string_view passName) const;
    D3D12_DISPATCH_RAYS_DESC BuildDispatchDesc(std::string_view passName, uint32_t width, uint32_t height, uint32_t depth) const;
    D3D12_DISPATCH_RAYS_DESC BuildDispatchDesc(uint32_t width, uint32_t height, uint32_t depth) const;
//Modify End
    //Modify Begin:2026-07-24 by Hui
    friend class RayTracingBindingSet;
    //Modify End
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
//Modify End
