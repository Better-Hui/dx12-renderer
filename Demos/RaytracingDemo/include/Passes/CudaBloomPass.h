#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <d3d12.h>
#include <wrl.h>

class Texture;

class CudaBloomPass final
{
public:
    CudaBloomPass();
    ~CudaBloomPass();

    bool DrawImGui();
//Modify Begin:2026-07-28 by BestHui
    bool ExecuteInPlace(Texture& postProcessColor, uint32_t width, uint32_t height);
    void ReleaseInteropResource();
//Modify End
    void Shutdown();

    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    const std::string& GetStatus() const { return m_Status; }

private:
    struct CudaDriver;

    bool InitializeCuda();
//Modify Begin:2026-07-28 by BestHui
    bool EnsureD3D12InteropResource(Texture& postProcessColor, uint32_t width, uint32_t height);
//Modify End
    bool EnsureCudaPyramidBuffers(uint32_t width, uint32_t height, uint32_t levelCount);
    bool RunCudaBloom(uint32_t width, uint32_t height);

    bool m_Enabled = false;
    bool m_AvailabilityChecked = false;
    bool m_CudaAvailable = false;
    float m_Threshold = 0.55f;
    float m_SoftThreshold = 0.30f;
    float m_Intensity = 0.75f;
    int m_PyramidLevels = 5;
    std::string m_Status = "CUDA bloom is not initialized.";

    std::unique_ptr<CudaDriver> m_Cuda;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    ID3D12Resource* m_SourceInteropResource = nullptr;
};
