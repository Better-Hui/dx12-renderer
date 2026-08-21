//Modify Begin:2026-08-21 by Hui
#pragma once

#include <Framework/Geometry/Mesh.h>

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>

namespace FrameworkAssimp
{
    inline constexpr unsigned int GeometryImportFlags =
        aiProcess_ConvertToLeftHanded |
        aiProcess_CalcTangentSpace |
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType |
        aiProcess_GenSmoothNormals |
        aiProcess_PopulateArmatureData |
        aiProcess_LimitBoneWeights |
        aiProcess_SplitLargeMeshes |
        aiProcess_ValidateDataStructure |
        aiProcess_FindInvalidData;

    inline void ConfigureGeometryImporter(Assimp::Importer& importer)
    {
        importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, 65535);
        importer.SetPropertyInteger(
            AI_CONFIG_PP_LBW_MAX_WEIGHTS,
            static_cast<int>(SkinningVertexAttributes::BONES_PER_VERTEX));
    }
}
//Modify End
