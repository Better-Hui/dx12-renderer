#include "PipelineLayout.h"

//Modify Begin:2026-07-24 by BestHui

#include <DX12Library/Helpers.h>
#include <d3dx12.h>

#include <algorithm>
#include <utility>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

namespace
{
    //Modify Begin:2026-07-27 by BestHui
    PipelineDescriptorRangeDesc MakeRangeFromRootDescriptor(const PipelineRootDescriptorDesc& rootDescriptor)
    {
        PipelineDescriptorRangeDesc range;
        range.Name = rootDescriptor.Name;
        range.Kind = rootDescriptor.Kind;
        range.ShaderRegister = rootDescriptor.ShaderRegister;
        range.RegisterSpace = rootDescriptor.RegisterSpace;
        range.DescriptorCount = 1u;
        range.RootParameterIndex = rootDescriptor.RootParameterIndex;
        range.BindingMode = PipelineDescriptorBindingMode::RootDescriptor;
        range.ShaderStages = rootDescriptor.ShaderStages;
        return range;
    }

    PipelineDescriptorSetDesc& GetOrAddDescriptorSet(PipelineLayoutDesc& desc, const UINT registerSpace)
    {
        auto setFindResult = std::find_if(
            desc.DescriptorSets.begin(),
            desc.DescriptorSets.end(),
            [registerSpace](const PipelineDescriptorSetDesc& setDesc)
            {
                return setDesc.RegisterSpace == registerSpace;
            });

        if (setFindResult == desc.DescriptorSets.end())
        {
            PipelineDescriptorSetDesc setDesc;
            setDesc.RegisterSpace = registerSpace;
            desc.DescriptorSets.push_back(std::move(setDesc));
            return desc.DescriptorSets.back();
        }

        return *setFindResult;
    }
    //Modify End

    UINT GetDescriptorCountOverride(
        const PipelineLayoutReflectionOptions& options,
        const std::string& name,
        const UINT reflectedCount)
    {
        for (const PipelineLayoutBindingOverride& bindingOverride : options.BindingOverrides)
        {
            if (bindingOverride.Name == name)
            {
                return bindingOverride.DescriptorCount;
            }
        }

        return DescriptorLayout::NormalizeDescriptorCount(reflectedCount, options.MaxDescriptorCount);
    }

    bool IsAccelerationStructureSrv(
        const ShaderUtils::ShaderResourceViewMetadata& srv,
        const PipelineLayoutReflectionOptions& options)
    {
        const char* fallbackName = options.AccelerationStructureFallbackName.empty() ?
            nullptr :
            options.AccelerationStructureFallbackName.c_str();
        return DescriptorLayout::IsRayTracingAccelerationStructureSrv(srv, fallbackName);
    }

    void BuildDescriptorSetsFromRanges(PipelineLayoutDesc& desc)
    {
        desc.DescriptorSets.clear();
        for (const PipelineDescriptorRangeDesc& range : desc.DescriptorRanges)
        {
            //Modify Begin:2026-07-27 by BestHui
            if (range.BindingMode == PipelineDescriptorBindingMode::RootDescriptor)
            {
                PipelineRootDescriptorDesc rootDescriptor;
                rootDescriptor.Name = range.Name;
                rootDescriptor.Kind = range.Kind;
                rootDescriptor.ShaderRegister = range.ShaderRegister;
                rootDescriptor.RegisterSpace = range.RegisterSpace;
                rootDescriptor.RootParameterIndex = range.RootParameterIndex;
                rootDescriptor.ShaderStages = range.ShaderStages;
                desc.RootDescriptors.push_back(std::move(rootDescriptor));
                continue;
            }
            //Modify End

            auto setFindResult = std::find_if(
                desc.DescriptorSets.begin(),
                desc.DescriptorSets.end(),
                [&range](const PipelineDescriptorSetDesc& setDesc)
                {
                    return setDesc.RegisterSpace == range.RegisterSpace;
                });

            if (setFindResult == desc.DescriptorSets.end())
            {
                PipelineDescriptorSetDesc setDesc;
                setDesc.RegisterSpace = range.RegisterSpace;
                setDesc.Ranges.push_back(range);
                desc.DescriptorSets.push_back(std::move(setDesc));
            }
            else
            {
                setFindResult->Ranges.push_back(range);
            }
        }
    }

