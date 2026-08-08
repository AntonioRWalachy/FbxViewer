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

// Textura de um material ja na GPU. As dimensoes ficam guardadas porque a aba
// de UV precisa delas para respeitar a proporcao da imagem.
struct GpuTexture
{
    ComPtr<ID3D11ShaderResourceView> srv;
    UINT width = 0;
    UINT height = 0;

    bool Valid() const { return srv != nullptr; }
};

// Buffers de GPU para um SceneModel carregado.
struct GpuModel
{
    ComPtr<ID3D11Buffer> vertexBuffer;
    ComPtr<ID3D11Buffer> indexBuffer;
    UINT vertexCount = 0;
    UINT indexCount = 0;
    std::vector<SubMesh> subMeshes;
    std::vector<GpuTexture> materialTextures; // por material, pode estar vazia
};

// Estado de iluminacao resolvido (preset + ajustes do usuario).
// Montado pela UI e consumido pelo RenderScene.
struct LightingState
{
    XMFLOAT3 background = XMFLOAT3(0.18f, 0.18f, 0.20f);
    XMFLOAT3 ambientColor = XMFLOAT3(1, 1, 1);
    float ambientIntensity = 0.22f;
    XMFLOAT3 mainLightColor = XMFLOAT3(1, 1, 1);
    float mainLightIntensity = 1.0f;
    float rotationDeg = 45.0f;   // azimute da luz principal ao redor do modelo
    float elevationDeg = 40.0f;  // altura da luz principal

    struct AuxLight
    {
        XMFLOAT3 direction = XMFLOAT3(0, -1, 0); // direcao em que a luz viaja
        XMFLOAT3 color = XMFLOAT3(1, 1, 1);
        float intensity = 1.0f;
        bool enabled = false;
    };
    AuxLight aux[3];

    // Chao: com opacidade 0 ele fica invisivel e so a sombra projetada
    // aparece, como no visualizador 3D nativo.
    XMFLOAT3 groundColor = XMFLOAT3(0.55f, 0.55f, 0.58f);
    float groundOpacity = 0.0f;

    // Espacamento das amostras do PCF, em texels do shadow map: quanto maior,
    // mais suave a borda da sombra.
    float shadowSoftness = 1.6f;

    bool showGrid = false;
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
    float AmbientIntensity;
    XMFLOAT3 MainLightColor;
    float MainLightIntensity;
    XMFLOAT3 AmbientColor;
    float ShadowSoftness;
    XMFLOAT4 AuxDir[3];   // xyz = direcao, w = 1 se habilitada
    XMFLOAT4 AuxColor[3]; // rgb = cor, w = intensidade
    XMFLOAT3 GroundColor;
    float GroundOpacity;
};

struct alignas(16) MaterialConstants
{
    XMFLOAT4 DiffuseColor;
    int HasTexture;
    int UseMaterial;
    float _padding2[2];
};

// cbuffer dos passos 2D (gizmo de orientacao e aba de UV) — ver Overlay.hlsl
struct alignas(16) OverlayConstants
{
    XMFLOAT4 Transform; // xy = meia-extensao em NDC, zw = deslocamento
    XMFLOAT4 Tint;      // multiplica a cor final
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

    // Aponta a camera na direcao pedida (usado pelo clique no gizmo).
    void LookFromDirection(const XMFLOAT3& direction);
};

// Estado da aba de UV de um documento.
struct UvViewState
{
    float zoom = 1.0f;
    float panX = 0.0f;   // em NDC
    float panY = 0.0f;
    int materialIndex = 0; // material cuja textura e exibida / destacada
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
    // plano de chao com sombra projetada, overlay de wireframe (opcional) e o
    // gizmo de orientacao no canto.
    void RenderScene(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
        UINT width, UINT height,
        const GpuModel& gpu, const SceneModel& cpuModel, const OrbitCamera& camera,
        ShadingMode mode, bool drawWireframe, bool drawShadows,
        const LightingState& lighting);

    // Renderiza a aba de UV: imagem de textura ao fundo e o desenho das UVs
    // por cima.
    void RenderUvView(ID3D11RenderTargetView* rtv, UINT width, UINT height,
        const GpuModel& gpu, const SceneModel& cpuModel, const UvViewState& uv);

    // Limpa o alvo com uma cor solida (usado quando nao ha modelo aberto).
    void ClearTarget(ID3D11RenderTargetView* rtv, UINT width, UINT height, const XMFLOAT3& color);

    // Renderiza a cena num alvo fora da tela e devolve os pixels em BGRA
    // (linhas de cima para baixo), prontos para o WIC ou para um DIB.
    // Usado pela exportacao de imagem, que pode pedir uma resolucao bem maior
    // que a da janela.
    bool RenderToImage(UINT width, UINT height,
        const GpuModel& gpu, const SceneModel& cpuModel, const OrbitCamera& camera,
        ShadingMode mode, bool drawWireframe, bool drawShadows,
        const LightingState& lighting, bool transparentBackground,
        std::vector<uint8_t>& outBgra);

    void EndFrame(IDXGISwapChain* swapChain);

    ID3D11Device* GetDevice() const { return m_device.Get(); }

    // ---- Gizmo de orientacao ----
    // Retorna a direcao do eixo sob o cursor (em coordenadas do viewport) ou
    // false se o clique nao acertou nenhuma esfera.
    static bool HitTestGizmo(const OrbitCamera& camera, UINT vpWidth, UINT vpHeight,
        int mouseX, int mouseY, XMFLOAT3& outDirection);

private:
    bool CompileShaders();
    bool CreateShadowResources();
    bool CreateGroundPlane();
    bool CreateGrid();
    bool CreateOverlayResources(); // buffer dinamico 2D + atlas do gizmo
    bool CreateGizmoAtlas();

