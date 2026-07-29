# Raytracing Sample API Guide

本文档描述 `RaytracingDemo` 作为 sample 项目的职责、推荐 API 使用方式，以及当前底层封装和 NRI 风格接口之间的差距。

`RaytracingDemo` 不应该只是一个能跑的 demo。它后续要作为编写新 sample 的参考，因此代码必须清楚展示：

- 如何通过 RenderGraph 组织 pass。
- 如何使用统一的 `CommandContext` 绑定 raster / compute / DXR pipeline。
- 如何通过名字绑定 shader 资源，而不是在 demo 层硬写 root parameter / register slot。
- 如何同时演示 inline ray tracing 和 DXR shader-table 两条后端。
- 如何把 CUDA interop 接入渲染流程。

## Sample 设计原则

### Demo 层职责

Demo 层负责表达渲染算法和 sample 结构：

- 场景资源：mesh、material、texture、geometry buffer、light buffer。
- RenderGraph pass 拓扑：base resources、direct lighting、indirect lighting、composite、denoise、skybox、post process、overlay。
- UI：只修改 sample 参数，不直接操作底层 descriptor heap / root signature。
- 后端选择：inline ray tracing 与 DXR shader-table 都应该消费同一套场景资源和 binding API。

Demo 层不应该直接管理：

- `ID3D12DescriptorHeap`
- root parameter index
- native descriptor table
- native root signature layout
- DXR descriptor heap bind

看到这些类型出现在 sample pass 中，通常说明封装边界有问题。

### 底层职责

底层负责把 API 使用者的意图翻译成 D3D12 命令：

- `PipelineLayout`：由 shader reflection 和显式 override 生成 root signature / descriptor layout。
- `PipelineDescriptorPool`：分配 descriptor set，记录容量和逻辑 offset。
- `PipelineDescriptorSet`：保存按名字绑定的 CBV / SRV / UAV / acceleration structure。
- `CommandContext`：唯一的命令录制入口，负责 bind pipeline、bind descriptor set、draw、dispatch、dispatch rays。
- `DynamicDescriptorHeap`：当前实际 GPU-visible descriptor table 提交点。

## 推荐 Command API

sample pass 中推荐写成：

```cpp
CommandContext commandContext(commandList);

commandContext.BindPipeline(shader);
commandContext.BindDescriptorSet(shader.GetDescriptorSet(), PipelineBindPoint::Compute);
commandContext.Dispatch(groupX, groupY, groupZ);
```

raster pass 推荐写成：

```cpp
CommandContext commandContext(commandList);

commandContext.BindPipeline(gBufferShader);
commandContext.BindDescriptorSet(gBufferShader.GetDescriptorSet(), PipelineBindPoint::Graphics);
mesh.Draw(commandList);
```

DXR shader-table pass 推荐写成：

```cpp
CommandContext commandContext(commandList);

commandContext.BindRayTracingDescriptorSet(bindingSet);
commandContext.DispatchRays(dispatchDesc);
```

不推荐 sample 直接调用：

```cpp
commandContext.SetDescriptorPool(...);
shader.ApplyBindings(...);
shader.StageDefaultDescriptorTables(...);
commandList.StageDynamicDescriptors(...);
```

这些接口属于迁移期兼容路径，后续新 sample 不应该继续依赖。

## 字符串绑定资源

shader 资源绑定应通过 shader 中的变量名完成：

```cpp
shader.SetConstantBuffer(commandList, "CameraConstants", camera);
commandList.SetTexture(shader, "GBufferNormal", ShaderResourceView(gBufferNormal));
shader.SetUnorderedAccessView(commandList, "Output", UnorderedAccessView(output));
```

语义是：

1. `Shader / ComputeShader / RayTracingBindingSet` 通过 reflection 找到变量名对应的 binding。
2. 资源写入 `PipelineDescriptorSet`。
3. pass 调用 `CommandContext::BindDescriptorSet()`。
4. `CommandContext` 统一提交 descriptor table。

sample 不应该硬编码：

```cpp
commandList.SetShaderResourceView(rootParameterIndex, slot, resource);
```

除非是在迁移旧 demo 或底层内部实现。

## Raster Pipeline

当前 raster pipeline 的推荐路径：

```cpp
Shader shader(vertexShaderBlob, pixelShaderBlob, [](RasterPipelineStateBuilder& builder)
{
    builder
        .WithRenderTargetFormat(...)
        .WithDepthFormat(...)
        .WithCullMode(...)
        .WithDepthEnabled(true);
});
```

运行时：

```cpp
shader.SetConstantBuffer(commandList, "PipelineCBuffer", constants);
commandList.SetTexture(shader, "BaseColor", ShaderResourceView(texture));

CommandContext commandContext(commandList);
commandContext.BindPipeline(shader);
commandContext.BindDescriptorSet(shader.GetDescriptorSet(), PipelineBindPoint::Graphics);
mesh.Draw(commandList);
```

## Compute Pipeline

compute pipeline 应该和 raster 保持相似形态：

```cpp
ComputeShader shader(
    ShaderBlob(shaderBytecode, shaderBytecodeSize),
    ComputePipelineDescBuilder::ReflectedDefault(shaderBlob).Build());
```

运行时：

