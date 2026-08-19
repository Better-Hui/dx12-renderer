#include "ShaderUtils.h"
#include <dxcapi.h>
//Modify Begin:2026-07-21 by Hui
#include <d3dcompiler.h>
//Modify End
#include "Helpers.h"

#include <unordered_set>

namespace hlsl
{
    // https://github.com/microsoft/DirectXShaderCompiler/blob/main/include/dxc/DxilContainer/DxilContainer.h
#define DXIL_FOURCC(ch0, ch1, ch2, ch3) (                            \
        (uint32_t)(uint8_t)(ch0)        | (uint32_t)(uint8_t)(ch1) << 8  | \
        (uint32_t)(uint8_t)(ch2) << 16  | (uint32_t)(uint8_t)(ch3) << 24   \
        )

    enum DxilFourCC
    {
        DFCC_Container = DXIL_FOURCC('D', 'X', 'B', 'C'), // for back-compat with tools that look for DXBC containers
        DFCC_ResourceDef = DXIL_FOURCC('R', 'D', 'E', 'F'),
        DFCC_InputSignature = DXIL_FOURCC('I', 'S', 'G', '1'),
        DFCC_OutputSignature = DXIL_FOURCC('O', 'S', 'G', '1'),
        DFCC_PatchConstantSignature = DXIL_FOURCC('P', 'S', 'G', '1'),
        DFCC_ShaderStatistics = DXIL_FOURCC('S', 'T', 'A', 'T'),
        DFCC_ShaderDebugInfoDXIL = DXIL_FOURCC('I', 'L', 'D', 'B'),
        DFCC_ShaderDebugName = DXIL_FOURCC('I', 'L', 'D', 'N'),
        DFCC_FeatureInfo = DXIL_FOURCC('S', 'F', 'I', '0'),
        DFCC_PrivateData = DXIL_FOURCC('P', 'R', 'I', 'V'),
        DFCC_RootSignature = DXIL_FOURCC('R', 'T', 'S', '0'),
        DFCC_DXIL = DXIL_FOURCC('D', 'X', 'I', 'L'),
        DFCC_PipelineStateValidation = DXIL_FOURCC('P', 'S', 'V', '0'),
        DFCC_RuntimeData = DXIL_FOURCC('R', 'D', 'A', 'T'),
        DFCC_ShaderHash = DXIL_FOURCC('H', 'A', 'S', 'H'),
        DFCC_ShaderSourceInfo = DXIL_FOURCC('S', 'R', 'C', 'I'),
        DFCC_ShaderPDBInfo = DXIL_FOURCC('P', 'D', 'B', 'I'),
        DFCC_CompilerVersion = DXIL_FOURCC('V', 'E', 'R', 'S'),
    };

#undef DXIL_FOURCC
}

namespace
{
    std::string GetBaseResourceName(const char* name)
    {
        std::string result = name != nullptr ? name : "";
        const size_t arraySuffix = result.find("[0]");
        if (arraySuffix != std::string::npos)
        {
            result = result.substr(0, arraySuffix);
        }
        return result;
    }

    std::string BuildResourceKey(const std::string& name, const UINT bindPoint, const UINT space)
    {
        return name + "#" + std::to_string(bindPoint) + "#" + std::to_string(space);
    }

    bool IsShaderResourceViewType(const D3D_SHADER_INPUT_TYPE inputType)
    {
        switch (inputType)
        {
        case D3D_SIT_BYTEADDRESS:
        case D3D_SIT_TEXTURE:
        case D3D_SIT_STRUCTURED:
        case D3D_SIT_RTACCELERATIONSTRUCTURE:
            return true;
        default:
            return false;
        }
    }

    bool IsUnorderedAccessViewType(const D3D_SHADER_INPUT_TYPE inputType)
    {
        switch (inputType)
        {
        case D3D_SIT_UAV_RWTYPED:
        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
        case D3D_SIT_UAV_APPEND_STRUCTURED:
        case D3D_SIT_UAV_CONSUME_STRUCTURED:
        case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            return true;
        default:
            return false;
        }
    }

//Modify Begin:2026-07-27 by Hui
    bool IsSamplerType(const D3D_SHADER_INPUT_TYPE inputType)
    {
        return inputType == D3D_SIT_SAMPLER;
    }
//Modify End
}



