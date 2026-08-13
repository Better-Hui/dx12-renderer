//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

#include <Framework/Rendering/Pipeline/ShaderCompiler.h>

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace
{
    constexpr uint64_t FnvOffsetBasis = 14695981039346656037ull;
    constexpr uint64_t FnvPrime = 1099511628211ull;

    template <typename T>
    void HashCombine(uint64_t& seed, const T& value)
    {
        const std::string text = [&]()
        {
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }();
        for (const unsigned char character : text)
        {
            seed ^= character;
            seed *= FnvPrime;
        }
        seed ^= 0xffu;
        seed *= FnvPrime;
    }

    void HashBytes(uint64_t& seed, const void* data, const size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t byteIndex = 0; byteIndex < size; ++byteIndex)
        {
            seed ^= bytes[byteIndex];
            seed *= FnvPrime;
        }
        seed ^= 0xffu;
        seed *= FnvPrime;
    }

    void HashWideString(uint64_t& seed, const std::wstring& value)
    {
        for (const wchar_t character : value)
        {
            const uint32_t codePoint = static_cast<uint32_t>(character);
            HashBytes(seed, &codePoint, sizeof(codePoint));
        }
        seed ^= 0xffu;
        seed *= FnvPrime;
    }

    std::vector<ShaderVariantDefine> SortedDefines(std::vector<ShaderVariantDefine> defines)
    {
        std::sort(
            defines.begin(),
            defines.end(),
            [](const ShaderVariantDefine& lhs, const ShaderVariantDefine& rhs)
            {
                if (lhs.Name == rhs.Name)
                {
                    return lhs.Value < rhs.Value;
                }
                return lhs.Name < rhs.Name;
            });
        return defines;
    }

    std::filesystem::path NormalizePath(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, error);
        if (error)
        {
            return path.lexically_normal();
        }

        const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(absolutePath, error);
        return error ? absolutePath.lexically_normal() : canonicalPath;
    }

    std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            throw std::runtime_error("Failed to open shader file: " + path.string());
        }

        const std::streamsize size = input.tellg();
        if (size <= 0)
        {
            throw std::runtime_error("Shader file is empty: " + path.string());
        }

        std::vector<uint8_t> data(static_cast<size_t>(size));
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char*>(data.data()), size);
        if (!input)
        {
            throw std::runtime_error("Failed to read shader file: " + path.string());
        }
        return data;
    }

    std::wstring GetEnvironmentValue(const wchar_t* name)
    {
        std::vector<wchar_t> buffer(32768, L'\0');
        const DWORD length = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return {};
        }
        return std::wstring(buffer.data(), length);
    }

    std::filesystem::path GetExecutableDirectory()
    {
        std::vector<wchar_t> buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
    }

    bool LooksLikeSourceRoot(const std::filesystem::path& path)
    {
        return std::filesystem::exists(path / "CMakeLists.txt") &&
            std::filesystem::is_directory(path / "Framework") &&
            std::filesystem::is_directory(path / "Demos");
    }

    std::filesystem::path ReadCMakeSourceRoot(const std::filesystem::path& buildDirectory)
    {
        const std::filesystem::path cachePath = buildDirectory / "CMakeCache.txt";
        std::ifstream input(cachePath);
        if (!input)
        {
            return {};
        }

        std::string line;
        constexpr std::string_view prefix = "CMAKE_HOME_DIRECTORY:INTERNAL=";
        while (std::getline(input, line))
        {
            if (line.rfind(prefix, 0) == 0)
            {
                return std::filesystem::path(line.substr(prefix.size()));
            }
        }
        return {};
    }

    std::filesystem::path SearchSourceRoot(std::filesystem::path startPath)
    {
        std::error_code error;
        startPath = std::filesystem::absolute(startPath, error);
        if (error)
        {
            return {};
        }
        if (!std::filesystem::is_directory(startPath, error))
        {
            startPath = startPath.parent_path();
        }

        for (std::filesystem::path current = startPath; !current.empty(); current = current.parent_path())
        {
            if (const std::filesystem::path cmakeRoot = ReadCMakeSourceRoot(current); !cmakeRoot.empty() && LooksLikeSourceRoot(cmakeRoot))
            {
                return NormalizePath(cmakeRoot);
            }
            if (LooksLikeSourceRoot(current))
            {
                return NormalizePath(current);
            }

            const std::filesystem::path parent = current.parent_path();
            if (parent == current)
            {
                break;
            }
        }
        return {};
    }

    ShaderVariantCompilationMode ParseCompilationMode(const std::wstring& value)
    {
        std::wstring normalized = value;
        std::transform(
            normalized.begin(),
            normalized.end(),
            normalized.begin(),
            [](const wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });

        if (normalized == L"off" || normalized == L"0" || normalized == L"disabled")
        {
            return ShaderVariantCompilationMode::Disabled;
        }
        if (normalized == L"force" || normalized == L"2")
        {
            return ShaderVariantCompilationMode::Force;
        }
        return ShaderVariantCompilationMode::Auto;
    }

    void AddUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return;
        }

        const std::filesystem::path normalized = NormalizePath(path);
        if (std::find(paths.begin(), paths.end(), normalized) == paths.end())
        {
            paths.push_back(normalized);
        }
    }

    struct DependencyScanResult
    {
        std::vector<std::filesystem::path> Files;
        std::vector<std::string> MissingIncludes;
    };

    std::filesystem::path ResolveIncludePath(
        const std::filesystem::path& includingFile,
        const std::string& includeName,
        const char delimiter,
        const std::vector<std::filesystem::path>& includeDirectories)
    {
        std::vector<std::filesystem::path> searchDirectories;
        if (delimiter == '"')
        {
            searchDirectories.push_back(includingFile.parent_path());
        }
        searchDirectories.insert(searchDirectories.end(), includeDirectories.begin(), includeDirectories.end());

        for (const std::filesystem::path& directory : searchDirectories)
        {
            const std::filesystem::path candidate = directory / std::filesystem::path(includeName);
            if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate))
            {
                return NormalizePath(candidate);
            }
        }
        return {};
    }

    void ScanDependenciesRecursive(
        const std::filesystem::path& file,
        const std::vector<std::filesystem::path>& includeDirectories,
        DependencyScanResult& result,
        std::unordered_set<std::wstring>& visitedFiles)
    {
        const std::filesystem::path normalizedFile = NormalizePath(file);
        if (!visitedFiles.insert(normalizedFile.wstring()).second)
        {
            return;
        }

        const std::vector<uint8_t> bytes = ReadBinaryFile(normalizedFile);
        result.Files.push_back(normalizedFile);
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
        {
            const size_t hashPosition = line.find('#');
            if (hashPosition == std::string::npos)
            {
                continue;
            }

            size_t keywordPosition = hashPosition + 1;
            while (keywordPosition < line.size() && std::isspace(static_cast<unsigned char>(line[keywordPosition])))
            {
                ++keywordPosition;
            }
            if (line.compare(keywordPosition, 7, "include") != 0)
            {
                continue;
            }

            const size_t delimiterPosition = line.find_first_of("\"<", keywordPosition + 7);
            if (delimiterPosition == std::string::npos || delimiterPosition + 2 >= line.size())
            {
                continue;
            }

            const char delimiter = line[delimiterPosition];
            const char closingDelimiter = delimiter == '"' ? '"' : '>';
            const size_t closingPosition = line.find(closingDelimiter, delimiterPosition + 1);
            if (closingPosition == std::string::npos)
            {
                continue;
            }

            const std::string includeName = line.substr(delimiterPosition + 1, closingPosition - delimiterPosition - 1);
            const std::filesystem::path includePath = ResolveIncludePath(
                normalizedFile,
                includeName,
                delimiter,
                includeDirectories);
            if (includePath.empty())
            {
                result.MissingIncludes.push_back(includeName);
                continue;
            }

            ScanDependenciesRecursive(includePath, includeDirectories, result, visitedFiles);
        }
    }

    std::string MakeCompilerIdentity()
    {
        const std::filesystem::path compilerPath = GetExecutableDirectory() / "dxcompiler.dll";
        std::error_code error;
        const uintmax_t fileSize = std::filesystem::file_size(compilerPath, error);
        const auto writeTime = std::filesystem::last_write_time(compilerPath, error);
        std::ostringstream identity;
        identity << "dxcapi-v1:" << fileSize << ':' << writeTime.time_since_epoch().count();
        return identity.str();
    }

    std::string MakeFingerprint(
        const ShaderVariantDesc& desc,
        const std::filesystem::path& sourcePath,
        const std::vector<std::filesystem::path>& includeDirectories,
        const DependencyScanResult& dependencies)
    {
        uint64_t hash = FnvOffsetBasis;
        HashCombine(hash, "ShaderVariantCacheFormat=1");
        HashCombine(hash, MakeCompilerIdentity());
        HashWideString(hash, sourcePath.wstring());
        HashCombine(hash, desc.EntryPoint);
        HashCombine(hash, desc.TargetProfile);

        for (const ShaderVariantDefine& define : SortedDefines(desc.Defines))
        {
            HashCombine(hash, define.Name);
            HashCombine(hash, define.Value);
        }
        for (const std::filesystem::path& includeDirectory : includeDirectories)
        {
            HashWideString(hash, includeDirectory.wstring());
        }
        for (const std::wstring& argument : desc.CompilerArguments)
        {
            HashWideString(hash, argument);
        }
        for (const std::filesystem::path& dependency : dependencies.Files)
        {
            HashWideString(hash, dependency.wstring());
            const std::vector<uint8_t> bytes = ReadBinaryFile(dependency);
            HashBytes(hash, bytes.data(), bytes.size());
        }
        for (const std::string& missingInclude : dependencies.MissingIncludes)
        {
            HashCombine(hash, "missing:" + missingInclude);
        }

        std::ostringstream fingerprint;
        fingerprint << std::hex << std::setw(16) << std::setfill('0') << hash;
        return fingerprint.str();
    }

    std::wstring MakeCacheStem(const std::wstring& compiledFileName)
    {
        std::wstring stem = std::filesystem::path(compiledFileName).stem().wstring();
        for (wchar_t& character : stem)
        {
            if (!std::iswalnum(character) && character != L'_' && character != L'-')
            {
                character = L'_';
            }
        }
        return stem.empty() ? L"shader" : stem;
    }

    void Trace(const std::wstring& message)
    {
        const std::wstring line = L"[ShaderVariant] " + message + L"\n";
        OutputDebugStringW(line.c_str());
        std::wcerr << line;
    }

    void WriteCacheFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            return;
        }

        const std::filesystem::path temporaryPath = path.wstring() + L".tmp";
        {
            std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                return;
            }
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!output)
            {
                return;
            }
        }

        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporaryPath, path, error);
        if (error)
        {
            std::filesystem::remove(temporaryPath, error);
        }
    }

    void WriteMetadataFile(
        const std::filesystem::path& path,
        const ShaderVariantDesc& desc,
        const std::filesystem::path& sourcePath,
        const std::string& fingerprint,
        const std::vector<std::filesystem::path>& dependencies)
    {
        std::ofstream output(path, std::ios::trunc);
        if (!output)
        {
            return;
        }

        output << "fingerprint=" << fingerprint << '\n';
        output << "source=" << sourcePath.string() << '\n';
        output << "entry=" << desc.EntryPoint << '\n';
        output << "target=" << desc.TargetProfile << '\n';
        for (const ShaderVariantDefine& define : SortedDefines(desc.Defines))
        {
            output << "define=" << define.Name << '=' << define.Value << '\n';
        }
        for (const std::filesystem::path& dependency : dependencies)
        {
            output << "dependency=" << dependency.string() << '\n';
        }
    }
}