```cpp
shader.SetConstantBuffer(commandList, "Constants", constants);
commandList.SetTexture(shader, "Input", ShaderResourceView(input));
shader.SetUnorderedAccessView(commandList, "Output", UnorderedAccessView(output));

CommandContext commandContext(commandList);
commandContext.BindPipeline(shader);
commandContext.BindDescriptorSet(shader.GetDescriptorSet(), PipelineBindPoint::Compute);
commandContext.Dispatch(groupX, groupY, 1);
```

inline ray tracing 属于 compute shader 能力，不需要 shader table。它需要：

- acceleration structure SRV。
- scene geometry/material/light buffers。
- output UAV。
- shader model / DXR feature 支持。

## DXR Shader-Table Pipeline

DXR shader-table 后端使用：

- `RayTracingShader`
- `RayTracingBindingSet`
- `RayTracingDispatchTables`
- `CommandContext::BindRayTracingDescriptorSet()`
- `CommandContext::DispatchRays()`

推荐 sample 形态：

```cpp
RayTracingBindingSet& bindingSet = pipelines.GetDirectRayTracingBindingSet();

bindingSet.SetConstantBufferData("CameraConstants", &camera, sizeof(camera));
bindingSet.SetAccelerationStructure("Scene", accelerationStructure);
bindingSet.SetTextureArray("Textures", sceneTextures);
bindingSet.SetStructuredBuffer("Materials", materialBuffer);
bindingSet.SetOutputTexture("DirectLighting", directLighting);

CommandContext commandContext(commandList);
commandContext.BindRayTracingDescriptorSet(bindingSet);
commandContext.DispatchRays(dispatchDesc);
```

DXR 的 shader table 是 DXR 特有结构，负责 raygen / miss / hit group record。它不应该反向决定 demo 的资源组织方式。

## RenderGraph Pass 组织

RaytracingDemo 当前主流程：

```text
BaseResources
-> DirectLighting
-> IndirectLighting
-> LightingComposite
-> optional Denoise
-> Skybox
-> optional CUDA Bloom external pass
-> DisplayComposite
-> Overlay
-> Present
```

新增 sample 时建议：

1. 先声明资源。
2. 再声明 pass 的输入输出。
3. pass 内只拿 `RenderContext` 里已经声明好的资源。
4. pass 内通过 `CommandContext` 提交命令。
5. UI pass 和 debug overlay 放在 post process 之后，避免被 bloom / denoiser 处理。

## CUDA Interop

CUDA 后处理当前以 external pass 接入 RenderGraph。sample 层只应该调用封装后的 CUDA pass，例如：

```cpp
demo.m_CudaBloom.ExecuteInPlace(texture, width, height, d3d12Queue);
```

CUDA interop 的关键点：

- D3D12 texture 需要支持 shared handle。
- CUDA import shared D3D12 resource。
- D3D12 与 CUDA 通过 external semaphore / shared fence 同步。
- CUDA pass 不应该把 ImGui/overlay 作为输入。
- 后处理输入应该是 scene/postprocess color，而不是 denoiser history。

## 与 NRI 的差距

当前已经接近 NRI 的部分：

- 有 `CommandContext`，形态接近 `CommandBuffer` command API。
- 有 `PipelineLayout`，可从 reflection 生成 descriptor layout。
- 有 `PipelineDescriptorPool / PipelineDescriptorSet`，资源绑定开始向 set 模型靠拢。
- raster / compute / DXR 在 RaytracingDemo 中基本都走 `BindPipeline + BindDescriptorSet + Draw/Dispatch/DispatchRays`。
- 支持 external D3D12 resource / Unity D3D12 device 方向的封装雏形。

仍然缺失或不完整：

- `DescriptorPool` 还不是完整 NRI 式 GPU descriptor set 生命周期模型。
- 当前 GPU-visible descriptor table 仍通过 `DynamicDescriptorHeap` 临时 copy 提交。
- sampler reflection / sampler descriptor set 还不完整。
- root constants / root descriptors 还没有完全统一成 public command API。
- pipeline cache key 还没有覆盖所有状态：shader variant、shader model、render target format、DXR payload/attribute/recursion/hit group/capacity。
- resource state 仍主要依赖运行时 `ResourceStateTracker`，RenderGraph compile 阶段还没有生成完整 barrier plan。
- DXR 仍有 `RayTracingBindingSet / RayTracingDispatchTables` 专用链路，虽然功能上合理，但还可以继续减薄 binding set。
- Unity scene adapter 还没有开始，只复制了 Unity PluginAPI 并做了外部 device/resource 方向准备。

## 下一步封装计划

优先级：

1. 设计 `FrameDescriptorHeap` 或 `CommandContextDescriptorAllocator`：由 command context / frame resource 统一持有 shader-visible heap pages。
2. 让 `PipelineDescriptorSet` 保留 CPU backing 和逻辑 offset，由 command context 在提交期生成 GPU-visible table。
3. 补 sampler reflection / sampler descriptor heap。
4. 统一 root constants / root descriptors API，避免 shader 对象自己提交底层命令。
5. 收敛 DXR：`RayTracingBindingSet` 只保存名字绑定资源，dispatch table 和 dispatch command 继续拆薄。
6. 补 pipeline cache key：raster / compute / DXR 使用统一 key/hash 体系。
7. 把 RaytracingDemo 继续写成 sample：每个 pass 都展示一种推荐 API，而不是隐藏在工具函数里。

