#include "FbxLoader.h"
#include "LoaderUtils.h"
#include <fbxsdk.h>
#include <unordered_map>
#include <set>
#include <utility>
#include <algorithm>

using loaderutil::RhToLh;
using loaderutil::Utf8ToWide;
using loaderutil::WideToUtf8;

// ---------------------------------------------------------------------------
// Estado interno usado durante o parsing (RAII para o SDK manager)
// ---------------------------------------------------------------------------
namespace
{
    struct FbxSdkContext
    {
        FbxManager* manager = nullptr;
        FbxScene* scene = nullptr;

        FbxSdkContext()
        {
            manager = FbxManager::Create();
            FbxIOSettings* ios = FbxIOSettings::Create(manager, IOSROOT);
            manager->SetIOSettings(ios);
            scene = FbxScene::Create(manager, "Scene");
        }

        ~FbxSdkContext()
        {
            if (manager) manager->Destroy();
        }
    };

    // Le a cor difusa de um material FBX, com fallback para cinza claro.
    XMFLOAT4 ExtractDiffuseColor(FbxSurfaceMaterial* mat)
    {
        XMFLOAT4 color(0.8f, 0.8f, 0.8f, 1.0f);
        if (!mat) return color;

        if (mat->GetClassId().Is(FbxSurfacePhong::ClassId))
        {
            FbxSurfacePhong* phong = (FbxSurfacePhong*)mat;
            FbxDouble3 c = phong->Diffuse.Get();
            color = XMFLOAT4((float)c[0], (float)c[1], (float)c[2], 1.0f);
        }
        else if (mat->GetClassId().Is(FbxSurfaceLambert::ClassId))
        {
            FbxSurfaceLambert* lambert = (FbxSurfaceLambert*)mat;
            FbxDouble3 c = lambert->Diffuse.Get();
            color = XMFLOAT4((float)c[0], (float)c[1], (float)c[2], 1.0f);
        }
        return color;
    }

    // Caminhos de textura gravados no material: o absoluto (da maquina que
    // exportou) e o relativo (em relacao a pasta do proprio .fbx).
    struct TexturePathInfo
    {
        std::wstring absolutePath;
        std::wstring relativePath;
    };

    // Procura a textura difusa de um material, se existir.
    TexturePathInfo ExtractDiffuseTexture(FbxSurfaceMaterial* mat)
    {
        TexturePathInfo info;
        if (!mat) return info;
        FbxProperty prop = mat->FindProperty(FbxSurfaceMaterial::sDiffuse);
        if (!prop.IsValid()) return info;

        FbxFileTexture* tex = nullptr;

        int layeredTextureCount = prop.GetSrcObjectCount<FbxLayeredTexture>();
        if (layeredTextureCount > 0)
        {
            FbxLayeredTexture* layered = prop.GetSrcObject<FbxLayeredTexture>(0);
            if (layered && layered->GetSrcObjectCount<FbxTexture>() > 0)
                tex = layered->GetSrcObject<FbxFileTexture>(0);
        }
        else if (prop.GetSrcObjectCount<FbxTexture>() > 0)
        {
            tex = prop.GetSrcObject<FbxFileTexture>(0);
        }

        if (tex)
        {
            info.absolutePath = Utf8ToWide(tex->GetFileName());
            info.relativePath = Utf8ToWide(tex->GetRelativeFileName());
        }
        return info;
    }

    // Triangula a cena inteira (necessario pois malhas FBX podem ter n-gons)
    void TriangulateScene(FbxManager* manager, FbxScene* scene)
    {
        FbxGeometryConverter converter(manager);
        converter.Triangulate(scene, /*replace=*/true);
    }