Microsoft::WRL::ComPtr<ID3DBlob> ShaderUtils::LoadShaderFromFile(const std::wstring& fileName)
{
    const auto completePath = L"Shaders/" + fileName;

    Microsoft::WRL::ComPtr<ID3DBlob> result;
//Modify Begin:2026-08-07 by Hui
    ThrowIfFailed(D3DReadFileToBlob(completePath.c_str(), &result));
//Modify End

    return result;
}

Microsoft::WRL::ComPtr<ID3D12ShaderReflection> ShaderUtils::Reflect(const Microsoft::WRL::ComPtr<ID3DBlob>& shaderSource)
{
    Microsoft::WRL::ComPtr<IDxcBlob> shaderSourceDxc;
    ThrowIfFailed(shaderSource.As(&shaderSourceDxc));

    Microsoft::WRL::ComPtr<IDxcContainerReflection> pReflection;
    UINT32 shaderIdx;
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&pReflection)));
    ThrowIfFailed(pReflection->Load(shaderSourceDxc.Get()));
//Modify Begin:2026-07-21 by Hui
    const HRESULT findDxilResult = pReflection->FindFirstPartKind(hlsl::DFCC_DXIL, &shaderIdx);
    if (FAILED(findDxilResult))
    {
        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
        ThrowIfFailed(D3DReflect(
            shaderSource->GetBufferPointer(),
            shaderSource->GetBufferSize(),
            IID_PPV_ARGS(&reflection)));
        return reflection;
    }
//Modify End

    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
    ThrowIfFailed(pReflection->GetPartReflection(shaderIdx, __uuidof(ID3D12ShaderReflection), (void**)&reflection));

    return reflection;
}

//Modify Begin:2026-07-23 by Hui
Microsoft::WRL::ComPtr<ID3D12LibraryReflection> ShaderUtils::ReflectLibrary(const Microsoft::WRL::ComPtr<ID3DBlob>& shaderSource)
{
    Microsoft::WRL::ComPtr<IDxcBlob> shaderSourceDxc;
    ThrowIfFailed(shaderSource.As(&shaderSourceDxc));

    Microsoft::WRL::ComPtr<IDxcContainerReflection> reflection;
    UINT32 shaderIndex;
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&reflection)));
    ThrowIfFailed(reflection->Load(shaderSourceDxc.Get()));
    ThrowIfFailed(reflection->FindFirstPartKind(hlsl::DFCC_DXIL, &shaderIndex));

    Microsoft::WRL::ComPtr<ID3D12LibraryReflection> libraryReflection;
    ThrowIfFailed(reflection->GetPartReflection(shaderIndex, __uuidof(ID3D12LibraryReflection), reinterpret_cast<void**>(libraryReflection.GetAddressOf())));
    return libraryReflection;
}
//Modify End