    // Corpo comum de RenderScene e RenderToImage.
    void DrawSceneInternal(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
        UINT width, UINT height,
        const GpuModel& gpu, const SceneModel& cpuModel, const OrbitCamera& camera,
        ShadingMode mode, bool drawWireframe, bool drawShadows,
        const LightingState& lighting, bool transparentBackground);

    GpuTexture LoadTexture(const MaterialData& material);
    GpuTexture CreateTextureFromWicSource(struct IWICBitmapSource* source);

    // Geometria 2D: acumula quads em NDC e despeja num unico DrawInstanced.
    struct OverlayVertex
    {
        XMFLOAT2 position; // ja em NDC do viewport corrente
        XMFLOAT2 uv;
        XMFLOAT4 color;
    };
    void OverlayBegin();
    void OverlayQuad(float cx, float cy, float halfW, float halfH,
        const XMFLOAT4& uvRect, const XMFLOAT4& color);
    void OverlayLine(float x0, float y0, float x1, float y1, float halfWidth,
        const XMFLOAT4& uvRect, const XMFLOAT4& color);
    void OverlayFlush(ID3D11PixelShader* ps, ID3D11ShaderResourceView* srv);
    void WriteOverlayConstants(const XMFLOAT4& transform, const XMFLOAT4& tint);

    void DrawOrientationGizmo(const OrbitCamera& camera, UINT width, UINT height);

    static constexpr UINT kShadowMapSize = 2048;

    // Area quadrada reservada ao gizmo, no canto superior direito do viewport.
    static constexpr int kGizmoBoxSize = 110;
    static constexpr int kGizmoMargin = 12;
    static constexpr float kGizmoBallRadiusNdc = 0.20f;
    static constexpr float kGizmoAxisLengthNdc = 0.74f;

    // Um eixo do gizmo ja projetado na caixa do gizmo.
    struct GizmoAxisPoint
    {
        XMFLOAT3 direction; // direcao no mundo
        float ndcX = 0, ndcY = 0;
        float depth = 0;    // z em espaco de camera: maior = mais longe
        int axis = 0;       // 0 = X, 1 = Y, 2 = Z
        bool positive = true;
    };
    static void ComputeGizmoAxes(const OrbitCamera& camera, GizmoAxisPoint out[6]);
    static RECT GizmoBoxRect(UINT vpWidth, UINT vpHeight);

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
    ComPtr<ID3D11VertexShader> m_vsGrid;
    ComPtr<ID3D11PixelShader> m_psGrid;
    ComPtr<ID3D11InputLayout> m_inputLayout;

    // Passos 2D (gizmo + aba de UV)
    ComPtr<ID3D11VertexShader> m_vsOverlay;  // vertices ja em NDC
    ComPtr<ID3D11VertexShader> m_vsUvMesh;   // malha desenhada no espaco de UV
    ComPtr<ID3D11PixelShader> m_psOverlaySolid;
    ComPtr<ID3D11PixelShader> m_psOverlaySprite;
    ComPtr<ID3D11PixelShader> m_psGizmoBall;
    ComPtr<ID3D11InputLayout> m_overlayLayout;
    ComPtr<ID3D11Buffer> m_overlayVB;
    ComPtr<ID3D11Buffer> m_overlayCB;
    ComPtr<ID3D11ShaderResourceView> m_gizmoAtlas;
    std::vector<OverlayVertex> m_overlayVerts;
    static constexpr UINT kOverlayMaxVerts = 512;

    // Constant buffers
    ComPtr<ID3D11Buffer> m_frameCB;
    ComPtr<ID3D11Buffer> m_materialCB;

    // Estados
    ComPtr<ID3D11RasterizerState> m_rsSolid;
    ComPtr<ID3D11RasterizerState> m_rsWireframe;
    ComPtr<ID3D11RasterizerState> m_rsShadow; // com depth bias p/ evitar shadow acne
    ComPtr<ID3D11RasterizerState> m_rsOverlay; // sem culling nem depth clip
    ComPtr<ID3D11DepthStencilState> m_dsState;
    ComPtr<ID3D11DepthStencilState> m_dsNoWrite; // p/ plano de chao translucido
    ComPtr<ID3D11DepthStencilState> m_dsDisabled; // p/ overlays 2D
    ComPtr<ID3D11SamplerState> m_samplerLinear;
    ComPtr<ID3D11SamplerState> m_samplerClamp;  // p/ overlays (sem repeticao)
    ComPtr<ID3D11SamplerState> m_samplerShadow; // comparison sampler p/ PCF
    ComPtr<ID3D11BlendState> m_blendOpaque;
    ComPtr<ID3D11BlendState> m_blendAlpha;   // p/ sombra no chao e overlays

    // Shadow map
    ComPtr<ID3D11Texture2D> m_shadowTex;
    ComPtr<ID3D11DepthStencilView> m_shadowDSV;
    ComPtr<ID3D11ShaderResourceView> m_shadowSRV;

    // Plano de chao (quad unitario no plano XZ, escalado por matriz World)
    ComPtr<ID3D11Buffer> m_groundVB;

    // Grade do chao: line list num grid unitario (-1..1) no plano XZ
    ComPtr<ID3D11Buffer> m_gridVB;
    UINT m_gridVertexCount = 0;
};
