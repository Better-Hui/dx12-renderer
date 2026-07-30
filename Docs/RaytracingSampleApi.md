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
- `CommandContextDescriptorAllocator`：命令录制期 descriptor table 提交器，缓存同一 `CommandContext` 内未变化的 table。
- `DynamicDescriptorHeap`：当前实际 GPU-visible descriptor table 提交点。

## 推荐 Command API

`CommandContext` 的 public API 只保留 sample/业务代码应该直接使用的命令入口：

- `BindPipeline(...)`
- `BindDescriptorSet(...)`
- `BindDescriptorSet(PipelineDescriptorSetBindDesc)`
- `Draw(...)`
- `Dispatch(...)`
- `DispatchRays(...)`
- resource transition / UAV barrier helper

以下内容属于内部迁移细节，不应该在 sample/业务层直接调用：

- root signature 绑定
- pipeline state 直接绑定
- descriptor pool 绑定
- descriptor table staging
- 单 root parameter binding

RaytracingDemo 当前不再保留 `Shader / ComputeShader::Bind / Unbind / ApplyBindings` 这类旧入口。旧 demo 如果还依赖旧封装，应停留在旧类型或自行迁移，不再反向污染新 sample API。

sample pass 中推荐写成：

```cpp
CommandContext commandContext(commandList);

commandContext.BindPipeline(shader);
commandContext.BindDescriptorSet(shader.GetDescriptorSet());
commandContext.Dispatch(groupX, groupY, groupZ);
```

raster pass 推荐写成：

```cpp
CommandContext commandContext(commandList);

commandContext.BindPipeline(gBufferShader);
commandContext.BindDescriptorSet(gBufferShader.GetDescriptorSet());
mesh.Draw(commandList);
```

DXR shader-table pass 推荐写成：

```cpp
CommandContext commandContext(commandList);

commandContext.BindPipeline(rayTracingShader);
commandContext.BindDescriptorSet(bindingSet);
commandContext.DispatchRays(RayTracingDispatchDesc{ "DirectLightingRayGen", width, height, 1 });
commandContext.InsertDescriptorSetOutputBarriers(bindingSet);
```

不推荐 sample 直接调用：

```cpp
commandContext.SetDescriptorPool(...);
commandContext.SetPipelineLayout(...);
shader.ApplyBindings(...);
shader.StageDefaultDescriptorTables(...);
commandList.StageDynamicDescriptors(...);
```

这些接口要么已经收进 private，要么属于迁移期兼容路径。后续新 sample 不应该继续依赖。

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

当前实现中，`PipelineDescriptorTableAllocation` 带有 revision。只要 descriptor set 中某个 table 的 CPU backing 被更新，revision 就会增加。`CommandContextDescriptorAllocator` 会用 CPU handle、descriptor count、revision 判断当前 `CommandContext` 内是否需要重新 stage descriptor table。

注意：这只是减少重复绑定的过渡优化，不等于最终 NRI 式 persistent GPU descriptor set。最终目标仍然是由 command context / frame resource 统一管理 shader-visible descriptor heap pages。

数组 SRV 推荐走 range update，而不是业务层逐个 descriptor 写：

```cpp
descriptorSet.SetShaderResourceViews("Textures", textureSrvs);
```

`RayTracingBindingSet::SetTextureArray()` 内部已经使用这个路径。它接近 NRI 的 `UpdateDescriptorRangeDesc` 思路：资源数组作为一个 descriptor range 更新，descriptor table revision 只增加一次。

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
commandContext.BindDescriptorSet(shader.GetDescriptorSet());
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
commandContext.BindDescriptorSet(shader.GetDescriptorSet());
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
- `CommandContext::BindPipeline(const RayTracingShader&)`
- `CommandContext::BindDescriptorSet(const RayTracingBindingSet&)`
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
commandContext.BindPipeline(rayTracingShader);
commandContext.BindDescriptorSet(bindingSet);
commandContext.DispatchRays(RayTracingDispatchDesc{ "DirectLightingRayGen", width, height, 1 });
commandContext.InsertDescriptorSetOutputBarriers(bindingSet);
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

## Unity 场景解析

Unity 场景解析属于 `Framework` 上层工具，不属于 `RaytracingDemo` 的临时代码：

- `Framework/include/Framework/UnitySceneParser.h`
- `Framework/src/UnitySceneParser.cpp`
- `Framework/tools/UnitySceneDump.cpp`

当前解析范围：

- `GameObject`
- `Transform`，包含 local / world transform。
- `Camera`
- `Light`
- `MeshFilter`
- `MeshRenderer` 的 material 引用。
- `.mat` 材质资产名和 shader 引用。

Unity 是 Y-up 世界空间。解析器保留 Unity 原始坐标，不在工具层强制转换坐标系。后续如果 RaytracingDemo 或 Unity 插件要导入 Unity 场景，应在 scene adapter 层显式决定坐标转换策略。

工具验证示例：

```powershell
UnitySceneDump.exe "C:\Program Files\Unity\MDR\ModernDeferredRenderer\project\ModernDeferredRenderer\Assets\Scenes\CornellBox.unity"
```

下一步扩展方向：

- Prefab / nested prefab 解析。
- SkinnedMeshRenderer / LODGroup。
- Unity asset database 缓存，避免每次递归扫描 `.meta`。
- 文件监听或 Unity 插件推送，实现动态场景更新。

## 与 NRI 的差距

当前已经接近 NRI 的部分：

- 有 `CommandContext`，形态接近 `CommandBuffer` command API。
- 有 `PipelineLayout`，可从 reflection 生成 descriptor layout。
- 有 `PipelineDescriptorPool / PipelineDescriptorSet`，资源绑定开始向 set 模型靠拢。
- 有 `CommandContextDescriptorAllocator`，descriptor table 提交边界已经从 `CommandContext.cpp` 局部 helper 抽成独立对象。
- raster / compute / DXR 在 RaytracingDemo 中基本都走 `BindPipeline + BindDescriptorSet + Draw/Dispatch/DispatchRays`。
- `CommandContext` 的 root signature、PSO、descriptor staging、descriptor pool 绑定入口已经收进 private。
- `Shader / ComputeShader` 的 `Bind / Unbind / ApplyBindings` 已从新封装删除；RaytracingDemo 不再调用这些入口。
- DXR sample 不再手动构造 `D3D12_DISPATCH_RAYS_DESC`；推荐路径是 `BindPipeline(rayTracingShader)`、`BindDescriptorSet(bindingSet)`、`DispatchRays(RayTracingDispatchDesc)`。`RayTracingBindingSet` 只表达资源绑定，不再作为 dispatch 入口。
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