std::vector<ShaderUtils::ConstantBufferMetadata> ShaderUtils::GetConstantBuffers(const Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& shaderReflection)
{
    // https://github.com/planetpratik/DirectXTutorials/blob/master/SimpleShader.cpp
    D3D12_SHADER_DESC shaderDesc;
    ThrowIfFailed(shaderReflection->GetDesc(&shaderDesc));

    std::vector<ShaderUtils::ConstantBufferMetadata> result;
    result.reserve(shaderDesc.ConstantBuffers);

    for (UINT cbufferIndex = 0; cbufferIndex < shaderDesc.ConstantBuffers; ++cbufferIndex)
    {
        const auto constantBuffer = shaderReflection->GetConstantBufferByIndex(cbufferIndex);
        D3D12_SHADER_BUFFER_DESC cbufferDesc;
        ThrowIfFailed(constantBuffer->GetDesc(&cbufferDesc));

//Modify Begin:2026-07-30 by Hui
        D3D12_SHADER_INPUT_BIND_DESC inputBindDesc{};
        HRESULT bindingResult = shaderReflection->GetResourceBindingDescByName(cbufferDesc.Name, &inputBindDesc);
        if (SUCCEEDED(bindingResult) && inputBindDesc.Type != D3D_SIT_CBUFFER)
        {
            bindingResult = E_FAIL;
        }
        if (FAILED(bindingResult))
        {
            bool foundBinding = false;
            for (UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex)
            {
                D3D12_SHADER_INPUT_BIND_DESC resourceBindDesc{};
                if (FAILED(shaderReflection->GetResourceBindingDesc(resourceIndex, &resourceBindDesc)))
                {
                    continue;
                }

                if (resourceBindDesc.Type == D3D_SIT_CBUFFER && cbufferDesc.Name == std::string(resourceBindDesc.Name))
                {
                    inputBindDesc = resourceBindDesc;
                    foundBinding = true;
                    break;
                }
            }

            if (!foundBinding)
            {
                continue;
            }
        }
//Modify End

        ConstantBufferMetadata constantBufferMetadata;
        constantBufferMetadata.Name = cbufferDesc.Name;
        constantBufferMetadata.RegisterIndex = inputBindDesc.BindPoint;
        constantBufferMetadata.Space = inputBindDesc.Space;
        constantBufferMetadata.Variables.reserve(cbufferDesc.Variables);
        constantBufferMetadata.Size = 0;

        for (UINT variableIndex = 0; variableIndex < cbufferDesc.Variables; ++variableIndex)
        {
            auto variable = constantBuffer->GetVariableByIndex(variableIndex);
            D3D12_SHADER_VARIABLE_DESC variableDesc;
            ThrowIfFailed(variable->GetDesc(&variableDesc));

            ConstantBufferMetadata::Variable variableMetadata;
            variableMetadata.Name = variableDesc.Name;
            variableMetadata.Offset = variableDesc.StartOffset;
            variableMetadata.Size = variableDesc.Size;

            if (variableDesc.DefaultValue != nullptr)
            {
                variableMetadata.DefaultValue = std::shared_ptr<uint8_t[]>(new uint8_t[variableDesc.Size]);
                memcpy(variableMetadata.DefaultValue.get(), variableDesc.DefaultValue, variableDesc.Size);
            }
            else
            {
                variableDesc.DefaultValue = nullptr;
            }


//Modify Begin:2026-08-18 by Hui
            constantBufferMetadata.Size = std::max(
                constantBufferMetadata.Size,
                variableMetadata.Offset + variableMetadata.Size);
//Modify End
            constantBufferMetadata.Variables.push_back(variableMetadata);
        }

        result.push_back(constantBufferMetadata);
    }

    return result;
}

