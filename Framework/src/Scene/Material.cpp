#include <Framework/Scene/Material.h>
//Modify Begin:2026-07-29 by Hui
#include <Framework/Rendering/Pipeline/CommandContext.h>
//Modify End

static const ShaderUtils::ConstantBufferMetadata* FindMaterialConstantBuffer(const Shader::ShaderMetadata& metadata)
{
    return metadata.m_ConstantBuffers.empty() ? nullptr : &metadata.m_ConstantBuffers.front();
}

Material::Material(const std::shared_ptr<Shader>& shader)
    : m_Shader(shader)
{
    const auto vsCbuffer = FindMaterialConstantBuffer(shader->GetVertexShaderMetadata());
    const auto psCbuffer = FindMaterialConstantBuffer(shader->GetPixelShaderMetadata());

    size_t cbufferSize;

    if (vsCbuffer != nullptr && psCbuffer != nullptr)
    {
        if (vsCbuffer->Size != psCbuffer->Size)
        {
            throw std::exception("Vertex and Pixel shader material constant buffer sizes are inconsistent.");
        }

        cbufferSize = vsCbuffer->Size;
        m_Metadata = vsCbuffer;
    }
    else if (vsCbuffer != nullptr)
    {
        cbufferSize = vsCbuffer->Size;
        m_Metadata = vsCbuffer;
    }
    else if (psCbuffer != nullptr)
    {
        cbufferSize = psCbuffer->Size;
        m_Metadata = psCbuffer;
    }
    else
    {
        m_ConstantBuffer = nullptr;
        m_ConstantBufferSize = 0;
        return;
    }

//Modify Begin:2026-07-27 by Hui
    m_ConstantBufferName = m_Metadata->Name;
//Modify End
    m_ConstantBuffer.reset(new uint8_t[cbufferSize]);
    m_ConstantBufferSize = cbufferSize;
    memset(m_ConstantBuffer.get(), 0, m_ConstantBufferSize);

    for (const auto& variable : m_Metadata->Variables)
    {
        if (variable.DefaultValue == nullptr)
        {
            continue;
        }

        memcpy(m_ConstantBuffer.get() + variable.Offset, variable.DefaultValue.get(), variable.Size);
    }
}

Material::Material(const Material& materialPreset)
    : m_Shader(materialPreset.m_Shader)
    , m_Metadata(materialPreset.m_Metadata)
//Modify Begin:2026-07-27 by Hui
    , m_ConstantBufferName(materialPreset.m_ConstantBufferName)
//Modify End
    , m_ConstantBuffer()
    , m_ConstantBufferSize(materialPreset.m_ConstantBufferSize)
    , m_ShaderResourceViews(materialPreset.m_ShaderResourceViews)
{
    m_ConstantBuffer.reset(new uint8_t[m_ConstantBufferSize]);
    memcpy(m_ConstantBuffer.get(), materialPreset.m_ConstantBuffer.get(), m_ConstantBufferSize);
}

void Material::SetAllVariables(size_t size, const void* data)
{
    if (size != m_ConstantBufferSize)
    {
        throw std::exception("Constant buffer size mismatch.");
    }

    memcpy(m_ConstantBuffer.get(), data, size);
}

void Material::SetVariable(const std::string& name, size_t size, const void* data, bool array, bool throwOnNotFound)
{
    if (m_ConstantBuffer == nullptr)
    {
        return;
    }

    for (const auto& variable : m_Metadata->Variables)
    {
        if (variable.Name != name) continue;

        if (array)
        {
            if (size > variable.Size)
            {
                throw std::exception("The value is too big for the destination array variable.");
            }
        }
        else
        {
            if (size != variable.Size)
            {
                throw std::exception("Variable size mismatch.");
            }
        }

        memcpy(m_ConstantBuffer.get() + variable.Offset, data, size);
        return;
    }

    if (throwOnNotFound)
    {
        throw std::exception("Variable not found.");
    }
}

void Material::SetShaderResourceView(const std::string& name, const ShaderResourceView& shaderResourceView)
{
    m_ShaderResourceViews.insert_or_assign(name, shaderResourceView);
    SetVariable<uint32_t>("has_" + name, 1u, false);
}

void Material::SetTexture(const std::string& name, const ShaderResourceView& shaderResourceView)
{
    SetShaderResourceView(name, shaderResourceView);
}

void Material::SetTexture(CommandList&, const std::string& name, const ShaderResourceView& shaderResourceView)
{
    SetTexture(name, shaderResourceView);
}

void Material::Bind(CommandList& commandList)
{
//Modify Begin:2026-07-29 by Hui
    CommandContext commandContext(commandList);
    commandContext.BindPipeline(*m_Shader);
    UploadUniforms(commandList);
    commandContext.BindDescriptorSet(m_Shader->GetDescriptorSet());
//Modify End
}

void Material::UploadUniforms(CommandList& commandList)
{
    UploadConstantBuffer(commandList);
    UploadShaderResourceViews(commandList);
}

std::shared_ptr<Material> Material::Create(const std::shared_ptr<Shader>& shader)
{
    return std::make_shared<Material>(shader);
}

std::shared_ptr<Material> Material::Create(const Material& materialPreset)
{
    return std::make_shared<Material>(materialPreset);
}

void Material::UploadConstantBuffer(CommandList& commandList)
{
    if (m_ConstantBuffer == nullptr)
    {
        return;
    }

//Modify Begin:2026-07-27 by Hui
    m_Shader->SetConstantBuffer(commandList, m_ConstantBufferName, m_ConstantBufferSize, m_ConstantBuffer.get());
//Modify End
}

void Material::UploadShaderResourceViews(CommandList& commandList)
{
    for (const auto& srvName : m_ShaderResourceViews)
    {
        const auto& name = srvName.first;
        const auto& shaderResourceView = srvName.second;

        m_Shader->SetShaderResourceView(commandList, name, shaderResourceView);
    }
}
