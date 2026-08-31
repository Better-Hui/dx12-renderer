//Modify Begin:2026-08-31 by Hui
#include <Framework/Diagnostics/RenderGraphAccessValidation.h>

#include <Framework/Core/FrameworkDeviceContext.h>

#include <string>
#include <utility>
#include <vector>

void FrameworkDiagnostics::ValidateActiveRenderGraphResourceAccess(
    FrameworkDeviceContext& deviceContext,
    ID3D12Resource* resourceIdentity,
    const DX12Diagnostics::DiagnosticResourceAccess access,
    const std::string_view accessKind,
    const std::optional<uint32_t> rootParameterIndex)
{
    if (resourceIdentity == nullptr || !deviceContext.HasDiagnosticTelemetrySink())
    {
        return;
    }

    DX12Diagnostics::DiagnosticRenderPassScope* scope =
        DX12Diagnostics::DiagnosticRenderPassScope::GetCurrent();
    if (scope == nullptr)
    {
        return;
    }

    const DX12Diagnostics::DiagnosticResourceAccessValidation validation =
        scope->ValidateAccess(resourceIdentity, access);
    if (validation.Declared && validation.AccessAllowed)
    {
        return;
    }

    const DX12Diagnostics::DiagnosticRenderPassScopeDesc& scopeDesc = scope->GetDesc();
    std::vector<DiagnosticTelemetryField> fields = {
        { "result", std::string("fail") },
        { "message", std::string("A shader or native D3D12 resource access is not declared by its RenderGraph pass.") },
        { "pass", scopeDesc.PassName },
        { "queue", scopeDesc.QueueName },
        { "access_kind", std::string(accessKind) },
        { "access_origin", std::string(rootParameterIndex.has_value() ? "descriptor" : "native_d3d12") },
        { "resource_identity", static_cast<uint64_t>(reinterpret_cast<uintptr_t>(resourceIdentity)) },
        { "declared", validation.Declared },
        { "access_allowed", validation.AccessAllowed },
        { "declared_resource_id", validation.LogicalResourceId },
        { "declared_resource_name", validation.LogicalResourceName },
    };
    if (rootParameterIndex.has_value())
    {
        fields.push_back({ "root_parameter", static_cast<uint64_t>(*rootParameterIndex) });
    }

    deviceContext.RecordDiagnosticTelemetry({
        .Category = "assertion",
        .Name = "render_graph_shader_access_declaration",
        .Severity = DiagnosticTelemetrySeverity::Error,
        .FrameIndex = scopeDesc.FrameIndex,
        .CorrelationId = scopeDesc.CorrelationId,
        .Fields = std::move(fields),
    });
}
//Modify End