    void FlattenDescriptorSets(PipelineLayoutDesc& desc)
    {
        desc.DescriptorRanges.clear();
        //Modify Begin:2026-07-27 by BestHui
        for (PipelineRootDescriptorDesc& rootDescriptor : desc.RootDescriptors)
        {
            rootDescriptor.RootParameterIndex = static_cast<UINT>(desc.DescriptorRanges.size());
            desc.DescriptorRanges.push_back(MakeRangeFromRootDescriptor(rootDescriptor));
        }
        //Modify End
        for (PipelineDescriptorSetDesc& setDesc : desc.DescriptorSets)
        {
            for (PipelineDescriptorRangeDesc& range : setDesc.Ranges)
            {
                range.RegisterSpace = setDesc.RegisterSpace;
                range.RootParameterIndex = static_cast<UINT>(desc.DescriptorRanges.size());
                desc.DescriptorRanges.push_back(range);
            }
        }
    }

    //Modify Begin:2026-07-27 by BestHui
    bool IsRootParameterWritten(const std::vector<bool>& writtenRootParameters, const UINT rootParameterIndex)
    {
        return rootParameterIndex < writtenRootParameters.size() && writtenRootParameters[rootParameterIndex];
    }

    UINT GetRootParameterCount(const PipelineLayoutDesc& desc)
    {
        UINT rootParameterCount = 0;
        for (const PipelineRootConstantDesc& rootConstant : desc.RootConstants)
        {
            rootParameterCount = std::max(rootParameterCount, rootConstant.RootParameterIndex + 1u);
        }
        for (const PipelineDescriptorRangeDesc& range : desc.DescriptorRanges)
        {
            rootParameterCount = std::max(rootParameterCount, range.RootParameterIndex + 1u);
        }
        return rootParameterCount;
    }
    //Modify End
}

PipelineLayout::PipelineLayout(PipelineLayoutDesc desc)
{
    Reset(std::move(desc));
}

PipelineLayoutDesc PipelineLayout::CreateDescFromReflection(
    const ShaderReflectionMetadata& reflection,
    const PipelineLayoutReflectionOptions& options)
{
    PipelineLayoutDesc desc;
    //Modify Begin:2026-07-27 by BestHui
    desc.ShaderStages = options.ShaderStages;
    //Modify End
    desc.DescriptorRanges.reserve(
        reflection.m_ConstantBuffers.size() +
        reflection.m_ShaderResourceViews.size() +
        reflection.m_UnorderedAccessViews.size());

    const auto appendDescriptorSetRange = [&desc, &options](
        std::string name,
        const DescriptorBindingKind kind,
        const UINT shaderRegister,
        const UINT registerSpace,
        const UINT descriptorCount,
        const PipelineDescriptorBindingMode bindingMode)
    {
        PipelineDescriptorRangeDesc range;
        range.Name = std::move(name);
        range.Kind = kind;
        range.ShaderRegister = shaderRegister;
        range.RegisterSpace = registerSpace;
        range.DescriptorCount = std::max(1u, descriptorCount);
        range.BindingMode = bindingMode;
        range.ShaderStages = options.ShaderStages;
        GetOrAddDescriptorSet(desc, registerSpace).Ranges.push_back(std::move(range));
    };

    //Modify Begin:2026-07-27 by BestHui
    const auto appendRootDescriptor = [&desc, &options](
        std::string name,
        const DescriptorBindingKind kind,
        const UINT shaderRegister,
        const UINT registerSpace)
    {
        PipelineRootDescriptorDesc rootDescriptor;
        rootDescriptor.Name = std::move(name);
        rootDescriptor.Kind = kind;
        rootDescriptor.ShaderRegister = shaderRegister;
        rootDescriptor.RegisterSpace = registerSpace;
        rootDescriptor.RootParameterIndex = static_cast<UINT>(desc.RootDescriptors.size());
        rootDescriptor.ShaderStages = options.ShaderStages;
        desc.RootDescriptors.push_back(std::move(rootDescriptor));
    };
    //Modify End

    for (const auto& cbuffer : reflection.m_ConstantBuffers)
    {
        appendRootDescriptor(
            cbuffer.Name,
            DescriptorBindingKind::ConstantBuffer,
            cbuffer.RegisterIndex,
            cbuffer.Space);
    }

    for (const auto& srv : reflection.m_ShaderResourceViews)
    {
        if (IsAccelerationStructureSrv(srv, options))
        {
            appendRootDescriptor(
                srv.Name,
                DescriptorBindingKind::AccelerationStructure,
                srv.RegisterIndex,
                srv.Space);
            continue;
        }

        appendDescriptorSetRange(
            srv.Name,
            DescriptorBindingKind::ShaderResourceView,
            srv.RegisterIndex,
            srv.Space,
            GetDescriptorCountOverride(options, srv.Name, srv.BindCount),
            PipelineDescriptorBindingMode::DescriptorTable);
    }

    for (const auto& uav : reflection.m_UnorderedAccessViews)
    {
        appendDescriptorSetRange(
            uav.Name,
            DescriptorBindingKind::UnorderedAccessView,
            uav.RegisterIndex,
            uav.Space,
            GetDescriptorCountOverride(options, uav.Name, uav.BindCount),
            PipelineDescriptorBindingMode::DescriptorTable);
    }

    //Modify Begin:2026-07-27 by BestHui
    FlattenDescriptorSets(desc);
    //Modify End
    return desc;
}

