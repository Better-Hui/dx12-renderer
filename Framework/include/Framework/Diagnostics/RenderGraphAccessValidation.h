//Modify Begin:2026-08-31 by Hui
#pragma once

#include <DX12Library/DiagnosticRenderScope.h>

#include <cstdint>
#include <optional>
#include <string_view>

class FrameworkDeviceContext;

namespace FrameworkDiagnostics
{
    void ValidateActiveRenderGraphResourceAccess(
        FrameworkDeviceContext& deviceContext,
        ID3D12Resource* resourceIdentity,
        DX12Diagnostics::DiagnosticResourceAccess access,
        std::string_view accessKind,
        std::optional<uint32_t> rootParameterIndex = std::nullopt);
}
//Modify End