bool ShaderVariantKey::operator==(const ShaderVariantKey& other) const
{
    return CompiledFileName == other.CompiledFileName &&
        SourceFileName == other.SourceFileName &&
        EntryPoint == other.EntryPoint &&
        TargetProfile == other.TargetProfile &&
        Defines == other.Defines &&
        IncludeDirectories == other.IncludeDirectories &&
        CompilerArguments == other.CompilerArguments;
}

size_t ShaderVariantKeyHasher::operator()(const ShaderVariantKey& key) const
{
    uint64_t seed = FnvOffsetBasis;
    HashWideString(seed, key.CompiledFileName);
    HashWideString(seed, key.SourceFileName);
    HashCombine(seed, key.EntryPoint);
    HashCombine(seed, key.TargetProfile);
    for (const ShaderVariantDefine& define : key.Defines)
    {
        HashCombine(seed, define.Name);
        HashCombine(seed, define.Value);
    }
    for (const std::filesystem::path& includeDirectory : key.IncludeDirectories)
    {
        HashWideString(seed, includeDirectory.wstring());
    }
    for (const std::wstring& argument : key.CompilerArguments)
    {
        HashWideString(seed, argument);
    }
    return static_cast<size_t>(seed);
}

ShaderVariantManager::ShaderVariantManager()
    : ShaderVariantManager(CreateDefaultConfig())
{
}

