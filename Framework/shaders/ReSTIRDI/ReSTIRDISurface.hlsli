#ifndef FRAMEWORK_RESTIR_DI_SURFACE_HLSLI
#define FRAMEWORK_RESTIR_DI_SURFACE_HLSLI

bool ReSTIRDIHaveSimilarMaterials(
    const SurfaceData receiver,
    const SurfaceData source,
    const float threshold)
{
    const float diffuseScale = max(0.05f, max(length(receiver.Diffuse), length(source.Diffuse)));
    const float specularScale = max(0.05f, max(length(receiver.Specular), length(source.Specular)));
    const float diffuseDifference = length(receiver.Diffuse - source.Diffuse) / diffuseScale;
    const float specularDifference = length(receiver.Specular - source.Specular) / specularScale;
    const float roughnessDifference = abs(receiver.Roughness - source.Roughness);
    const float metallicDifference = abs(receiver.Metallic - source.Metallic);
    const float occlusionDifference = abs(receiver.AmbientOcclusion - source.AmbientOcclusion);
    return max(max(diffuseDifference, specularDifference), max(roughnessDifference, max(metallicDifference, occlusionDifference))) <= threshold;
}

bool ReSTIRDIHaveCompatibleSurfaces(
    const SurfaceData receiver,
    const float receiverDepth,
    const SurfaceData source,
    const float sourceDepth,
    const float normalThreshold,
    const float depthThreshold,
    const float materialThreshold)
{
    return source.Valid &&
        ReSTIRDIIsSurfaceCompatible(
            receiver.NormalWs,
            receiverDepth,
            source.NormalWs,
            sourceDepth,
            normalThreshold,
            depthThreshold) &&
        ReSTIRDIHaveSimilarMaterials(receiver, source, materialThreshold);
}

#endif