//Modify Begin:2026-08-18 by Hui
std::vector<ShaderUtils::ConstantBufferMetadata> ShaderUtils::GetConstantBuffers(const Microsoft::WRL::ComPtr<ID3D12LibraryReflection>& libraryReflection)
{
    D3D12_LIBRARY_DESC libraryDesc{};
    ThrowIfFailed(libraryReflection->GetDesc(&libraryDesc));

    std::vector<ShaderUtils::ConstantBufferMetadata> result;
    std::unordered_set<std::string> visitedResources;

    for (UINT functionIndex = 0; functionIndex < libraryDesc.FunctionCount; ++functionIndex)
    {
        ID3D12FunctionReflection* functionReflection = libraryReflection->GetFunctionByIndex(functionIndex);
        if (functionReflection == nullptr)
        {
            continue;
        }

        D3D12_FUNCTION_DESC functionDesc{};
        ThrowIfFailed(functionReflection->GetDesc(&functionDesc));

        for (UINT cbufferIndex = 0; cbufferIndex < functionDesc.ConstantBuffers; ++cbufferIndex)
        {
            ID3D12ShaderReflectionConstantBuffer* constantBuffer = functionReflection->GetConstantBufferByIndex(cbufferIndex);
            D3D12_SHADER_BUFFER_DESC cbufferDesc{};
            ThrowIfFailed(constantBuffer->GetDesc(&cbufferDesc));

            D3D12_SHADER_INPUT_BIND_DESC inputBindDesc{};
            if (FAILED(functionReflection->GetResourceBindingDescByName(cbufferDesc.Name, &inputBindDesc)))
            {
                continue;
            }
            if (inputBindDesc.Type != D3D_SIT_CBUFFER)
            {
                continue;
            }

            const std::string name = GetBaseResourceName(cbufferDesc.Name);
            const std::string key = BuildResourceKey(name, inputBindDesc.BindPoint, inputBindDesc.Space);
            if (!visitedResources.insert(key).second)
            {
                continue;
            }

            ConstantBufferMetadata constantBufferMetadata;
            constantBufferMetadata.Name = name;
            constantBufferMetadata.RegisterIndex = inputBindDesc.BindPoint;
            constantBufferMetadata.Space = inputBindDesc.Space;
            constantBufferMetadata.Size = 0;
            constantBufferMetadata.Variables.reserve(cbufferDesc.Variables);

            for (UINT variableIndex = 0; variableIndex < cbufferDesc.Variables; ++variableIndex)
            {
                ID3D12ShaderReflectionVariable* variable = constantBuffer->GetVariableByIndex(variableIndex);
                D3D12_SHADER_VARIABLE_DESC variableDesc{};
                ThrowIfFailed(variable->GetDesc(&variableDesc));

                ConstantBufferMetadata::Variable variableMetadata;
                variableMetadata.Name = variableDesc.Name;
                variableMetadata.Offset = variableDesc.StartOffset;
                variableMetadata.Size = variableDesc.Size;
                constantBufferMetadata.Size = std::max(
                    constantBufferMetadata.Size,
                    variableMetadata.Offset + variableMetadata.Size);
                constantBufferMetadata.Variables.push_back(variableMetadata);
            }

            result.push_back(std::move(constantBufferMetadata));
        }
    }

    return result;
}
//Modify End

std::vector<ShaderUtils::ShaderResourceViewMetadata> ShaderUtils::GetShaderResourceViews(const Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& shaderReflection)
{
    // https://github.com/planetpratik/DirectXTutorials/blob/master/SimpleShader.cpp
    D3D12_SHADER_DESC shaderDesc;
    ThrowIfFailed(shaderReflection->GetDesc(&shaderDesc));

    std::vector<ShaderUtils::ShaderResourceViewMetadata> result;
    result.reserve(shaderDesc.BoundResources);

    for (UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex)
    {
        D3D12_SHADER_INPUT_BIND_DESC inputBindDesc;
        ThrowIfFailed(shaderReflection->GetResourceBindingDesc(resourceIndex, &inputBindDesc));

        if (IsShaderResourceViewType(inputBindDesc.Type))
        {
            ShaderResourceViewMetadata resourceMetadata;
            resourceMetadata.Name = GetBaseResourceName(inputBindDesc.Name);
            resourceMetadata.RegisterIndex = inputBindDesc.BindPoint;
            resourceMetadata.Space = inputBindDesc.Space;
//Modify Begin:2026-07-23 by Hui
            resourceMetadata.BindCount = inputBindDesc.BindCount;
            resourceMetadata.InputType = inputBindDesc.Type;
            resourceMetadata.Dimension = inputBindDesc.Dimension;
//Modify End
            result.push_back(resourceMetadata);
        }
    }

    return result;
}