ShaderVariantManager::ShaderVariantManager(ShaderVariantManagerConfig config)
    : m_Config(std::move(config))
{
}

ShaderVariantManager::~ShaderVariantManager() = default;

ShaderVariantManagerConfig ShaderVariantManager::CreateDefaultConfig()
{
    ShaderVariantManagerConfig config;
    const std::wstring sourceRootOverride = GetEnvironmentValue(L"DX12_RENDERER_SHADER_SOURCE_ROOT");
    config.SourceRoot = sourceRootOverride.empty()
        ? SearchSourceRoot(std::filesystem::current_path())
        : NormalizePath(sourceRootOverride);

    if (config.SourceRoot.empty())
    {
        config.SourceRoot = SearchSourceRoot(GetExecutableDirectory());
    }

    const std::wstring cacheRootOverride = GetEnvironmentValue(L"DX12_RENDERER_SHADER_CACHE_ROOT");
    config.CacheRoot = cacheRootOverride.empty()
        ? GetExecutableDirectory() / "Saved" / "ShaderCache"
        : NormalizePath(cacheRootOverride);

    if (!config.SourceRoot.empty())
    {
        AddUniquePath(config.IncludeDirectories, config.SourceRoot);
        AddUniquePath(config.IncludeDirectories, config.SourceRoot / "DXC" / "include");
        AddUniquePath(config.IncludeDirectories, config.SourceRoot / "Shaders");
        AddUniquePath(config.IncludeDirectories, config.SourceRoot / "Framework" / "shaders");
        AddUniquePath(config.IncludeDirectories, config.SourceRoot / "External" / "NRD" / "Shaders");
    }

    std::wstring compilationMode = GetEnvironmentValue(L"DX12_RENDERER_SHADER_COMPILE");
    if (compilationMode.empty())
    {
        compilationMode = GetEnvironmentValue(L"RAYTRACING_DEMO_SHADER_COMPILE");
    }
    config.CompilationMode = ParseCompilationMode(compilationMode);
    return config;
}