void PipelineLayout::Reset(PipelineLayoutDesc desc)
{
    if (desc.DescriptorRanges.empty() && !desc.DescriptorSets.empty())
    {
        FlattenDescriptorSets(desc);
    }
    else if (desc.DescriptorSets.empty())
    {
        BuildDescriptorSetsFromRanges(desc);
    }

    m_Desc = std::move(desc);
    RebuildDescriptorLayout();
}

//Modify Begin:2026-07-27 by BestHui
void PipelineLayout::SetRootSignature(std::shared_ptr<RootSignature> rootSignature)
{
    m_RootSignature = std::move(rootSignature);
}

std::shared_ptr<RootSignature> PipelineLayout::CreateRootSignature(const PipelineRootSignatureBuildDesc& buildDesc) const
{
    using RootParameter = CD3DX12_ROOT_PARAMETER1;
    using DescriptorRange = CD3DX12_DESCRIPTOR_RANGE1;

    const UINT rootParameterCount = GetRootParameterCount(m_Desc);
    std::vector<RootParameter> rootParameters(rootParameterCount);
    std::vector<bool> writtenRootParameters(rootParameterCount, false);

    UINT descriptorTableCount = 0;
    for (const PipelineDescriptorRangeDesc& range : m_Desc.DescriptorRanges)
    {
        if (range.BindingMode == PipelineDescriptorBindingMode::DescriptorTable)
        {
            ++descriptorTableCount;
        }
    }

    std::vector<DescriptorRange> descriptorRanges;
    descriptorRanges.reserve(descriptorTableCount);

    for (const PipelineRootConstantDesc& rootConstant : m_Desc.RootConstants)
    {
        Assert(rootConstant.RootParameterIndex < rootParameters.size(), "Pipeline root constant index is out of range.");
        Assert(!IsRootParameterWritten(writtenRootParameters, rootConstant.RootParameterIndex), "Duplicate pipeline root parameter index.");
        const UINT constantCount = static_cast<UINT>((rootConstant.SizeInBytes + 3u) / 4u);
        rootParameters[rootConstant.RootParameterIndex].InitAsConstants(
            constantCount,
            rootConstant.ShaderRegister,
            rootConstant.RegisterSpace,
            GetD3D12ShaderVisibility(rootConstant.ShaderStages));
        writtenRootParameters[rootConstant.RootParameterIndex] = true;
    }

    for (const PipelineDescriptorRangeDesc& range : m_Desc.DescriptorRanges)
    {
        Assert(range.RootParameterIndex < rootParameters.size(), "Pipeline descriptor range index is out of range.");
        Assert(!IsRootParameterWritten(writtenRootParameters, range.RootParameterIndex), "Duplicate pipeline root parameter index.");

        if (range.BindingMode == PipelineDescriptorBindingMode::RootDescriptor)
        {
            if (range.Kind == DescriptorBindingKind::ConstantBuffer)
            {
                rootParameters[range.RootParameterIndex].InitAsConstantBufferView(
                    range.ShaderRegister,
                    range.RegisterSpace,
                    D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                    GetD3D12ShaderVisibility(range.ShaderStages));
            }
            else if (range.Kind == DescriptorBindingKind::UnorderedAccessView)
            {
                rootParameters[range.RootParameterIndex].InitAsUnorderedAccessView(
                    range.ShaderRegister,
                    range.RegisterSpace,
                    D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                    GetD3D12ShaderVisibility(range.ShaderStages));
            }
            else
            {
                rootParameters[range.RootParameterIndex].InitAsShaderResourceView(
                    range.ShaderRegister,
                    range.RegisterSpace,
                    D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                    GetD3D12ShaderVisibility(range.ShaderStages));
            }
        }
        else
        {
            const D3D12_DESCRIPTOR_RANGE_TYPE rangeType = range.Kind == DescriptorBindingKind::UnorderedAccessView ?
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV :
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            descriptorRanges.emplace_back(
                rangeType,
                range.DescriptorCount,
                range.ShaderRegister,
                range.RegisterSpace,
                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
            rootParameters[range.RootParameterIndex].InitAsDescriptorTable(
                1,
                &descriptorRanges.back(),
                GetD3D12ShaderVisibility(range.ShaderStages));
        }

        writtenRootParameters[range.RootParameterIndex] = true;
    }

    for (UINT rootParameterIndex = 0; rootParameterIndex < rootParameterCount; ++rootParameterIndex)
    {
        Assert(writtenRootParameters[rootParameterIndex], "Pipeline layout root parameter indices must be dense.");
    }

    std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers = buildDesc.StaticSamplers;
    staticSamplers.reserve(staticSamplers.size() + m_Desc.RootSamplers.size());
    for (const PipelineRootSamplerDesc& rootSampler : m_Desc.RootSamplers)
    {
        D3D12_STATIC_SAMPLER_DESC samplerDesc = rootSampler.Desc;
        samplerDesc.ShaderRegister = rootSampler.ShaderRegister;
        samplerDesc.RegisterSpace = rootSampler.RegisterSpace;
        samplerDesc.ShaderVisibility = GetD3D12ShaderVisibility(rootSampler.ShaderStages);
        staticSamplers.push_back(samplerDesc);
    }

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc{};
    rootSignatureDesc.NumParameters = static_cast<UINT>(rootParameters.size());
    rootSignatureDesc.pParameters = rootParameters.data();
    rootSignatureDesc.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
    rootSignatureDesc.pStaticSamplers = staticSamplers.data();
    rootSignatureDesc.Flags = buildDesc.Flags;

    return std::make_shared<RootSignature>(rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1);
}
//Modify End

const PipelineDescriptorRangeDesc* PipelineLayout::FindRange(
    const std::string& name,
    const DescriptorBindingKind expectedKind) const
{
    const std::string baseName = DescriptorLayout::GetBaseResourceName(name);
    const auto findResult = std::find_if(
        m_Desc.DescriptorRanges.begin(),
        m_Desc.DescriptorRanges.end(),
        [&name, &baseName, expectedKind](const PipelineDescriptorRangeDesc& range)
        {
            return range.Kind == expectedKind &&
                (range.Name == name || DescriptorLayout::GetBaseResourceName(range.Name) == baseName);
        });

    return findResult != m_Desc.DescriptorRanges.end() ? &*findResult : nullptr;
}

const PipelineDescriptorRangeDesc* PipelineLayout::FindRangeByRootParameterIndex(const UINT rootParameterIndex) const
{
    const auto findResult = std::find_if(
        m_Desc.DescriptorRanges.begin(),
        m_Desc.DescriptorRanges.end(),
        [rootParameterIndex](const PipelineDescriptorRangeDesc& range)
        {
            return range.RootParameterIndex == rootParameterIndex;
        });

    return findResult != m_Desc.DescriptorRanges.end() ? &*findResult : nullptr;
}

bool PipelineLayout::HasBinding(const std::string& name, const DescriptorBindingKind expectedKind) const
{
    return FindRange(name, expectedKind) != nullptr;
}

const DescriptorBindingInfo& PipelineLayout::GetBinding(
    const std::string& name,
    const DescriptorBindingKind expectedKind) const
{
    return m_DescriptorLayout.GetBinding(name, expectedKind);
}

const DescriptorBindingInfo& PipelineLayout::GetFirstBinding(const DescriptorBindingKind expectedKind) const
{
    return m_DescriptorLayout.GetFirstBinding(expectedKind);
}

void PipelineLayout::AddDefaultShaderResourceViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const ShaderUtils::ShaderResourceViewMetadata& srv)
{
    m_DescriptorLayout.AddDefaultShaderResourceViewTable(rootParameterIndex, descriptorCount, srv);
}

void PipelineLayout::AddDefaultUnorderedAccessViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const ShaderUtils::UnorderedAccessViewMetadata& uav)
{
    m_DescriptorLayout.AddDefaultUnorderedAccessViewTable(rootParameterIndex, descriptorCount, uav);
}

void PipelineLayout::StageDefaultDescriptorTables(CommandList& commandList) const
{
    m_DescriptorLayout.StageDefaultDescriptorTables(commandList);
}

void PipelineLayout::RebuildDescriptorLayout()
{
    m_DescriptorLayout = DescriptorLayout();
    for (const PipelineDescriptorRangeDesc& range : m_Desc.DescriptorRanges)
    {
        DescriptorBindingInfo binding;
        binding.Kind = range.Kind;
        binding.RootParameterIndex = range.RootParameterIndex;
        binding.DescriptorCount = range.DescriptorCount;
        m_DescriptorLayout.AddBinding(range.Name, binding);
    }
}

//Modify End
