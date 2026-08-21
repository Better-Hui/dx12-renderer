# 后续功能候选清单

> 本文记录可能推进的技术方向，不代表功能承诺或固定排期。实现前仍需先完成架构、画质和性能评估。

- [ ] **间接光追**：在 `Framework` 中形成独立的间接光照模块，明确输入 `GBuffer`、RTAS、材质、光源与环境数据，输出 `IndirectLighting`。目标是支持可配置的 bounce、漫反射与镜面传输、环境与自发光贡献，以及可独立接入的降噪契约；`RaytracingDemo` 仅负责选择技术并连接 `RenderGraph` 资源。
- [ ] **FBX 场景支持**：在 `Framework` 中增加可独立复用的 FBX 场景导入路径，明确网格、节点变换、PBR 材质、纹理和光源的导入契约；`RaytracingDemo` 仅负责选择源场景并将导入结果交给既有 `Scene` / `RaytracingDemoSceneResources` 上传流程。实现前先确定 Assimp、FBX SDK 或自有解析器的依赖、许可、坐标系与切线空间转换策略。
- [ ] **自发光 Mesh 光源**：完善 `SurfaceEmitter` 的网格发光体采样、动态更新和 Direct/Indirect Lighting 共享 light-domain 路径。
- [ ] **动态 RTAS 更新**：补充动态顶点几何的 BLAS refit 与资源退休策略，并用自动化场景验证变换和几何更新。
- [ ] **多 queue 资源调度**：将连续 Async Compute pass 合并为 compute segment，补充 Copy queue pass 的实际 sample/批处理覆盖，并设计可验证的跨 queue transient aliasing。
- [ ] **Shader 变体工作流**：继续完善 Framework shader 的依赖追踪、变体声明和构建缓存，避免不受控的排列组合膨胀。
- [ ] **Slang Shader 工具链**：在保留 D3D12、Shader Model 6.8、DXIL 输出和 HLSL/Slang 文件共存的前提下，将 Slang 编译与反射接入 Framework 的 `ShaderCompiler`、`ShaderVariantManager` 和 `PipelineLayout`，再分批迁移 Framework shader。该目标只表示引入 Slang shader language/toolchain，不等同于立即使用 `slang-gfx` 或改造成多后端 RHI。
- [ ] **实验性 DLSS 路径验证**：在支持硬件上分别完成 DLSS SR/DLAA、Ray Reconstruction 与 Frame Generation 的画质、稳定性和性能验证。
