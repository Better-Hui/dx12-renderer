#include <Framework/Rendering/Pipeline/PipelineLayout.h>

//Modify Begin:2026-07-24 by BestHui

#include <DX12Library/Helpers.h>
#include <d3dx12.h>

#include <algorithm>
#include <sstream>
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

//Modify Begin:2026-07-31 by BestHui
    bool IsRootConstantBufferName(const PipelineLayoutReflectionOptions& options, const std::string& name)
    {
        const std::string baseName = DescriptorLayout::GetBaseResourceName(name);
        return std::any_of(
            options.RootConstantBufferNames.begin(),
            options.RootConstantBufferNames.end(),
            [&name, &baseName](const std::string& rootConstantName)
            {
                return rootConstantName == name || rootConstantName == baseName;
            });
    }
//Modify End

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
        //Modify Begin:2026-07-31 by BestHui
        UINT rootParameterIndex = 0;
        for (PipelineRootConstantDesc& rootConstant : desc.RootConstants)
        {
            rootConstant.RootParameterIndex = rootParameterIndex++;
        }
        for (PipelineRootDescriptorDesc& rootDescriptor : desc.RootDescriptors)
        {
            rootDescriptor.RootParameterIndex = rootParameterIndex++;
            desc.DescriptorRanges.push_back(MakeRangeFromRootDescriptor(rootDescriptor));
        }
        //Modify End
        for (PipelineDescriptorSetDesc& setDesc : desc.DescriptorSets)
        {
            for (PipelineDescriptorRangeDesc& range : setDesc.Ranges)
            {
                range.RegisterSpace = setDesc.RegisterSpace;
                range.RootParameterIndex = rootParameterIndex++;
                desc.DescriptorRanges.push_back(range);
            }
        }
        //Modify End
    }

    //Modify Begin:2026-07-27 by BestHui
    bool IsRootParameterWritten(const std::vector<bool>& writtenRootParameters, const UINT rootParameterIndex)
    {
        return rootParameterIndex < writtenRootParameters.size() && writtenRootParameters[rootParameterIndex];
    }

    const char* ToString(const DescriptorBindingKind kind)
    {
        switch (kind)
        {
        case DescriptorBindingKind::ConstantBuffer:
            return "CBV";
        case DescriptorBindingKind::ShaderResourceView:
            return "SRV";
        case DescriptorBindingKind::UnorderedAccessView:
            return "UAV";
        case DescriptorBindingKind::AccelerationStructure:
            return "AccelerationStructure";
        default:
            return "Unknown";
        }
    }

    std::string DescribePipelineLayout(const PipelineLayoutDesc& desc)
    {
        std::ostringstream stream;
        stream << " PipelineLayout ranges:";
        for (const PipelineDescriptorRangeDesc& range : desc.DescriptorRanges)
        {
            stream << " [root=" << range.RootParameterIndex
                << ", name=" << range.Name
                << ", kind=" << ToString(range.Kind)
                << ", reg=" << range.ShaderRegister
                << ", space=" << range.RegisterSpace
                << ", count=" << range.DescriptorCount
                << ", mode=" << (range.BindingMode == PipelineDescriptorBindingMode::RootDescriptor ? "root" : "table")
                << "]";
        }
        return stream.str();
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

    D3D12_TEXTURE_ADDRESS_MODE GetSamplerAddressMode(const std::string& name)
    {
        return name.find("Clamp") != std::string::npos ?
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP :
            D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }

    D3D12_FILTER GetSamplerFilter(const std::string& name)
    {
        const bool isPoint = name.find("Point") != std::string::npos;
        const bool isComparison =
            name.find("Comparison") != std::string::npos ||
            name.find("Shadow") != std::string::npos;

        if (isComparison)
        {
            return isPoint ?
                D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT :
                D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        }

        return isPoint ?
            D3D12_FILTER_MIN_MAG_MIP_POINT :
            D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    }

    D3D12_STATIC_SAMPLER_DESC CreateStaticSamplerDesc(const ShaderUtils::SamplerMetadata& sampler)
    {
        D3D12_STATIC_SAMPLER_DESC desc = {};
        desc.Filter = GetSamplerFilter(sampler.Name);
        desc.AddressU = GetSamplerAddressMode(sampler.Name);
        desc.AddressV = desc.AddressU;
        desc.AddressW = desc.AddressU;
        desc.MipLODBias = 0.0f;
        desc.MaxAnisotropy = 16u;
        desc.ComparisonFunc =
            desc.Filter == D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT ||
            desc.Filter == D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR ?
            D3D12_COMPARISON_FUNC_LESS_EQUAL :
            D3D12_COMPARISON_FUNC_ALWAYS;
        desc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        desc.MinLOD = 0.0f;
        desc.MaxLOD = D3D12_FLOAT32_MAX;
        desc.ShaderRegister = sampler.RegisterIndex;
        desc.RegisterSpace = sampler.Space;
        desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        return desc;
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

    const auto appendRootConstant = [&desc, &options](
        std::string name,
        const UINT shaderRegister,
        const UINT registerSpace,
        const UINT sizeInBytes)
    {
        PipelineRootConstantDesc rootConstant;
        rootConstant.Name = std::move(name);
        rootConstant.ShaderRegister = shaderRegister;
        rootConstant.RegisterSpace = registerSpace;
        rootConstant.SizeInBytes = sizeInBytes;
        rootConstant.RootParameterIndex = static_cast<UINT>(desc.RootConstants.size());
        rootConstant.ShaderStages = options.ShaderStages;
        desc.RootConstants.push_back(std::move(rootConstant));
    };
    //Modify End

    for (const auto& cbuffer : reflection.m_ConstantBuffers)
    {
//Modify Begin:2026-07-31 by BestHui
        if (IsRootConstantBufferName(options, cbuffer.Name))
        {
            appendRootConstant(
                cbuffer.Name,
                cbuffer.RegisterIndex,
                cbuffer.Space,
                cbuffer.Size);
            continue;
        }
//Modify End
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
    for (const auto& sampler : reflection.m_Samplers)
    {
        const UINT samplerCount = DescriptorLayout::NormalizeDescriptorCount(sampler.BindCount, options.MaxDescriptorCount);
        for (UINT i = 0; i < samplerCount; ++i)
        {
            PipelineRootSamplerDesc rootSampler;
            rootSampler.Name = sampler.Name;
            rootSampler.ShaderRegister = sampler.RegisterIndex + i;
            rootSampler.RegisterSpace = sampler.Space;
            rootSampler.Desc = CreateStaticSamplerDesc(sampler);
            rootSampler.Desc.ShaderRegister = rootSampler.ShaderRegister;
            rootSampler.ShaderStages = options.ShaderStages;
            desc.RootSamplers.push_back(std::move(rootSampler));
        }
    }
//Modify End

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

    try
    {
        return std::make_shared<RootSignature>(rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1);
    }
    catch (const std::exception& exception)
    {
        throw std::runtime_error(std::string(exception.what()) + DescribePipelineLayout(m_Desc));
    }
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

//Modify Begin:2026-07-31 by BestHui
const PipelineRootConstantDesc* PipelineLayout::FindRootConstant(const std::string& name) const
{
    const std::string baseName = DescriptorLayout::GetBaseResourceName(name);
    const auto findResult = std::find_if(
        m_Desc.RootConstants.begin(),
        m_Desc.RootConstants.end(),
        [&name, &baseName](const PipelineRootConstantDesc& rootConstant)
        {
            return rootConstant.Name == name || DescriptorLayout::GetBaseResourceName(rootConstant.Name) == baseName;
        });

    return findResult != m_Desc.RootConstants.end() ? &*findResult : nullptr;
}
//Modify End

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

void PipelineLayout::AddDefaultShaderResourceViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc)
{
    m_DescriptorLayout.AddDefaultShaderResourceViewTable(rootParameterIndex, descriptorCount, srvDesc);
}

void PipelineLayout::AddDefaultUnorderedAccessViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const ShaderUtils::UnorderedAccessViewMetadata& uav)
{
    m_DescriptorLayout.AddDefaultUnorderedAccessViewTable(rootParameterIndex, descriptorCount, uav);
}

void PipelineLayout::AddDefaultUnorderedAccessViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC& uavDesc)
{
    m_DescriptorLayout.AddDefaultUnorderedAccessViewTable(rootParameterIndex, descriptorCount, uavDesc);
}

void PipelineLayout::StageDefaultDescriptorTables(CommandList& commandList) const
{
    m_DescriptorLayout.StageDefaultDescriptorTables(commandList);
}

//Modify Begin:2026-07-27 by BestHui
const DescriptorAllocation* PipelineLayout::FindDefaultDescriptorTable(const UINT rootParameterIndex) const
{
    return m_DescriptorLayout.FindDefaultDescriptorTable(rootParameterIndex);
}
//Modify End

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