//Modify Begin:2026-07-23 by Hui
std::vector<ShaderUtils::ShaderResourceViewMetadata> ShaderUtils::GetShaderResourceViews(const Microsoft::WRL::ComPtr<ID3D12LibraryReflection>& libraryReflection)
{
    D3D12_LIBRARY_DESC libraryDesc{};
    ThrowIfFailed(libraryReflection->GetDesc(&libraryDesc));

    std::vector<ShaderUtils::ShaderResourceViewMetadata> result;
    std::unordered_set<std::string> visitedResources;

    for (UINT functionIndex = 0; functionIndex < libraryDesc.FunctionCount; ++functionIndex)
    {
        ID3D12FunctionReflection* functionReflection = libraryReflection->GetFunctionByIndex(functionIndex);
        if (functionReflection == nullptr)
        {
            continue;
        }

        D3D12_FUNCTION_DESC functionDesc{};
        ThrowIfFailed(functionReflection->GetDesc(&functionDesc));

        for (UINT resourceIndex = 0; resourceIndex < functionDesc.BoundResources; ++resourceIndex)
        {
            D3D12_SHADER_INPUT_BIND_DESC inputBindDesc{};
            ThrowIfFailed(functionReflection->GetResourceBindingDesc(resourceIndex, &inputBindDesc));
            if (!IsShaderResourceViewType(inputBindDesc.Type))
            {
                continue;
            }

            const std::string name = GetBaseResourceName(inputBindDesc.Name);
            const std::string key = BuildResourceKey(name, inputBindDesc.BindPoint, inputBindDesc.Space);
            if (!visitedResources.insert(key).second)
            {
                continue;
            }

            ShaderResourceViewMetadata resourceMetadata;
            resourceMetadata.Name = name;
            resourceMetadata.RegisterIndex = inputBindDesc.BindPoint;
            resourceMetadata.Space = inputBindDesc.Space;
            resourceMetadata.BindCount = inputBindDesc.BindCount;
            resourceMetadata.InputType = inputBindDesc.Type;
            resourceMetadata.Dimension = inputBindDesc.Dimension;
            result.push_back(resourceMetadata);
        }
    }

    return result;
}

std::vector<ShaderUtils::UnorderedAccessViewMetadata> ShaderUtils::GetUnorderedAccessViews(const Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& shaderReflection)
{
    D3D12_SHADER_DESC shaderDesc;
    ThrowIfFailed(shaderReflection->GetDesc(&shaderDesc));

    std::vector<ShaderUtils::UnorderedAccessViewMetadata> result;
    result.reserve(shaderDesc.BoundResources);

    for (UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex)
    {
        D3D12_SHADER_INPUT_BIND_DESC inputBindDesc;
        ThrowIfFailed(shaderReflection->GetResourceBindingDesc(resourceIndex, &inputBindDesc));

        if (IsUnorderedAccessViewType(inputBindDesc.Type))
        {
            UnorderedAccessViewMetadata resourceMetadata;
            resourceMetadata.Name = GetBaseResourceName(inputBindDesc.Name);
            resourceMetadata.RegisterIndex = inputBindDesc.BindPoint;
            resourceMetadata.Space = inputBindDesc.Space;
            resourceMetadata.BindCount = inputBindDesc.BindCount;
            resourceMetadata.InputType = inputBindDesc.Type;
            resourceMetadata.Dimension = inputBindDesc.Dimension;
            result.push_back(resourceMetadata);
        }
    }

    return result;
}

std::vector<ShaderUtils::UnorderedAccessViewMetadata> ShaderUtils::GetUnorderedAccessViews(const Microsoft::WRL::ComPtr<ID3D12LibraryReflection>& libraryReflection)
{
    D3D12_LIBRARY_DESC libraryDesc{};
    ThrowIfFailed(libraryReflection->GetDesc(&libraryDesc));

    std::vector<ShaderUtils::UnorderedAccessViewMetadata> result;
    std::unordered_set<std::string> visitedResources;

    for (UINT functionIndex = 0; functionIndex < libraryDesc.FunctionCount; ++functionIndex)
    {
        ID3D12FunctionReflection* functionReflection = libraryReflection->GetFunctionByIndex(functionIndex);
        if (functionReflection == nullptr)
        {
            continue;
        }

        D3D12_FUNCTION_DESC functionDesc{};
        ThrowIfFailed(functionReflection->GetDesc(&functionDesc));

        for (UINT resourceIndex = 0; resourceIndex < functionDesc.BoundResources; ++resourceIndex)
        {
            D3D12_SHADER_INPUT_BIND_DESC inputBindDesc{};
            ThrowIfFailed(functionReflection->GetResourceBindingDesc(resourceIndex, &inputBindDesc));
            if (!IsUnorderedAccessViewType(inputBindDesc.Type))
            {
                continue;
            }

            const std::string name = GetBaseResourceName(inputBindDesc.Name);
            const std::string key = BuildResourceKey(name, inputBindDesc.BindPoint, inputBindDesc.Space);
            if (!visitedResources.insert(key).second)
            {
                continue;
            }

            UnorderedAccessViewMetadata resourceMetadata;
            resourceMetadata.Name = name;
            resourceMetadata.RegisterIndex = inputBindDesc.BindPoint;
            resourceMetadata.Space = inputBindDesc.Space;
            resourceMetadata.BindCount = inputBindDesc.BindCount;
            resourceMetadata.InputType = inputBindDesc.Type;
            resourceMetadata.Dimension = inputBindDesc.Dimension;
            result.push_back(resourceMetadata);
        }
    }

    return result;
}
//Modify End

