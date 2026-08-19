//Modify Begin:2026-08-06 by Hui
#include <Framework/Scene/Scene.h>

#include <stdexcept>

using namespace DirectX;

bool SceneTextureBinding::IsValid() const
{
    return !AssetPath.empty();
}

void Scene::Clear()
{
    m_SourcePath.clear();
    m_ProjectRoot.clear();
    m_AssetsRoot.clear();
    m_Skybox = {};
    m_LightGroupSettings = {};
    m_Camera = {};
    m_Objects.clear();
    m_Materials.clear();
    m_DirectionalLights.clear();
    m_PointLights.clear();
    m_AreaLights.clear();
}

void Scene::SetSourcePaths(
    std::filesystem::path sourcePath,
    std::filesystem::path projectRoot,
    std::filesystem::path assetsRoot)
{
    m_SourcePath = std::move(sourcePath);
    m_ProjectRoot = std::move(projectRoot);
    m_AssetsRoot = std::move(assetsRoot);
}

void Scene::SetCamera(const SceneCamera& camera)
{
    m_Camera = camera;
    if (m_Camera.RuntimeCamera == nullptr)
    {
        m_Camera.RuntimeCamera = std::make_shared<Camera>();
    }
}

void Scene::SetSkybox(const SceneSkybox& skybox)
{
    m_Skybox = skybox;
}

void Scene::SetLightGroupSettings(const SceneLightGroupSettings& settings)
{
    m_LightGroupSettings = settings;
}

uint32_t Scene::AddMaterial(SceneMaterial material)
{
    const uint32_t index = static_cast<uint32_t>(m_Materials.size());
    m_Materials.push_back(std::move(material));
    return index;
}

void Scene::AddObject(SceneObject object)
{
    m_Objects.push_back(std::move(object));
}

void Scene::AddDirectionalLight(const DirectionalLight& light)
{
    m_DirectionalLights.push_back(light);
}

void Scene::AddPointLight(const PointLight& light)
{
    m_PointLights.push_back(light);
}

void Scene::AddAreaLight(const AreaLight& light)
{
    m_AreaLights.push_back(light);
}

void Scene::SetDirectionalLights(std::vector<DirectionalLight> lights)
{
    m_DirectionalLights = std::move(lights);
}

void Scene::SetPointLights(std::vector<PointLight> lights)
{
    m_PointLights = std::move(lights);
}

void Scene::SetAreaLights(std::vector<AreaLight> lights)
{
    m_AreaLights = std::move(lights);
}

void Scene::UpdateCamera(
    const Camera& camera,
    const float fieldOfView,
    const float nearClipPlane,
    const float farClipPlane)
{
    Camera& runtimeCamera = GetRuntimeCamera();
    runtimeCamera.SetTranslation(camera.GetTranslation());
    runtimeCamera.SetRotation(camera.GetRotation());
    runtimeCamera.SetFov(fieldOfView);
    m_Camera.FieldOfView = fieldOfView;
    m_Camera.NearClipPlane = nearClipPlane;
    m_Camera.FarClipPlane = farClipPlane;
    XMStoreFloat3(&m_Camera.SourceBinding.LocalPosition, camera.GetTranslation());
    XMStoreFloat4(&m_Camera.SourceBinding.LocalRotation, camera.GetRotation());
}

const std::filesystem::path& Scene::GetSourcePath() const
{
    return m_SourcePath;
}

const std::filesystem::path& Scene::GetProjectRoot() const
{
    return m_ProjectRoot;
}

const std::filesystem::path& Scene::GetAssetsRoot() const
{
    return m_AssetsRoot;
}

const SceneSkybox& Scene::GetSkybox() const
{
    return m_Skybox;
}

const SceneLightGroupSettings& Scene::GetLightGroupSettings() const
{
    return m_LightGroupSettings;
}

const SceneCamera& Scene::GetCamera() const
{
    return m_Camera;
}

SceneCamera& Scene::GetMutableCamera()
{
    return m_Camera;
}

Camera& Scene::GetRuntimeCamera()
{
    if (m_Camera.RuntimeCamera == nullptr)
    {
        m_Camera.RuntimeCamera = std::make_shared<Camera>();
    }
    return *m_Camera.RuntimeCamera;
}

const Camera& Scene::GetRuntimeCamera() const
{
    if (m_Camera.RuntimeCamera == nullptr)
    {
        throw std::runtime_error("Scene runtime camera is not initialized.");
    }
    return *m_Camera.RuntimeCamera;
}

const std::vector<SceneObject>& Scene::GetObjects() const
{
    return m_Objects;
}

const std::vector<SceneMaterial>& Scene::GetMaterials() const
{
    return m_Materials;
}

const std::vector<DirectionalLight>& Scene::GetDirectionalLights() const
{
    return m_DirectionalLights;
}

const std::vector<PointLight>& Scene::GetPointLights() const
{
    return m_PointLights;
}

const std::vector<AreaLight>& Scene::GetAreaLights() const
{
    return m_AreaLights;
}
//Modify End
