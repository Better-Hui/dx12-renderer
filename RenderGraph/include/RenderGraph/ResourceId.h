#pragma once

#include <cstdint>
#include <string>

namespace RenderGraph
{
    typedef uint32_t ResourceId;

    class ResourceIds
    {
    public:
        static ResourceId GetResourceId(const wchar_t* name);
        static std::wstring GetResourceName(ResourceId id);

        static const ResourceId GRAPH_OUTPUT;
    };
}
