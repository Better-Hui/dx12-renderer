#pragma once
//Modify Begin:2026-07-21 by BestHui

#include <Framework/RayTracingAccelerationStructure.h>
//Modify Begin:2026-07-27 by BestHui
#include <Framework\FrameworkDeprecated.h>
//Modify End
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class CommandList;
class CommandContext;
class ShaderBlob;
class StructuredBuffer;
class Texture;
class Resource;
class RayTracingShader;

enum class RayTracingShaderBindingType
{
    OutputTexture,
    AccelerationStructure,
    ConstantBuffer,
    StructuredBuffer,
    TextureArray,
    VertexBufferArray,
    IndexBufferArray
};

struct RayTracingShaderBindingDesc
{
    std::string Name;
    RayTracingShaderBindingType Type = RayTracingShaderBindingType::StructuredBuffer;
    uint32_t ShaderRegister = 0;
    uint32_t RegisterSpace = 0;
    uint32_t DescriptorCount = 1;
    //Modify Begin:2026-07-24 by BestHui
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
    std::vector<RayTracingShaderPassDesc> Passes;
    uint32_t PayloadSizeInBytes = sizeof(float) * 8;
    uint32_t AttributeSizeInBytes = sizeof(float) * 2;
    uint32_t MaxTraceRecursionDepth = 1;
    uint32_t MaxDescriptorCount = 2048;
};

//Modify Begin:2026-07-23 by BestHui
class RayTracingPipelineDescBuilder
{
public:
    RayTracingPipelineDescBuilder();
    explicit RayTracingPipelineDescBuilder(RayTracingPipelineDesc desc);

    static RayTracingPipelineDescBuilder Default();
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
    RayTracingPipelineDescBuilder& WithVertexBufferArray(std::string name, uint32_t shaderRegister, uint32_t registerSpace, uint32_t descriptorCount);
    RayTracingPipelineDescBuilder& WithIndexBufferArray(std::string name, uint32_t shaderRegister, uint32_t registerSpace, uint32_t descriptorCount);

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
};
//Modify End

//Modify Begin:2026-07-24 by BestHui
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
    struct Impl;
    Impl& GetImpl();
    const Impl& GetImpl() const;
    friend class CommandContext;
    std::unique_ptr<Impl> m_Impl;
};
//Modify End

class RayTracingShader
{
public:
    explicit RayTracingShader(const ShaderBlob& shaderLibrary);
    RayTracingShader(const ShaderBlob& shaderLibrary, RayTracingPipelineDesc desc);
    ~RayTracingShader();

    RayTracingShader(const RayTracingShader&) = delete;
    RayTracingShader& operator=(const RayTracingShader&) = delete;
    RayTracingShader(RayTracingShader&&) noexcept;
    RayTracingShader& operator=(RayTracingShader&&) noexcept;

    static bool IsSupported();
    static RayTracingPipelineDesc CreateDefaultPipelineDesc();

    const RayTracingPipelineDesc& GetDesc() const;
    //Modify Begin:2026-07-24 by BestHui
    RayTracingBindingSet CreateBindingSet() const;
    //Modify End

    bool HasBinding(std::string_view name) const;

private:
    //Modify Begin:2026-07-24 by BestHui
    friend class RayTracingBindingSet;
    friend class CommandContext;
    //Modify End
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
//Modify End
