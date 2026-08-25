# 后续功能候选清单

> 本文只记录尚未完成的扩展或验收目标，不代表功能承诺或固定排期。实现前仍需先完成架构、画质和性能评估。

- [ ] **Diagnostics 深化**：机器可读 capture、具名 control/observation、确定性 automation、RenderGraph/queue/resource/descriptor telemetry、结构化 invariant、CPU/GPU timing、查询/diff/复现工具已经落地。真实 Direct → Copy → Async Compute → Direct 路径已验证 producer fence、GPU wait、state plan、batch 与资源 retirement；后续补通用 GPU texture/buffer readback 与 image assertion、device removal 的 DRED attachment、实际 shader access 与声明的通用闭环，以及后台写盘、压缩和 retention policy。详见 `Docs/FrameworkDiagnosticsPlan.zh-CN.md`。
- [ ] **间接光追扩展**：当前 sample 已有 Path Tracing 和 Framework `ReSTIRGIPass` 的 one-bounce 间接光路径；后续仍需把多 bounce、漫反射/镜面传输、环境与自发光贡献以及可独立接入的降噪契约收敛为更完整的 Framework light-domain 接口。
- [ ] **动态自发光 Mesh 光源**：当前已有 `SurfaceEmitter` 的几何级 triangle CDF 和实例数据上传；后续完善网格发光体的动态几何/材质更新，并让 Direct/Indirect Lighting 共用稳定的更新与采样契约。
- [ ] **跨 queue transient aliasing 收口**：Compiler 已能把兼容的连续 Async Compute/Copy pass 合并为同 queue non-direct batch，真实 Copy queue 路径与 Diagnostics 闭环已覆盖 Direct → Copy → Async Compute → Direct。Raster Bloom scratch 已恢复 transient aliasing，并在首次实际使用点编码 alias barrier 和 `COMMON` 初始状态；后续再设计可验证、可回收的跨 queue alias allocator。
- [ ] **Shader 变体工程化**：当前 demo-owned shader 已具备启动期依赖扫描、fingerprint 和缓存；后续继续覆盖 Framework 生成式 shader、项目级 variant manifest、后台编译和更完整的缓存失效策略，避免排列组合膨胀。
- [ ] **Slang Shader 工具链**：在保留 D3D12、Shader Model 6.8、DXIL 输出和 HLSL/Slang 文件共存的前提下，将 Slang 编译与反射接入 Framework 的 `ShaderCompiler`、`ShaderVariantManager` 和 `PipelineLayout`，再分批迁移 Framework shader。该目标只表示引入 Slang shader language/toolchain，不等同于立即使用 `slang-gfx` 或改造成多后端 RHI。
- [ ] **实验性 DLSS 路径验证**：在支持硬件上分别完成 DLSS SR/DLAA、Ray Reconstruction 与 Frame Generation 的画质、稳定性和性能验证。