    // Processa recursivamente os nodes da cena, achatando tudo em um unico
    // buffer de vertices/indices (com sub-meshes por material).
    void ProcessNode(
        FbxNode* node,
        SceneModel& outModel,
        std::vector<FbxSurfaceMaterial*>& materialOrder,
        std::unordered_map<FbxSurfaceMaterial*, int>& materialIndexMap,
        std::set<std::pair<int, uint64_t>>& edgeSet,
        int& nextMeshIndex)
    {
        FbxMesh* mesh = node->GetMesh();
        if (mesh)
        {
            int meshIndex = nextMeshIndex++; // diferencia arestas entre meshes distintos
            FbxAMatrix globalTransform = node->EvaluateGlobalTransform();

            // A cena esta em right-handed Y-up (ver LoadFbxFile) e o renderer
            // e left-handed, entao RhToLh espelha X — o que inverte o sentido
            // dos triangulos. Um node com determinante negativo (objeto
            // espelhado no DCC) inverte de novo e as duas inversoes se
            // cancelam.
            const bool flipWinding = (globalTransform.Determinant() >= 0.0);

            int polyCount = mesh->GetPolygonCount();
            FbxVector4* controlPoints = mesh->GetControlPoints();

            int materialCountOnMesh = node->GetMaterialCount();
            std::vector<int> localMaterialIndices; // indice global do material por slot local
            for (int m = 0; m < materialCountOnMesh; m++)
            {
                FbxSurfaceMaterial* mat = node->GetMaterial(m);
                auto it = materialIndexMap.find(mat);
                int globalIdx;
                if (it == materialIndexMap.end())
                {
                    globalIdx = (int)materialOrder.size();
                    materialOrder.push_back(mat);
                    materialIndexMap[mat] = globalIdx;
                }
                else
                {
                    globalIdx = it->second;
                }
                localMaterialIndices.push_back(globalIdx);
            }
            if (localMaterialIndices.empty())
                localMaterialIndices.push_back(-1);

            // Agrupa triangulos por material (sub-mesh)
            std::unordered_map<int, std::vector<UINT>> subMeshIndices; // materialIdx -> indices

            FbxGeometryElementUV* uvElement = mesh->GetElementUVCount() > 0 ? mesh->GetElementUV(0) : nullptr;
            if (uvElement) outModel.hasTexCoords = true;
            FbxGeometryElementNormal* normalElement = mesh->GetElementNormalCount() > 0 ? mesh->GetElementNormal(0) : nullptr;
            FbxGeometryElementMaterial* matElement = mesh->GetElementMaterialCount() > 0 ? mesh->GetElementMaterial(0) : nullptr;

            for (int p = 0; p < polyCount; p++)
            {
                int polySize = mesh->GetPolygonSize(p);
                // Apos triangulacao deve ser sempre 3, mas protegemos mesmo assim.
                if (polySize != 3) continue;

                int matSlot = 0;
                if (matElement)
                {
                    if (matElement->GetMappingMode() == FbxGeometryElement::eByPolygon)
                    {
                        int idx = matElement->GetIndexArray().GetAt(p);
                        if (idx >= 0 && idx < (int)localMaterialIndices.size())
                            matSlot = idx;
                    }
                }
                int globalMaterialIdx = localMaterialIndices[matSlot];

                UINT triIndices[3];
                int ctrlPointIndices[3]; // indices de ponto de controle originais, p/ contagem de arestas unicas
                for (int v = 0; v < 3; v++)
                {
                    int ctrlPointIndex = mesh->GetPolygonVertex(p, v);
                    ctrlPointIndices[v] = ctrlPointIndex;
                    FbxVector4 pos = controlPoints[ctrlPointIndex];
                    FbxVector4 worldPos = globalTransform.MultT(pos);

                    FbxVector4 normal(0, 1, 0, 0);
                    if (normalElement)
                    {
                        int normalIndex = ctrlPointIndex;
                        if (normalElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
                        {
                            int vertexId = p * 3 + v; // valido pois ja triangulado
                            normalIndex = (normalElement->GetReferenceMode() == FbxGeometryElement::eDirect)
                                ? vertexId
                                : normalElement->GetIndexArray().GetAt(vertexId);
                        }
                        else if (normalElement->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
                        {
                            normalIndex = normalElement->GetIndexArray().GetAt(ctrlPointIndex);
                        }
                        if (normalIndex >= 0 && normalIndex < normalElement->GetDirectArray().GetCount())
                            normal = normalElement->GetDirectArray().GetAt(normalIndex);

                        FbxAMatrix normalMatrix = globalTransform;
                        normalMatrix.SetT(FbxVector4(0, 0, 0, 0));
                        normal = normalMatrix.MultT(normal);
                        normal.Normalize();
                    }

                    FbxVector2 uv(0, 0);
                    if (uvElement)
                    {
                        int uvIndex = ctrlPointIndex;
                        if (uvElement->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
                        {
                            int textureUVIndex = mesh->GetTextureUVIndex(p, v);
                            uvIndex = textureUVIndex;
                        }
                        if (uvIndex >= 0 && uvIndex < uvElement->GetDirectArray().GetCount())
                            uv = uvElement->GetDirectArray().GetAt(uvIndex);
                    }

                    Vertex vert;
                    vert.position = RhToLh((float)worldPos[0], (float)worldPos[1], (float)worldPos[2]);
                    vert.normal = RhToLh((float)normal[0], (float)normal[1], (float)normal[2]);
                    vert.uv = XMFLOAT2((float)uv[0], 1.0f - (float)uv[1]); // V invertido (convencao DX)

                    UINT newIndex = (UINT)outModel.vertices.size();
                    outModel.vertices.push_back(vert);
                    triIndices[v] = newIndex;

                    // Atualiza bounding box
                    outModel.boundsMin.x = std::min(outModel.boundsMin.x, vert.position.x);
                    outModel.boundsMin.y = std::min(outModel.boundsMin.y, vert.position.y);
                    outModel.boundsMin.z = std::min(outModel.boundsMin.z, vert.position.z);
                    outModel.boundsMax.x = std::max(outModel.boundsMax.x, vert.position.x);
                    outModel.boundsMax.y = std::max(outModel.boundsMax.y, vert.position.y);
                    outModel.boundsMax.z = std::max(outModel.boundsMax.z, vert.position.z);
                }

                std::vector<UINT>& target = subMeshIndices[globalMaterialIdx];
                target.push_back(triIndices[0]);
                target.push_back(flipWinding ? triIndices[2] : triIndices[1]);
                target.push_back(flipWinding ? triIndices[1] : triIndices[2]);

                // Conta arestas unicas usando os indices de ponto de controle
                // ORIGINAIS da malha (estaveis por vertice fisico), nao os
                // indices de vertice duplicados que vao para a GPU — caso
                // contrario cada triangulo contaria suas 3 arestas como
                // "novas", mesmo quando compartilhadas com o vizinho.
                // O meshIndex entra na chave para nao confundir arestas de
                // meshes diferentes que reusam os mesmos indices de ponto
                // de controle (cada FbxMesh comeca a contar do zero).
                auto addEdge = [&](int a, int b)
                {
                    if (a > b) std::swap(a, b);
                    uint64_t key = ((uint64_t)(uint32_t)a << 32) | (uint64_t)(uint32_t)b;
                    edgeSet.insert({ meshIndex, key });
                };
                addEdge(ctrlPointIndices[0], ctrlPointIndices[1]);
                addEdge(ctrlPointIndices[1], ctrlPointIndices[2]);
                addEdge(ctrlPointIndices[2], ctrlPointIndices[0]);
            }

            for (auto& kv : subMeshIndices)
            {
                SubMesh sm;
                sm.indexStart = (UINT)outModel.indices.size();
                sm.indexCount = (UINT)kv.second.size();
                sm.materialIndex = kv.first;
                outModel.indices.insert(outModel.indices.end(), kv.second.begin(), kv.second.end());
                outModel.subMeshes.push_back(sm);
            }

            outModel.stats.meshCount++;
        }

        int childCount = node->GetChildCount();
        for (int i = 0; i < childCount; i++)
            ProcessNode(node->GetChild(i), outModel, materialOrder, materialIndexMap, edgeSet, nextMeshIndex);
    }
}

bool LoadFbxFile(const std::wstring& filePath, SceneModel& outModel, std::wstring& outError)
{
    FbxSdkContext ctx;
    if (!ctx.manager || !ctx.scene)
    {
        outError = L"Falha ao inicializar o FBX SDK.";
        return false;
    }

    FbxImporter* importer = FbxImporter::Create(ctx.manager, "");
    std::string narrowPath = WideToUtf8(filePath);

    bool importStatus = importer->Initialize(narrowPath.c_str(), -1, ctx.manager->GetIOSettings());
    if (!importStatus)
    {
        outError = Utf8ToWide(importer->GetStatus().GetErrorString());
        importer->Destroy();
        return false;
    }

    importStatus = importer->Import(ctx.scene);
    importer->Destroy();
    if (!importStatus)
    {
        outError = L"Falha ao importar a cena do arquivo.";
        return false;
    }

    // Normaliza a orientacao da cena para right-handed com Y para cima
    // (sistema "OpenGL"), que e o mesmo dos loaders de OBJ/PLY/glTF. A
    // conversao para o sistema left-handed do renderer e feita depois, vertice
    // a vertice, por RhToLh — deixar o SDK trocar a lateralidade sozinho nao
    // e confiavel e era a origem do modelo aparecer espelhado.
    // As unidades originais do arquivo sao mantidas; a camera se enquadra pelo
    // bounding box.
    FbxAxisSystem sceneAxisSystem = ctx.scene->GetGlobalSettings().GetAxisSystem();
    if (sceneAxisSystem != FbxAxisSystem::OpenGL)
        FbxAxisSystem::OpenGL.ConvertScene(ctx.scene);

    TriangulateScene(ctx.manager, ctx.scene);

    FbxNode* root = ctx.scene->GetRootNode();
    if (!root)
    {
        outError = L"Cena sem root node.";
        return false;
    }

    outModel.boundsMin = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
    outModel.boundsMax = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    std::vector<FbxSurfaceMaterial*> materialOrder;
    std::unordered_map<FbxSurfaceMaterial*, int> materialIndexMap;
    std::set<std::pair<int, uint64_t>> edgeSet;
    int nextMeshIndex = 0;

    int childCount = root->GetChildCount();
    for (int i = 0; i < childCount; i++)
        ProcessNode(root->GetChild(i), outModel, materialOrder, materialIndexMap, edgeSet, nextMeshIndex);

    // Se nao achou nenhum vertice, zera o bounding box pra nao ficar com FLT_MAX
    if (outModel.vertices.empty())
    {
        outModel.boundsMin = XMFLOAT3(0, 0, 0);
        outModel.boundsMax = XMFLOAT3(0, 0, 0);
    }

    // Materiais
    std::wstring baseDir;
    {
        size_t slashPos = filePath.find_last_of(L"\\/");
        if (slashPos != std::wstring::npos)
            baseDir = filePath.substr(0, slashPos + 1);
    }

    // Pasta de extracao de midias embutidas: o FBX SDK descompacta texturas
    // embedded para "<arquivo>.fbm\" ao lado do .fbx durante o Import.
    std::wstring fbmDir;
    {
        size_t dotPos = filePath.find_last_of(L'.');
        std::wstring noExt = (dotPos == std::wstring::npos) ? filePath : filePath.substr(0, dotPos);
        fbmDir = noExt + L".fbm\\";
    }

    auto fileExists = [](const std::wstring& p)
    {
        if (p.empty()) return false;
        DWORD attr = GetFileAttributesW(p.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    };

    auto fileNameOf = [](const std::wstring& p) -> std::wstring
    {
        size_t slash = p.find_last_of(L"\\/");
        return (slash == std::wstring::npos) ? p : p.substr(slash + 1);
    };

    // Tenta localizar a textura testando os candidatos em ordem de confianca.
    auto resolveTexture = [&](const TexturePathInfo& info) -> std::wstring
    {
        if (info.absolutePath.empty() && info.relativePath.empty())
            return L"";

        std::wstring name = fileNameOf(
            !info.absolutePath.empty() ? info.absolutePath : info.relativePath);

        const std::wstring candidates[] = {
            info.absolutePath,               // caminho absoluto como gravado
            baseDir + info.relativePath,     // relativo a pasta do .fbx
            baseDir + name,                  // so o nome, na pasta do .fbx
            fbmDir + name,                   // pasta .fbm (texturas embedded extraidas)
            baseDir + L"textures\\" + name,  // convencao comum de projetos
            baseDir + L"Textures\\" + name,
        };
        for (const std::wstring& c : candidates)
            if (fileExists(c))
                return c;
        return L"";
    };

    for (FbxSurfaceMaterial* mat : materialOrder)
    {
        MaterialData md;
        md.name = mat ? mat->GetName() : "Default";
        md.diffuseColor = ExtractDiffuseColor(mat);

        TexturePathInfo texInfo = ExtractDiffuseTexture(mat);
        bool materialHasTexture = !texInfo.absolutePath.empty() || !texInfo.relativePath.empty();
        md.texturePath = resolveTexture(texInfo);
        if (!md.texturePath.empty())
            md.textureName = fileNameOf(md.texturePath);

        // Quando o material usa textura, e comum a cor difusa vir PRETA no
        // FBX (a textura e quem define a cor). Como o shader multiplica
        // cor * textura, preto anularia tudo — entao clareamos a base.
        // O mesmo vale se a textura existia mas nao foi encontrada no disco:
        // melhor um modelo cinza-claro do que um vulto preto.
        if (materialHasTexture)
        {
            float lum = 0.2126f * md.diffuseColor.x
                      + 0.7152f * md.diffuseColor.y
                      + 0.0722f * md.diffuseColor.z;
            if (lum < 0.05f)
                md.diffuseColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        outModel.materials.push_back(md);
    }
    if (outModel.materials.empty())
    {
        MaterialData md;
        md.name = "Default";
        outModel.materials.push_back(md);
        for (auto& sm : outModel.subMeshes)
            if (sm.materialIndex < 0) sm.materialIndex = 0;
    }

    outModel.stats.vertexCount = (UINT)outModel.vertices.size(); // vertices "de GPU" (duplicados por normal/UV, como no viewer nativo)
    outModel.stats.triangleCount = (UINT)outModel.indices.size() / 3;
    outModel.stats.edgeCount = (UINT)edgeSet.size(); // arestas unicas da topologia original (por ponto de controle)
    outModel.stats.materialCount = (UINT)outModel.materials.size();
    outModel.stats.drawCallCount = (UINT)outModel.subMeshes.size(); // 1 draw call por submesh (mesh x material)

    if (outModel.vertices.empty())
    {
        outError = L"Nenhuma malha encontrada no arquivo.";
        return false;
    }

    return true;
}
