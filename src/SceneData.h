#pragma once
#ifndef NOMINMAX
#define NOMINMAX // impede que windows.h defina macros min/max que quebram std::min/std::max
#endif
#include <windows.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <memory>

using namespace DirectX;

// Vertice usado pelo pipeline DirectX
struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 uv;
};

// Material simples (cor difusa + textura opcional)
struct MaterialData
{
    std::string name;
    XMFLOAT4 diffuseColor = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
    std::wstring texturePath; // caminho da textura difusa, se houver
};

// Uma sub-malha: faixa de índices que usa um único material
struct SubMesh
{
    UINT indexStart = 0;
    UINT indexCount = 0;
    int materialIndex = -1; // -1 = sem material
};

// Estatisticas de uma malha/cena, para exibir na UI
struct MeshStats
{
    UINT vertexCount = 0;
    UINT triangleCount = 0;
    UINT edgeCount = 0;
    UINT meshCount = 0;
    UINT materialCount = 0;
    UINT drawCallCount = 0; // 1 DrawIndexed por par mesh x material (submesh)
};

// Cena completa carregada de um arquivo (pode ter varias malhas)
struct SceneModel
{
    std::wstring filePath;
    std::wstring displayName;

    std::vector<Vertex> vertices;
    std::vector<UINT> indices;
    std::vector<SubMesh> subMeshes;
    std::vector<MaterialData> materials;

    // Bounding box para enquadrar a camera automaticamente
    XMFLOAT3 boundsMin = XMFLOAT3(0, 0, 0);
    XMFLOAT3 boundsMax = XMFLOAT3(0, 0, 0);

    MeshStats stats;

    bool gpuReady = false;
};
