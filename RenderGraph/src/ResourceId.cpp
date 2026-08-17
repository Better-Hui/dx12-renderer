#include "ResourceId.h"

#include <DX12Library/Helpers.h>

#include <map>
#include <mutex>
#include <vector>

using namespace RenderGraph;

namespace
{
//Modify Begin:2026-08-13 by Hui
    struct ResourceIdRegistry
    {
        std::mutex Mutex;
        std::vector<std::wstring> Names = { L"Null" };
        std::map<std::wstring, ResourceId> ExistingIds;
    };

    ResourceIdRegistry& GetResourceIdRegistry()
    {
        static ResourceIdRegistry registry;
        return registry;
    }
//Modify End
}

const ResourceId ResourceIds::GRAPH_OUTPUT = GetResourceId(L"RenderGraph-BuiltIn-GraphOutput");

ResourceId ResourceIds::GetResourceId(const wchar_t* name)
{
//Modify Begin:2026-08-13 by Hui
    Assert(name != nullptr && name[0] != L'\0', "Render graph resource name must not be empty.");
    ResourceIdRegistry& registry = GetResourceIdRegistry();
    std::lock_guard lock(registry.Mutex);
    std::wstring nameString = { name };
    if (const auto findResult = registry.ExistingIds.find(nameString); findResult != registry.ExistingIds.end())
    {
        return findResult->second;
    }

    const ResourceId id = static_cast<ResourceId>(registry.Names.size());
    registry.Names.push_back(nameString);
    registry.ExistingIds.emplace(std::move(nameString), id);
    return id;
//Modify End
}

std::wstring ResourceIds::GetResourceName(const ResourceId id)
{
//Modify Begin:2026-08-13 by Hui
    ResourceIdRegistry& registry = GetResourceIdRegistry();
    std::lock_guard lock(registry.Mutex);
    Assert(id < registry.Names.size(), "ID is invalid.");
    return registry.Names[id];
//Modify End
}
