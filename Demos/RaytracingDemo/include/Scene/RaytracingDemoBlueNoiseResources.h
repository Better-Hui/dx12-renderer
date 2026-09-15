#pragma once

#include <memory>

class CommandList;
class Texture;

class RaytracingDemoBlueNoiseResources final
{
public:
    void Clear() noexcept;
    void Load(CommandList& commandList);

    bool IsLoaded() const noexcept;
    const std::shared_ptr<Texture>& GetScalarTexture() const noexcept;
    const std::shared_ptr<Texture>& GetVec2Texture() const noexcept;

private:
    std::shared_ptr<Texture> m_ScalarTexture;
    std::shared_ptr<Texture> m_Vec2Texture;
};