ShaderVariantKey ShaderVariantManager::CreateKey(const ShaderVariantDesc& desc)
{
    ShaderVariantKey key;
    key.CompiledFileName = desc.CompiledFileName;
    key.SourceFileName = desc.SourceFileName;
    key.EntryPoint = desc.EntryPoint;
    key.TargetProfile = desc.TargetProfile;
    key.Defines = SortedDefines(desc.Defines);
    key.IncludeDirectories = desc.IncludeDirectories;
    key.CompilerArguments = desc.CompilerArguments;
    return key;
}

std::filesystem::path ShaderVariantManager::ResolveSourcePath(const ShaderVariantDesc& desc) const
{
    if (desc.SourceFileName.empty())
    {
        return {};
    }

    const std::filesystem::path requestedPath(desc.SourceFileName);
    if (requestedPath.is_absolute() && std::filesystem::exists(requestedPath))
    {
        return NormalizePath(requestedPath);
    }

    if (!m_Config.SourceRoot.empty())
    {
        const std::filesystem::path sourcePath = m_Config.SourceRoot / requestedPath;
        if (std::filesystem::exists(sourcePath))
        {
            return NormalizePath(sourcePath);
        }
    }

    if (std::filesystem::exists(requestedPath))
    {
        return NormalizePath(requestedPath);
    }
    return {};
}

