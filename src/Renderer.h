#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include "SceneData.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

enum class ShadingMode
{
    Material,   // com material/textura
    NoMaterial  // cinza solido com shading basico
};

// Buffers de GPU para um SceneModel carregado.
struct GpuModel
{
    ComPtr<ID3D11Buffer> vertexBuffer;
    ComPtr<ID3D11Buffer> indexBuffer;
    UINT vertexCount = 0;
    UINT indexCount = 0;
    std::vector<SubMesh> subMeshes;
    std::vector<ComPtr<ID3D11ShaderResourceView>> materialTextures; // por material, pode ser nullptr
};

// Layout ESPELHADO nos cbuffers dos .hlsl — manter sincronizado!
struct alignas(16) FrameConstants
{
    XMMATRIX World;
    XMMATRIX View;
    XMMATRIX Projection;
    XMMATRIX LightViewProj; // para shadow mapping
    XMFLOAT3 LightDirection;
    int ShadowsEnabled;
    XMFLOAT3 CameraPosition;
    float _padding1;
};

struct alignas(16) MaterialConstants
{
    XMFLOAT4 DiffuseColor;
    int HasTexture;
    int UseMaterial;
    float _padding2[2];
};

// Camera orbital simples (estilo "olhar para o centro do objeto")
struct OrbitCamera
{
    float yaw = 0.6f;
    float pitch = 0.4f;
    float distance = 5.0f;
    XMFLOAT3 target = XMFLOAT3(0, 0, 0);

    XMMATRIX GetViewMatrix() const;
    XMFLOAT3 GetEyePosition() const;
};

// Encapsula o device/contexto D3D11 (compartilhado por todas as abas) e o
// estado de renderizacao de uma aba especifica (swapchain + render target).
class Renderer
{
public:
    bool InitDevice(); // cria device + contexto compartilhados (chamar uma vez)
    bool CreateSwapChainForWindow(HWND hwnd, UINT width, UINT height,
        ComPtr<IDXGISwapChain>& outSwapChain, ComPtr<ID3D11RenderTargetView>& outRTV,
        ComPtr<ID3D11DepthStencilView>& outDSV, ComPtr<ID3D11Texture2D>& outDepthTex);

    void ResizeSwapChain(ComPtr<IDXGISwapChain>& swapChain, UINT width, UINT height,
        ComPtr<ID3D11RenderTargetView>& outRTV, ComPtr<ID3D11DepthStencilView>& outDSV,
        ComPtr<ID3D11Texture2D>& outDepthTex);

    bool UploadModel(const SceneModel& model, GpuModel& outGpu);

    // Renderiza a cena completa: passo de sombra (opcional), passo solido,
    // plano de chao com sombra projetada, e overlay de wireframe (opcional).
    void RenderScene(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
        UINT width, UINT height,
        const GpuModel& gpu, const SceneModel& cpuModel, const OrbitCamera& camera,
        ShadingMode mode, bool drawWireframe, bool drawShadows);

    void EndFrame(IDXGISwapChain* swapChain);

    ID3D11Device* GetDevice() const { return m_device.Get(); }

private:
    bool CompileShaders();
    bool CreateShadowResources();
    bool CreateGroundPlane();
    ComPtr<ID3D11ShaderResourceView> LoadTexture(const std::wstring& path);

    static constexpr UINT kShadowMapSize = 2048;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;

    // Shaders
    ComPtr<ID3D11VertexShader> m_vsMain;
    ComPtr<ID3D11PixelShader> m_psMain;
    ComPtr<ID3D11VertexShader> m_vsWire;
    ComPtr<ID3D11PixelShader> m_psWire;
    ComPtr<ID3D11VertexShader> m_vsShadow;  // passo de profundidade (luz)
    ComPtr<ID3D11VertexShader> m_vsGround;  // plano receptor de sombra
    ComPtr<ID3D11PixelShader> m_psGround;
    ComPtr<ID3D11InputLayout> m_inputLayout;

    // Constant buffers
    ComPtr<ID3D11Buffer> m_frameCB;
    ComPtr<ID3D11Buffer> m_materialCB;

    // Estados
    ComPtr<ID3D11RasterizerState> m_rsSolid;
    ComPtr<ID3D11RasterizerState> m_rsWireframe;
    ComPtr<ID3D11RasterizerState> m_rsShadow; // com depth bias p/ evitar shadow acne
    ComPtr<ID3D11DepthStencilState> m_dsState;
    ComPtr<ID3D11DepthStencilState> m_dsNoWrite; // p/ plano de chao translucido
    ComPtr<ID3D11SamplerState> m_samplerLinear;
    ComPtr<ID3D11SamplerState> m_samplerShadow; // comparison sampler p/ PCF
    ComPtr<ID3D11BlendState> m_blendOpaque;
    ComPtr<ID3D11BlendState> m_blendAlpha;   // p/ sombra no chao

    // Shadow map
    ComPtr<ID3D11Texture2D> m_shadowTex;
    ComPtr<ID3D11DepthStencilView> m_shadowDSV;
    ComPtr<ID3D11ShaderResourceView> m_shadowSRV;

    // Plano de chao (quad unitario no plano XZ, escalado por matriz World)
    ComPtr<ID3D11Buffer> m_groundVB;
};
