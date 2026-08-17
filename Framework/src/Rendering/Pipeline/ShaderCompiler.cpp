//Modify Begin:2026-07-30 by Hui
#include <Framework/Rendering/Pipeline/ShaderCompiler.h>

#include <DX12Library/Helpers.h>

#include <Windows.h>
#include <dxcapi.h>
#include <wrl.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace
{
    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int requiredLength = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0);
        if (requiredLength <= 0)
        {
            throw std::runtime_error("Failed to convert a shader compiler argument from UTF-8 to UTF-16.");
        }

        std::wstring result(static_cast<size_t>(requiredLength), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                result.data(),
                requiredLength) != requiredLength)
        {
            throw std::runtime_error("Failed to convert a shader compiler argument from UTF-8 to UTF-16.");
        }

        return result;
    }

    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int requiredLength = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (requiredLength <= 0)
        {
            return "<unavailable path>";
        }

        std::string result(static_cast<size_t>(requiredLength), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            requiredLength,
            nullptr,
            nullptr);
        return result;
    }

    std::string ReadDiagnostics(const Microsoft::WRL::ComPtr<IDxcOperationResult>& operationResult)
    {
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> diagnostics;
        if (FAILED(operationResult->GetErrorBuffer(&diagnostics)) || diagnostics == nullptr || diagnostics->GetBufferSize() == 0)
        {
            return {};
        }

        return std::string(
            static_cast<const char*>(diagnostics->GetBufferPointer()),
            diagnostics->GetBufferSize());
    }

    std::runtime_error MakeCompileError(
        const std::filesystem::path& sourcePath,
        const HRESULT status,
        const std::string& diagnostics)
    {
        std::ostringstream message;
        message << "DXC failed to compile '" << WideToUtf8(sourcePath.wstring()) << "' (HRESULT=0x"
                << std::hex << std::uppercase << static_cast<uint32_t>(status) << ").";
        if (!diagnostics.empty())
        {
            message << '\n' << diagnostics;
        }
        return std::runtime_error(message.str());
    }
}

struct ShaderCompiler::Impl
{
    Microsoft::WRL::ComPtr<IDxcLibrary> Library;
    Microsoft::WRL::ComPtr<IDxcCompiler> Compiler;
};

ShaderCompiler::ShaderCompiler()
    : m_Impl(std::make_unique<Impl>())
{
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&m_Impl->Library)));
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_Impl->Compiler)));
}

ShaderCompiler::~ShaderCompiler() = default;

std::vector<uint8_t> ShaderCompiler::Compile(const ShaderCompileRequest& request) const
{
    if (request.SourcePath.empty())
    {
        throw std::invalid_argument("Shader compilation requires a source path.");
    }
    if (request.TargetProfile.empty())
    {
        throw std::invalid_argument("Shader compilation requires a target profile.");
    }

    uint32_t codePage = CP_UTF8;
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
    ThrowIfFailed(m_Impl->Library->CreateBlobFromFile(request.SourcePath.c_str(), &codePage, &sourceBlob));

    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
    ThrowIfFailed(m_Impl->Library->CreateIncludeHandler(&includeHandler));

    std::vector<std::wstring> arguments;
    arguments.reserve(4 + request.IncludeDirectories.size() * 2 + request.Arguments.size());
    arguments.emplace_back(L"-HV");
    arguments.emplace_back(L"2021");
    arguments.emplace_back(L"-WX");
    for (const std::filesystem::path& includeDirectory : request.IncludeDirectories)
    {
        arguments.emplace_back(L"-I");
        arguments.emplace_back(includeDirectory.wstring());
    }
    arguments.insert(arguments.end(), request.Arguments.begin(), request.Arguments.end());

    std::vector<LPCWSTR> argumentPointers;
    argumentPointers.reserve(arguments.size());
    for (const std::wstring& argument : arguments)
    {
        argumentPointers.push_back(argument.c_str());
    }

    std::vector<std::wstring> defineNames;
    std::vector<std::wstring> defineValues;
    defineNames.reserve(request.Defines.size());
    defineValues.reserve(request.Defines.size());
    for (const ShaderCompileDefine& define : request.Defines)
    {
        defineNames.push_back(Utf8ToWide(define.Name));
        defineValues.push_back(Utf8ToWide(define.Value));
    }

    std::vector<DxcDefine> defines;
    defines.reserve(request.Defines.size());
    for (size_t defineIndex = 0; defineIndex < request.Defines.size(); ++defineIndex)
    {
        defines.push_back({
            defineNames[defineIndex].c_str(),
            defineValues[defineIndex].empty() ? nullptr : defineValues[defineIndex].c_str(),
        });
    }

    const std::wstring entryPoint = Utf8ToWide(request.EntryPoint);
    const std::wstring targetProfile = Utf8ToWide(request.TargetProfile);

    Microsoft::WRL::ComPtr<IDxcOperationResult> operationResult;
    const HRESULT compileResult = m_Impl->Compiler->Compile(
        sourceBlob.Get(),
        request.SourcePath.c_str(),
        entryPoint.empty() ? nullptr : entryPoint.c_str(),
        targetProfile.c_str(),
        argumentPointers.empty() ? nullptr : argumentPointers.data(),
        static_cast<UINT32>(argumentPointers.size()),
        defines.empty() ? nullptr : defines.data(),
        static_cast<UINT32>(defines.size()),
        includeHandler.Get(),
        &operationResult);
    if (FAILED(compileResult) || operationResult == nullptr)
    {
        throw MakeCompileError(request.SourcePath, compileResult, {});
    }

    HRESULT compilationStatus = E_FAIL;
    ThrowIfFailed(operationResult->GetStatus(&compilationStatus));
    const std::string diagnostics = ReadDiagnostics(operationResult);
    if (FAILED(compilationStatus))
    {
        throw MakeCompileError(request.SourcePath, compilationStatus, diagnostics);
    }

    Microsoft::WRL::ComPtr<IDxcBlob> compiledBlob;
    ThrowIfFailed(operationResult->GetResult(&compiledBlob));
    if (compiledBlob == nullptr || compiledBlob->GetBufferSize() == 0)
    {
        throw std::runtime_error("DXC completed without producing shader bytecode.");
    }

    const auto* bytecode = static_cast<const uint8_t*>(compiledBlob->GetBufferPointer());
    return { bytecode, bytecode + compiledBlob->GetBufferSize() };
}
//Modify End
