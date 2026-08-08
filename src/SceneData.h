#pragma once
#ifndef NOMINMAX
#define NOMINMAX // impede que windows.h defina macros min/max que quebram std::min/std::max
#endif
#include <windows.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

using namespace DirectX;

// Vertice usado pelo pipeline DirectX.
// A cor por vertice e branca por padrao (neutra na multiplicacao do shader);
// so PLY e glTF com COLOR_0 costumam preencher esse campo.
struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 uv;
    uint32_t color = 0xFFFFFFFFu; // R8G8B8A8 (ordem de bytes: R,G,B,A)
};

// Empacota canais 0..1 no formato R8G8B8A8_UNORM esperado pelo input layout.
inline uint32_t PackColorRgba(float r, float g, float b, float a = 1.0f)
{
    auto to8 = [](float v) -> uint32_t
    {
        float c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return (uint32_t)(c * 255.0f + 0.5f);
    };
    return to8(r) | (to8(g) << 8) | (to8(b) << 16) | (to8(a) << 24);
}

// Material simples (cor difusa + textura opcional)
struct MaterialData
{
    std::string name;
    XMFLOAT4 diffuseColor = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
    std::wstring texturePath; // caminho da textura difusa em disco, se houver

    // Textura embutida no proprio arquivo (GLB e .gltf com data URI): os bytes
    // do PNG/JPG ficam aqui e sao decodificados direto da memoria, sem passar
    // pelo disco. Quando preenchido, tem prioridade sobre texturePath.
    std::vector<uint8_t> textureBytes;
    std::wstring textureName; // nome amigavel p/ exibir na aba de UV
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

    // O arquivo trazia coordenadas de textura? Quem responde e o loader — dar
    // palpite olhando os valores nao funciona, porque a inversao de V faz uma
    // UV ausente (0,0) virar (0,1) e parecer preenchida.
    bool hasTexCoords = false;

    MeshStats stats;

    bool gpuReady = false;
};