//Modify Begin:2026-07-27 by Hui
std::vector<ShaderUtils::SamplerMetadata> ShaderUtils::GetSamplers(const Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& shaderReflection)
{
    D3D12_SHADER_DESC shaderDesc;
    ThrowIfFailed(shaderReflection->GetDesc(&shaderDesc));

    std::vector<ShaderUtils::SamplerMetadata> result;
    result.reserve(shaderDesc.BoundResources);

    for (UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex)
    {
        D3D12_SHADER_INPUT_BIND_DESC inputBindDesc;
        ThrowIfFailed(shaderReflection->GetResourceBindingDesc(resourceIndex, &inputBindDesc));
        if (!IsSamplerType(inputBindDesc.Type))
        {
            continue;
        }

        SamplerMetadata samplerMetadata;
        samplerMetadata.Name = GetBaseResourceName(inputBindDesc.Name);
        samplerMetadata.RegisterIndex = inputBindDesc.BindPoint;
        samplerMetadata.Space = inputBindDesc.Space;
        samplerMetadata.BindCount = inputBindDesc.BindCount;
        result.push_back(std::move(samplerMetadata));
    }

    return result;
}

std::vector<ShaderUtils::SamplerMetadata> ShaderUtils::GetSamplers(const Microsoft::WRL::ComPtr<ID3D12LibraryReflection>& libraryReflection)
{
    D3D12_LIBRARY_DESC libraryDesc{};
    ThrowIfFailed(libraryReflection->GetDesc(&libraryDesc));

    std::vector<ShaderUtils::SamplerMetadata> result;
    std::unordered_set<std::string> visitedResources;

    for (UINT functionIndex = 0; functionIndex < libraryDesc.FunctionCount; ++functionIndex)
    {
        ID3D12FunctionReflection* functionReflection = libraryReflection->GetFunctionByIndex(functionIndex);
        if (functionReflection == nullptr)
        {
            continue;
        }

        D3D12_FUNCTION_DESC functionDesc{};
        ThrowIfFailed(functionReflection->GetDesc(&functionDesc));

        for (UINT resourceIndex = 0; resourceIndex < functionDesc.BoundResources; ++resourceIndex)
        {
            D3D12_SHADER_INPUT_BIND_DESC inputBindDesc{};
            ThrowIfFailed(functionReflection->GetResourceBindingDesc(resourceIndex, &inputBindDesc));
            if (!IsSamplerType(inputBindDesc.Type))
            {
                continue;
            }

            const std::string name = GetBaseResourceName(inputBindDesc.Name);
            const std::string key = BuildResourceKey(name, inputBindDesc.BindPoint, inputBindDesc.Space);
            if (!visitedResources.insert(key).second)
            {
                continue;
            }

            SamplerMetadata samplerMetadata;
            samplerMetadata.Name = name;
            samplerMetadata.RegisterIndex = inputBindDesc.BindPoint;
            samplerMetadata.Space = inputBindDesc.Space;
            samplerMetadata.BindCount = inputBindDesc.BindCount;
            result.push_back(std::move(samplerMetadata));
        }
    }

    return result;
}
//Modify End