std::shared_ptr<ShaderBlob> ShaderVariantManager::GetOrCompile(const ShaderVariantDesc& desc)
{
    const ShaderVariantKey key = CreateKey(desc);
    if (const auto findResult = m_Cache.find(key); findResult != m_Cache.end())
    {
        return findResult->second;
    }

    const auto loadPackagedVariant = [&]()
    {
        if (desc.CompiledFileName.empty())
        {
            throw std::runtime_error("Shader variant has neither a source file nor a packaged shader file.");
        }
        return std::make_shared<ShaderBlob>(desc.CompiledFileName);
    };

    if (m_Config.CompilationMode == ShaderVariantCompilationMode::Disabled)
    {
        const auto shaderBlob = loadPackagedVariant();
        m_Cache.emplace(key, shaderBlob);
        return shaderBlob;
    }

    const std::filesystem::path sourcePath = ResolveSourcePath(desc);
    if (sourcePath.empty())
    {
        Trace(L"Source is unavailable; loading packaged shader " + desc.CompiledFileName);
        const auto shaderBlob = loadPackagedVariant();
        m_Cache.emplace(key, shaderBlob);
        return shaderBlob;
    }

    std::vector<std::filesystem::path> includeDirectories = m_Config.IncludeDirectories;
    for (const std::filesystem::path& includeDirectory : desc.IncludeDirectories)
    {
        AddUniquePath(includeDirectories, includeDirectory);
    }

    DependencyScanResult dependencies;
    std::unordered_set<std::wstring> visitedFiles;
    ScanDependenciesRecursive(sourcePath, includeDirectories, dependencies, visitedFiles);
    const std::string fingerprint = MakeFingerprint(desc, sourcePath, includeDirectories, dependencies);
    const std::filesystem::path cachePath = m_Config.CacheRoot /
        (MakeCacheStem(desc.CompiledFileName) + L"." + std::wstring(fingerprint.begin(), fingerprint.end()) + L".cso");

    if (m_Config.CompilationMode != ShaderVariantCompilationMode::Force && std::filesystem::exists(cachePath))
    {
        try
        {
            const std::vector<uint8_t> bytecode = ReadBinaryFile(cachePath);
            const auto shaderBlob = std::make_shared<ShaderBlob>(bytecode.data(), bytecode.size());
            m_Cache.emplace(key, shaderBlob);
            Trace(L"Cache hit: " + cachePath.wstring());
            return shaderBlob;
        }
        catch (const std::exception&)
        {
//Modify Begin:2026-07-30 by BestHui
            Trace(L"Shader variant cache is invalid; rebuilding " + cachePath.wstring());
//Modify End
            std::error_code error;
            std::filesystem::remove(cachePath, error);
        }
    }

    if (m_Compiler == nullptr)
    {
        m_Compiler = std::make_unique<ShaderCompiler>();
    }

    ShaderCompileRequest request;
    request.SourcePath = sourcePath;
    request.EntryPoint = desc.EntryPoint;
    request.TargetProfile = desc.TargetProfile;
    request.IncludeDirectories = includeDirectories;
    request.Arguments = m_Config.CompilerArguments;
    request.Arguments.insert(request.Arguments.end(), desc.CompilerArguments.begin(), desc.CompilerArguments.end());
    request.Defines.reserve(desc.Defines.size());
    for (const ShaderVariantDefine& define : SortedDefines(desc.Defines))
    {
        request.Defines.push_back({ define.Name, define.Value });
    }

    Trace(L"Compiling " + sourcePath.wstring() + L" -> " + cachePath.wstring());
    const std::vector<uint8_t> bytecode = m_Compiler->Compile(request);
    WriteCacheFile(cachePath, bytecode);
    WriteMetadataFile(cachePath.wstring() + L".meta", desc, sourcePath, fingerprint, dependencies.Files);

    const auto shaderBlob = std::make_shared<ShaderBlob>(bytecode.data(), bytecode.size());
    m_Cache.emplace(key, shaderBlob);
    return shaderBlob;
}

std::shared_ptr<ShaderBlob> ShaderVariantManager::LoadCompiledVariant(const ShaderVariantDesc& desc)
{
    return GetOrCompile(desc);
}

void ShaderVariantManager::Invalidate(const ShaderVariantKey& key)
{
    m_Cache.erase(key);
}

void ShaderVariantManager::Clear()
{
    m_Cache.clear();
}
//Modify End
