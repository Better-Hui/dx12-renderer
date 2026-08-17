#pragma once

//Modify Begin:2026-08-07 by Hui
#include "ResourceId.h"

#include <memory>
#include <span>

class CommandList;
class Texture;

namespace RenderGraph
{
    class ExternalFrameProcessor
    {
    public:
        virtual ~ExternalFrameProcessor() = default;

        [[nodiscard]] virtual std::span<const ResourceId> GetRequiredResourceIds() const = 0;
        virtual void Process(CommandList& commandList, const std::shared_ptr<Texture>& displayTexture) = 0;
        virtual void BeforePresent() { }
        virtual void AfterPresent() { }
    };
}
//Modify End
