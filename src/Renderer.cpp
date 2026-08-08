#include "Renderer.h"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace
{
    ComPtr<ID3DBlob> CompileShaderFromFile(const std::wstring& path, const char* entryPoint, const char* target)
    {
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG;
#endif

        HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, nullptr, entryPoint, target, flags, 0,
            &shaderBlob, &errorBlob);

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                std::string msg((char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize());
                OutputDebugStringA(msg.c_str());
            }
            return nullptr;
        }
        return shaderBlob;
    }

    // ---- Atlas do gizmo: 4 celulas lado a lado (X, Y, Z, anel) ----
    constexpr int kAtlasCellCount = 4;
    constexpr int kAtlasCellPixels = 32;   // tamanho exato em que a esfera e desenhada
    constexpr int kAtlasSupersample = 8;   // desenha grande e reduz: antialias barato

    XMFLOAT4 AtlasCell(int index)
    {
        float u0 = (float)index / (float)kAtlasCellCount;
        return XMFLOAT4(u0, 0.0f, u0 + 1.0f / (float)kAtlasCellCount, 1.0f);
    }

    // Cores dos eixos, proximas das do Blender.
    const XMFLOAT4 kAxisColors[3] = {
        XMFLOAT4(0.88f, 0.30f, 0.34f, 1.0f), // X — vermelho
        XMFLOAT4(0.51f, 0.77f, 0.20f, 1.0f), // Y — verde
        XMFLOAT4(0.22f, 0.53f, 0.90f, 1.0f), // Z — azul
    };
}

XMMATRIX OrbitCamera::GetViewMatrix() const
{
    XMFLOAT3 eye = GetEyePosition();
    XMVECTOR eyeVec = XMLoadFloat3(&eye);
    XMVECTOR targetVec = XMLoadFloat3(&target);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    return XMMatrixLookAtLH(eyeVec, targetVec, up);
}

XMFLOAT3 OrbitCamera::GetEyePosition() const
{
    float x = distance * cosf(pitch) * sinf(yaw);
    float y = distance * sinf(pitch);
    float z = distance * cosf(pitch) * cosf(yaw);
    return XMFLOAT3(target.x + x, target.y + y, target.z + z);
}

void OrbitCamera::LookFromDirection(const XMFLOAT3& direction)
{
    XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&direction));
    XMFLOAT3 n;
    XMStoreFloat3(&n, d);

    // Limite igual ao do arrasto: evita a camera coincidir com o vetor "up".
    const float kMaxPitch = 1.5f;
    pitch = std::clamp(asinf(std::clamp(n.y, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);

    // Nas vistas de topo/base o azimute e indefinido; manter o atual evita um
    // giro inesperado da cena.
    if (fabsf(n.x) > 1e-4f || fabsf(n.z) > 1e-4f)
        yaw = atan2f(n.x, n.z);
}

bool Renderer::InitDevice()
{
    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
        nullptr, 0, D3D11_SDK_VERSION,
        &m_device, &featureLevel, &m_context);

    if (FAILED(hr)) return false;

    if (!CompileShaders()) return false;
    if (!CreateShadowResources()) return false;
    if (!CreateGroundPlane()) return false;
    if (!CreateGrid()) return false;

    // Rasterizer states
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.DepthClipEnable = TRUE;
    m_device->CreateRasterizerState(&rsDesc, &m_rsSolid);

    D3D11_RASTERIZER_DESC rsWireDesc = rsDesc;
    rsWireDesc.FillMode = D3D11_FILL_WIREFRAME;
    rsWireDesc.CullMode = D3D11_CULL_NONE;
    rsWireDesc.DepthBias = -50; // evita z-fighting do wireframe sobre a malha solida
    rsWireDesc.SlopeScaledDepthBias = -1.0f;
    m_device->CreateRasterizerState(&rsWireDesc, &m_rsWireframe);

    // Rasterizer do passo de sombra: bias positivo reduz "shadow acne"
    D3D11_RASTERIZER_DESC rsShadowDesc = rsDesc;
    rsShadowDesc.CullMode = D3D11_CULL_BACK;
    rsShadowDesc.DepthBias = 60;
    rsShadowDesc.SlopeScaledDepthBias = 2.0f;
    rsShadowDesc.DepthBiasClamp = 0.01f;
    m_device->CreateRasterizerState(&rsShadowDesc, &m_rsShadow);

    // Rasterizer dos overlays 2D: sem culling (os quads sao gerados na CPU)
    D3D11_RASTERIZER_DESC rsOverlayDesc = {};
    rsOverlayDesc.FillMode = D3D11_FILL_SOLID;
    rsOverlayDesc.CullMode = D3D11_CULL_NONE;
    rsOverlayDesc.DepthClipEnable = FALSE;
    m_device->CreateRasterizerState(&rsOverlayDesc, &m_rsOverlay);

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    m_device->CreateDepthStencilState(&dsDesc, &m_dsState);

    D3D11_DEPTH_STENCIL_DESC dsNoWriteDesc = dsDesc;
    dsNoWriteDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; // chao nao escreve depth
    m_device->CreateDepthStencilState(&dsNoWriteDesc, &m_dsNoWrite);

    D3D11_DEPTH_STENCIL_DESC dsOffDesc = {};
    dsOffDesc.DepthEnable = FALSE;
    dsOffDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    m_device->CreateDepthStencilState(&dsOffDesc, &m_dsDisabled);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&sampDesc, &m_samplerLinear);

    D3D11_SAMPLER_DESC clampDesc = sampDesc;
    clampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    m_device->CreateSamplerState(&clampDesc, &m_samplerClamp);

    // Comparison sampler para PCF do shadow map
    D3D11_SAMPLER_DESC shadowSampDesc = {};
    shadowSampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadowSampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    shadowSampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    shadowSampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    shadowSampDesc.BorderColor[0] = 1.0f; // fora do mapa = sem sombra
    shadowSampDesc.BorderColor[1] = 1.0f;
    shadowSampDesc.BorderColor[2] = 1.0f;
    shadowSampDesc.BorderColor[3] = 1.0f;
    shadowSampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    m_device->CreateSamplerState(&shadowSampDesc, &m_samplerShadow);

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_device->CreateBlendState(&blendDesc, &m_blendOpaque);

    // Alpha blend para a sombra translucida no chao e para os overlays
    D3D11_BLEND_DESC blendAlphaDesc = {};
    blendAlphaDesc.RenderTarget[0].BlendEnable = TRUE;
    blendAlphaDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendAlphaDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendAlphaDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    // Alfa em "source-over" de verdade (a + d*(1-a)), e nao apenas o alfa da
    // origem: com fundo opaco o resultado continua opaco, e com fundo
    // transparente (exportacao com alfa) a sombra e o chao contribuem o alfa
    // certo. Com DEST_ALPHA = ZERO, a area da sombra saia semitransparente na
    // imagem exportada mesmo sem transparencia pedida.
    blendAlphaDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendAlphaDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendAlphaDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendAlphaDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_device->CreateBlendState(&blendAlphaDesc, &m_blendAlpha);

    // Constant buffers
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    cbDesc.ByteWidth = sizeof(FrameConstants);
    m_device->CreateBuffer(&cbDesc, nullptr, &m_frameCB);

    cbDesc.ByteWidth = sizeof(MaterialConstants);
    m_device->CreateBuffer(&cbDesc, nullptr, &m_materialCB);

    cbDesc.ByteWidth = sizeof(OverlayConstants);
    m_device->CreateBuffer(&cbDesc, nullptr, &m_overlayCB);

    if (!CreateOverlayResources()) return false;

    return true;
}

bool Renderer::CompileShaders()
{
    // Caminho dos shaders: pasta "shaders" ao lado do executavel.
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring mainShaderPath = dir + L"\\shaders\\MainShader.hlsl";
    std::wstring wireShaderPath = dir + L"\\shaders\\WireShader.hlsl";
    std::wstring overlayShaderPath = dir + L"\\shaders\\Overlay.hlsl";

    auto vsBlob = CompileShaderFromFile(mainShaderPath, "VS_Main", "vs_5_0");
    auto psBlob = CompileShaderFromFile(mainShaderPath, "PS_Main", "ps_5_0");
    auto vsShadowBlob = CompileShaderFromFile(mainShaderPath, "VS_Shadow", "vs_5_0");
    auto vsGroundBlob = CompileShaderFromFile(mainShaderPath, "VS_Ground", "vs_5_0");
    auto psGroundBlob = CompileShaderFromFile(mainShaderPath, "PS_Ground", "ps_5_0");
    auto vsGridBlob = CompileShaderFromFile(mainShaderPath, "VS_Grid", "vs_5_0");
    auto psGridBlob = CompileShaderFromFile(mainShaderPath, "PS_Grid", "ps_5_0");
    auto vsWireBlob = CompileShaderFromFile(wireShaderPath, "VS_Wire", "vs_5_0");
    auto psWireBlob = CompileShaderFromFile(wireShaderPath, "PS_Wire", "ps_5_0");
    auto vsOverlayBlob = CompileShaderFromFile(overlayShaderPath, "VS_Overlay", "vs_5_0");
    auto vsUvMeshBlob = CompileShaderFromFile(overlayShaderPath, "VS_UvMesh", "vs_5_0");
    auto psSolidBlob = CompileShaderFromFile(overlayShaderPath, "PS_OverlaySolid", "ps_5_0");
    auto psSpriteBlob = CompileShaderFromFile(overlayShaderPath, "PS_OverlaySprite", "ps_5_0");
    auto psBallBlob = CompileShaderFromFile(overlayShaderPath, "PS_GizmoBall", "ps_5_0");

    if (!vsBlob || !psBlob || !vsShadowBlob || !vsGroundBlob || !psGroundBlob ||
        !vsGridBlob || !psGridBlob ||
        !vsWireBlob || !psWireBlob || !vsOverlayBlob || !vsUvMeshBlob ||
        !psSolidBlob || !psSpriteBlob || !psBallBlob)
        return false;

    m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vsMain);
    m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_psMain);
    m_device->CreateVertexShader(vsShadowBlob->GetBufferPointer(), vsShadowBlob->GetBufferSize(), nullptr, &m_vsShadow);
    m_device->CreateVertexShader(vsGroundBlob->GetBufferPointer(), vsGroundBlob->GetBufferSize(), nullptr, &m_vsGround);
    m_device->CreatePixelShader(psGroundBlob->GetBufferPointer(), psGroundBlob->GetBufferSize(), nullptr, &m_psGround);
    m_device->CreateVertexShader(vsGridBlob->GetBufferPointer(), vsGridBlob->GetBufferSize(), nullptr, &m_vsGrid);
    m_device->CreatePixelShader(psGridBlob->GetBufferPointer(), psGridBlob->GetBufferSize(), nullptr, &m_psGrid);
    m_device->CreateVertexShader(vsWireBlob->GetBufferPointer(), vsWireBlob->GetBufferSize(), nullptr, &m_vsWire);
    m_device->CreatePixelShader(psWireBlob->GetBufferPointer(), psWireBlob->GetBufferSize(), nullptr, &m_psWire);
    m_device->CreateVertexShader(vsOverlayBlob->GetBufferPointer(), vsOverlayBlob->GetBufferSize(), nullptr, &m_vsOverlay);
    m_device->CreateVertexShader(vsUvMeshBlob->GetBufferPointer(), vsUvMeshBlob->GetBufferSize(), nullptr, &m_vsUvMesh);
    m_device->CreatePixelShader(psSolidBlob->GetBufferPointer(), psSolidBlob->GetBufferSize(), nullptr, &m_psOverlaySolid);
    m_device->CreatePixelShader(psSpriteBlob->GetBufferPointer(), psSpriteBlob->GetBufferSize(), nullptr, &m_psOverlaySprite);
    m_device->CreatePixelShader(psBallBlob->GetBufferPointer(), psBallBlob->GetBufferSize(), nullptr, &m_psGizmoBall);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    HRESULT hr = m_device->CreateInputLayout(layout, ARRAYSIZE(layout),
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);
    if (FAILED(hr)) return false;

    D3D11_INPUT_ELEMENT_DESC overlayLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = m_device->CreateInputLayout(overlayLayout, ARRAYSIZE(overlayLayout),
        vsOverlayBlob->GetBufferPointer(), vsOverlayBlob->GetBufferSize(), &m_overlayLayout);
    return SUCCEEDED(hr);
}

bool Renderer::CreateShadowResources()
{
    // Textura de profundidade typeless: usada como DSV no passo de sombra e
    // como SRV (R32_FLOAT) na amostragem do shader principal.
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = kShadowMapSize;
    texDesc.Height = kShadowMapSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_shadowTex);
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = m_device->CreateDepthStencilView(m_shadowTex.Get(), &dsvDesc, &m_shadowDSV);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = m_device->CreateShaderResourceView(m_shadowTex.Get(), &srvDesc, &m_shadowSRV);
    return SUCCEEDED(hr);
}

bool Renderer::CreateGroundPlane()
{
    // Quad unitario no plano XZ centrado na origem (escalado via matriz World)
    Vertex verts[6] = {
        { XMFLOAT3(-0.5f, 0, -0.5f), XMFLOAT3(0, 1, 0), XMFLOAT2(0, 0) },
        { XMFLOAT3(-0.5f, 0,  0.5f), XMFLOAT3(0, 1, 0), XMFLOAT2(0, 1) },
        { XMFLOAT3( 0.5f, 0, -0.5f), XMFLOAT3(0, 1, 0), XMFLOAT2(1, 0) },
        { XMFLOAT3( 0.5f, 0, -0.5f), XMFLOAT3(0, 1, 0), XMFLOAT2(1, 0) },
        { XMFLOAT3(-0.5f, 0,  0.5f), XMFLOAT3(0, 1, 0), XMFLOAT2(0, 1) },
        { XMFLOAT3( 0.5f, 0,  0.5f), XMFLOAT3(0, 1, 0), XMFLOAT2(1, 1) },
    };

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(verts);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = { verts };
    return SUCCEEDED(m_device->CreateBuffer(&desc, &data, &m_groundVB));
}

bool Renderer::CreateGrid()
{
    // Grid unitario no plano XZ, de -1 a 1, com uma linha a cada 1/20. A
    // matriz World escala isso para o tamanho do modelo. As linhas centrais
    // recebem a cor do eixo correspondente (X vermelho, Z azul), como no
    // Blender; as demais sao cinza, mais claras a cada 5 divisoes.
    constexpr int kDivisions = 20; // por lado, a partir do centro
    constexpr float kStep = 1.0f / (float)kDivisions;

    // O RGB aqui e so o "peso" da linha; o tom final vem do tint que a CPU
    // escolhe conforme o fundo (ver o passo da grade em DrawSceneInternal).
    const uint32_t minor = PackColorRgba(1.0f, 1.0f, 1.0f, 0.35f);
    const uint32_t major = PackColorRgba(1.0f, 1.0f, 1.0f, 0.60f);
    const uint32_t axisX = PackColorRgba(kAxisColors[0].x, kAxisColors[0].y, kAxisColors[0].z, 0.95f);
    const uint32_t axisZ = PackColorRgba(kAxisColors[2].x, kAxisColors[2].y, kAxisColors[2].z, 0.95f);

    std::vector<Vertex> lines;
    lines.reserve((size_t)(kDivisions * 2 + 1) * 4);

    auto addLine = [&lines](const XMFLOAT3& a, const XMFLOAT3& b, uint32_t color)
    {
        Vertex v0{ a, XMFLOAT3(0, 1, 0), XMFLOAT2(0, 0), color };
        Vertex v1{ b, XMFLOAT3(0, 1, 0), XMFLOAT2(0, 0), color };
        lines.push_back(v0);
        lines.push_back(v1);
    };

    for (int i = -kDivisions; i <= kDivisions; i++)
    {
        float t = (float)i * kStep;
        uint32_t color = (i == 0) ? axisZ : ((i % 5 == 0) ? major : minor);
        addLine(XMFLOAT3(t, 0, -1.0f), XMFLOAT3(t, 0, 1.0f), color); // paralela a Z

        color = (i == 0) ? axisX : ((i % 5 == 0) ? major : minor);
        addLine(XMFLOAT3(-1.0f, 0, t), XMFLOAT3(1.0f, 0, t), color); // paralela a X
    }

    m_gridVertexCount = (UINT)lines.size();

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (UINT)(lines.size() * sizeof(Vertex));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = { lines.data() };
    return SUCCEEDED(m_device->CreateBuffer(&desc, &data, &m_gridVB));
}

bool Renderer::CreateOverlayResources()
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = kOverlayMaxVerts * sizeof(OverlayVertex);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_overlayVB)))
        return false;

    m_overlayVerts.reserve(kOverlayMaxVerts);
    return CreateGizmoAtlas();
}

// ---------------------------------------------------------------------------
// Atlas do gizmo. As formas sao desenhadas com GDI num DIB ampliado e depois
// reduzidas por media (antialias) para o tamanho final. Guardamos duas
// mascaras em canais separados: R = forma (disco ou anel), G = letra.
// ---------------------------------------------------------------------------
bool Renderer::CreateGizmoAtlas()
{
    const int atlasW = kAtlasCellPixels * kAtlasCellCount;
    const int atlasH = kAtlasCellPixels;
    const int bigW = atlasW * kAtlasSupersample;
    const int bigH = atlasH * kAtlasSupersample;
    const int bigCell = kAtlasCellPixels * kAtlasSupersample;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bigW;
    bmi.bmiHeader.biHeight = -bigH; // negativo = linhas de cima para baixo
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screenDC);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screenDC);

    if (!dc || !dib || !bits)
    {
        if (dib) DeleteObject(dib);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, dib);

    const size_t pixelCount = (size_t)atlasW * atlasH;
    std::vector<uint8_t> shapeMask(pixelCount, 0);
    std::vector<uint8_t> letterMask(pixelCount, 0);

    // Reduz o DIB para a resolucao final tirando a media de cada bloco.
    auto downsample = [&](std::vector<uint8_t>& out)
    {
        GdiFlush();
        const uint8_t* src = (const uint8_t*)bits;
        const int stride = bigW * 4;
        const int block = kAtlasSupersample * kAtlasSupersample;
        for (int y = 0; y < atlasH; y++)
        {
            for (int x = 0; x < atlasW; x++)
            {
                unsigned sum = 0;
                for (int sy = 0; sy < kAtlasSupersample; sy++)
                {
                    const uint8_t* row = src + (size_t)(y * kAtlasSupersample + sy) * stride
                        + (size_t)(x * kAtlasSupersample) * 4;
                    for (int sx = 0; sx < kAtlasSupersample; sx++)
                        sum += row[sx * 4]; // canal azul; as formas sao cinza
                }
                out[(size_t)y * atlasW + x] = (uint8_t)(sum / block);
            }
        }
    };

    auto clearBlack = [&]()
    {
        RECT full = { 0, 0, bigW, bigH };
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &full, black);
        DeleteObject(black);
    };

    // ---- Passo 1: formas ----
    clearBlack();
    {
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        const int radius = bigCell / 2 - kAtlasSupersample; // ~1 px de folga
        const int rimThickness = (int)(2.5f * kAtlasSupersample);

        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        HBRUSH dim = CreateSolidBrush(RGB(64, 64, 64)); // miolo dos eixos negativos

        for (int cell = 0; cell < kAtlasCellCount; cell++)
        {
            int cx = cell * bigCell + bigCell / 2;
            int cy = bigCell / 2;

            SelectObject(dc, white);
            Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);

            if (cell == 3) // anel dos eixos negativos: miolo translucido
            {
                int inner = radius - rimThickness;
                SelectObject(dc, dim);
                Ellipse(dc, cx - inner, cy - inner, cx + inner, cy + inner);
            }
        }

        SelectObject(dc, oldPen);
        DeleteObject(white);
        DeleteObject(dim);
    }
    downsample(shapeMask);

    // ---- Passo 2: letras X / Y / Z ----
    clearBlack();
    {
        LOGFONTW lf = {};
        lf.lfHeight = -(int)(bigCell * 0.62f);
        lf.lfWeight = FW_BOLD;
        lf.lfQuality = ANTIALIASED_QUALITY;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        HFONT font = CreateFontIndirectW(&lf);
        HGDIOBJ oldFont = SelectObject(dc, font);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));

        const wchar_t* letters[3] = { L"X", L"Y", L"Z" };
        for (int cell = 0; cell < 3; cell++)
        {
            RECT r = { cell * bigCell, 0, (cell + 1) * bigCell, bigCell };
            DrawTextW(dc, letters[cell], 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        }

        SelectObject(dc, oldFont);
        DeleteObject(font);
    }
    downsample(letterMask);

    SelectObject(dc, oldBitmap);
    DeleteObject(dib);
    DeleteDC(dc);

    // ---- Combina nos canais R (forma) e G (letra) ----
    std::vector<uint8_t> rgba(pixelCount * 4);
    for (size_t i = 0; i < pixelCount; i++)
    {
        rgba[i * 4 + 0] = shapeMask[i];
        // A letra so existe onde ha forma; recortar evita halo nas bordas.
        rgba[i * 4 + 1] = (uint8_t)((letterMask[i] * shapeMask[i]) / 255);
        rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = 255;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = atlasW;
    texDesc.Height = atlasH;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = atlasW * 4;

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(m_device->CreateTexture2D(&texDesc, &initData, &texture)))
        return false;
    return SUCCEEDED(m_device->CreateShaderResourceView(texture.Get(), nullptr, &m_gizmoAtlas));
}

bool Renderer::CreateSwapChainForWindow(HWND hwnd, UINT width, UINT height,
    ComPtr<IDXGISwapChain>& outSwapChain, ComPtr<ID3D11RenderTargetView>& outRTV,
    ComPtr<ID3D11DepthStencilView>& outDSV, ComPtr<ID3D11Texture2D>& outDepthTex)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    m_device.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);
    ComPtr<IDXGIFactory> factory;
    adapter->GetParent(__uuidof(IDXGIFactory), &factory);

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 2;
    scDesc.BufferDesc.Width = width;
    scDesc.BufferDesc.Height = height;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Numerator = 60;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.Windowed = TRUE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = factory->CreateSwapChain(m_device.Get(), &scDesc, &outSwapChain);
    if (FAILED(hr)) return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    outSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &outRTV);

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    m_device->CreateTexture2D(&depthDesc, nullptr, &outDepthTex);
    m_device->CreateDepthStencilView(outDepthTex.Get(), nullptr, &outDSV);

    return true;
}

void Renderer::ResizeSwapChain(ComPtr<IDXGISwapChain>& swapChain, UINT width, UINT height,
    ComPtr<ID3D11RenderTargetView>& outRTV, ComPtr<ID3D11DepthStencilView>& outDSV,
    ComPtr<ID3D11Texture2D>& outDepthTex)
{
    if (!swapChain || width == 0 || height == 0) return;

    // Solta as referencias antigas antes do ResizeBuffers (exigencia do DXGI)
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    outRTV.Reset();
    outDSV.Reset();
    outDepthTex.Reset();

    swapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

    ComPtr<ID3D11Texture2D> backBuffer;
    swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &outRTV);

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    m_device->CreateTexture2D(&depthDesc, nullptr, &outDepthTex);
    m_device->CreateDepthStencilView(outDepthTex.Get(), nullptr, &outDSV);
}

GpuTexture Renderer::CreateTextureFromWicSource(IWICBitmapSource* source)
{
    GpuTexture result;
    if (!source) return result;

    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory))))
        return result;

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(&converter))) return result;
    if (FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return result;

    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);
    if (width == 0 || height == 0) return result;

    std::vector<BYTE> pixels((size_t)width * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, (UINT)pixels.size(), pixels.data())))
        return result;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = width * 4;

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(m_device->CreateTexture2D(&texDesc, &initData, &texture))) return result;
    if (FAILED(m_device->CreateShaderResourceView(texture.Get(), nullptr, &result.srv))) return result;

    result.width = width;
    result.height = height;
    return result;
}

GpuTexture Renderer::LoadTexture(const MaterialData& material)
{
    // Usamos o WIC (Windows Imaging Component), que ja vem com o Windows,
    // para decodificar PNG/JPG/BMP/TIFF sem precisar de bibliotecas externas.
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory))))
        return GpuTexture();

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;

    if (!material.textureBytes.empty())
    {
        // Textura embutida (GLB / data URI): decodifica direto da memoria.
        // O IWICStream nao copia os bytes, entao o vetor precisa continuar
        // vivo — e continua, pois pertence ao SceneModel do chamador.
        Microsoft::WRL::ComPtr<IWICStream> stream;
        if (FAILED(wicFactory->CreateStream(&stream))) return GpuTexture();
        if (FAILED(stream->InitializeFromMemory(
            const_cast<BYTE*>(material.textureBytes.data()),
            (DWORD)material.textureBytes.size())))
            return GpuTexture();
        if (FAILED(wicFactory->CreateDecoderFromStream(stream.Get(), nullptr,
            WICDecodeMetadataCacheOnDemand, &decoder)))
            return GpuTexture();
    }
    else if (!material.texturePath.empty())
    {
        if (FAILED(wicFactory->CreateDecoderFromFilename(material.texturePath.c_str(), nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
            return GpuTexture(); // arquivo nao encontrado ou formato nao suportado
    }
    else
    {
        return GpuTexture();
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return GpuTexture();
    return CreateTextureFromWicSource(frame.Get());
}

bool Renderer::UploadModel(const SceneModel& model, GpuModel& outGpu)
{
    if (model.vertices.empty() || model.indices.empty()) return false;

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = (UINT)(model.vertices.size() * sizeof(Vertex));
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = { model.vertices.data() };
    HRESULT hr = m_device->CreateBuffer(&vbDesc, &vbData, &outGpu.vertexBuffer);
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = (UINT)(model.indices.size() * sizeof(UINT));
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData = { model.indices.data() };
    hr = m_device->CreateBuffer(&ibDesc, &ibData, &outGpu.indexBuffer);
    if (FAILED(hr)) return false;

    outGpu.vertexCount = (UINT)model.vertices.size();
    outGpu.indexCount = (UINT)model.indices.size();
    outGpu.subMeshes = model.subMeshes;

    outGpu.materialTextures.resize(model.materials.size());
    for (size_t i = 0; i < model.materials.size(); i++)
        outGpu.materialTextures[i] = LoadTexture(model.materials[i]);

    return true;
}

// ---------------------------------------------------------------------------
// Geometria 2D acumulada (gizmo e aba de UV)
// ---------------------------------------------------------------------------
void Renderer::OverlayBegin()
{
    m_overlayVerts.clear();
}

void Renderer::OverlayQuad(float cx, float cy, float halfW, float halfH,
    const XMFLOAT4& uvRect, const XMFLOAT4& color)
{
    if (m_overlayVerts.size() + 6 > kOverlayMaxVerts) return;

    // Atencao aos eixos: em NDC o Y cresce para cima, mas em espaco de textura
    // o V cresce para baixo. Por isso o canto de baixo do quad recebe o V do
    // fim do retangulo (uvRect.w) e o canto de cima recebe o do inicio
    // (uvRect.y) — inverter isso deixa a imagem (e as letras do gizmo) de
    // cabeca para baixo.
    const OverlayVertex corners[4] = {
        { XMFLOAT2(cx - halfW, cy - halfH), XMFLOAT2(uvRect.x, uvRect.w), color },
        { XMFLOAT2(cx + halfW, cy - halfH), XMFLOAT2(uvRect.z, uvRect.w), color },
        { XMFLOAT2(cx + halfW, cy + halfH), XMFLOAT2(uvRect.z, uvRect.y), color },
        { XMFLOAT2(cx - halfW, cy + halfH), XMFLOAT2(uvRect.x, uvRect.y), color },
    };
    const int order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i : order)
        m_overlayVerts.push_back(corners[i]);
}

void Renderer::OverlayLine(float x0, float y0, float x1, float y1, float halfWidth,
    const XMFLOAT4& uvRect, const XMFLOAT4& color)
{
    if (m_overlayVerts.size() + 6 > kOverlayMaxVerts) return;

    float dx = x1 - x0, dy = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 1e-6f) return;
    // Perpendicular unitaria. So faz sentido em viewport quadrado (o gizmo),
    // onde uma unidade de NDC vale o mesmo em x e y.
    float px = -dy / length * halfWidth;
    float py = dx / length * halfWidth;

    const OverlayVertex corners[4] = {
        { XMFLOAT2(x0 + px, y0 + py), XMFLOAT2(uvRect.x, uvRect.y), color },
        { XMFLOAT2(x1 + px, y1 + py), XMFLOAT2(uvRect.z, uvRect.y), color },
        { XMFLOAT2(x1 - px, y1 - py), XMFLOAT2(uvRect.z, uvRect.w), color },
        { XMFLOAT2(x0 - px, y0 - py), XMFLOAT2(uvRect.x, uvRect.w), color },
    };
    const int order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i : order)
        m_overlayVerts.push_back(corners[i]);
}

void Renderer::WriteOverlayConstants(const XMFLOAT4& transform, const XMFLOAT4& tint)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_context->Map(m_overlayCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    OverlayConstants* constants = (OverlayConstants*)mapped.pData;
    constants->Transform = transform;
    constants->Tint = tint;
    m_context->Unmap(m_overlayCB.Get(), 0);
}

void Renderer::OverlayFlush(ID3D11PixelShader* ps, ID3D11ShaderResourceView* srv)
{
    if (m_overlayVerts.empty()) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_context->Map(m_overlayVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    memcpy(mapped.pData, m_overlayVerts.data(), m_overlayVerts.size() * sizeof(OverlayVertex));
    m_context->Unmap(m_overlayVB.Get(), 0);

    UINT stride = sizeof(OverlayVertex);
    UINT offset = 0;
    m_context->IASetInputLayout(m_overlayLayout.Get());
    m_context->IASetVertexBuffers(0, 1, m_overlayVB.GetAddressOf(), &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vsOverlay.Get(), nullptr, 0);
    m_context->PSSetShader(ps, nullptr, 0);
    m_context->PSSetShaderResources(0, 1, &srv);
    m_context->Draw((UINT)m_overlayVerts.size(), 0);

    m_overlayVerts.clear();
}

// ---------------------------------------------------------------------------
// Gizmo de orientacao
// ---------------------------------------------------------------------------
RECT Renderer::GizmoBoxRect(UINT vpWidth, UINT vpHeight)
{
    RECT box = {};
    if ((int)vpWidth < kGizmoBoxSize + 2 * kGizmoMargin ||
        (int)vpHeight < kGizmoBoxSize + 2 * kGizmoMargin)
        return box; // viewport pequeno demais: nao desenha

    box.right = (LONG)vpWidth - kGizmoMargin;
    box.left = box.right - kGizmoBoxSize;
    box.top = kGizmoMargin;
    box.bottom = box.top + kGizmoBoxSize;
    return box;
}

void Renderer::ComputeGizmoAxes(const OrbitCamera& camera, GizmoAxisPoint out[6])
{
    const XMFLOAT3 directions[6] = {
        XMFLOAT3(1, 0, 0), XMFLOAT3(-1, 0, 0),
        XMFLOAT3(0, 1, 0), XMFLOAT3(0, -1, 0),
        XMFLOAT3(0, 0, 1), XMFLOAT3(0, 0, -1),
    };

    XMMATRIX view = camera.GetViewMatrix();
    for (int i = 0; i < 6; i++)
    {
        XMVECTOR world = XMLoadFloat3(&directions[i]);
        XMFLOAT3 inView;
        XMStoreFloat3(&inView, XMVector3TransformNormal(world, view));

        out[i].direction = directions[i];
        out[i].ndcX = inView.x * kGizmoAxisLengthNdc;
        out[i].ndcY = inView.y * kGizmoAxisLengthNdc;
        out[i].depth = inView.z; // maior = mais longe da camera
        out[i].axis = i / 2;
        out[i].positive = (i % 2 == 0);
    }
}

bool Renderer::HitTestGizmo(const OrbitCamera& camera, UINT vpWidth, UINT vpHeight,
    int mouseX, int mouseY, XMFLOAT3& outDirection)
{
    RECT box = GizmoBoxRect(vpWidth, vpHeight);
    if (box.right <= box.left) return false;
    if (mouseX < box.left || mouseX >= box.right || mouseY < box.top || mouseY >= box.bottom)
        return false;

    float half = (float)kGizmoBoxSize * 0.5f;
    float ndcX = ((float)mouseX - (float)box.left - half) / half;
    float ndcY = -((float)mouseY - (float)box.top - half) / half; // NDC tem Y para cima

    GizmoAxisPoint axes[6];
    ComputeGizmoAxes(camera, axes);

    int best = -1;
    float bestDepth = 0.0f;
    for (int i = 0; i < 6; i++)
    {
        float dx = ndcX - axes[i].ndcX;
        float dy = ndcY - axes[i].ndcY;
        if (dx * dx + dy * dy > kGizmoBallRadiusNdc * kGizmoBallRadiusNdc) continue;
        // Empate: fica com a esfera mais proxima da camera (menor depth)
        if (best < 0 || axes[i].depth < bestDepth)
        {
            best = i;
            bestDepth = axes[i].depth;
        }
    }
    if (best < 0) return false;

    outDirection = axes[best].direction;
    return true;
}

void Renderer::DrawOrientationGizmo(const OrbitCamera& camera, UINT width, UINT height)
{
    RECT box = GizmoBoxRect(width, height);
    if (box.right <= box.left || !m_gizmoAtlas) return;

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = (float)box.left;
    vp.TopLeftY = (float)box.top;
    vp.Width = (float)(box.right - box.left);
    vp.Height = (float)(box.bottom - box.top);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    m_context->RSSetState(m_rsOverlay.Get());
    m_context->OMSetDepthStencilState(m_dsDisabled.Get(), 0);
    m_context->OMSetBlendState(m_blendAlpha.Get(), nullptr, 0xFFFFFFFF);
    m_context->PSSetSamplers(0, 1, m_samplerClamp.GetAddressOf());

    WriteOverlayConstants(XMFLOAT4(1, 1, 0, 0), XMFLOAT4(1, 1, 1, 1));
    m_context->VSSetConstantBuffers(0, 1, m_overlayCB.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_overlayCB.GetAddressOf());

    GizmoAxisPoint axes[6];
    ComputeGizmoAxes(camera, axes);

    // 1) Hastes dos eixos positivos, do centro ate a esfera.
    const float lineHalfWidth = 1.6f / ((float)kGizmoBoxSize * 0.5f);
    OverlayBegin();
    for (int i = 0; i < 6; i++)
    {
        if (!axes[i].positive) continue;
        OverlayLine(0.0f, 0.0f, axes[i].ndcX, axes[i].ndcY, lineHalfWidth,
            AtlasCell(0), kAxisColors[axes[i].axis]);
    }
    OverlayFlush(m_psOverlaySolid.Get(), nullptr);

    // 2) Esferas, da mais distante para a mais proxima (algoritmo do pintor).
    int order[6] = { 0, 1, 2, 3, 4, 5 };
    std::sort(std::begin(order), std::end(order),
        [&axes](int a, int b) { return axes[a].depth > axes[b].depth; });

    OverlayBegin();
    for (int index : order)
    {
        const GizmoAxisPoint& point = axes[index];
        XMFLOAT4 cell = point.positive ? AtlasCell(point.axis) : AtlasCell(3);
        OverlayQuad(point.ndcX, point.ndcY, kGizmoBallRadiusNdc, kGizmoBallRadiusNdc,
            cell, kAxisColors[point.axis]);
    }
    OverlayFlush(m_psGizmoBall.Get(), m_gizmoAtlas.Get());

    // Restaura o viewport cheio para quem desenhar depois.
    D3D11_VIEWPORT full = {};
    full.Width = (float)width;
    full.Height = (float)height;
    full.MinDepth = 0.0f;
    full.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &full);
}

void Renderer::ClearTarget(ID3D11RenderTargetView* rtv, UINT width, UINT height, const XMFLOAT3& color)
{
    if (!rtv) return;
    const float clear[4] = { color.x, color.y, color.z, 1.0f };
    m_context->OMSetRenderTargets(1, &rtv, nullptr);
    m_context->ClearRenderTargetView(rtv, clear);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
}

void Renderer::RenderScene(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
    UINT width, UINT height,
    const GpuModel& gpu, const SceneModel& cpuModel, const OrbitCamera& camera,
    ShadingMode mode, bool drawWireframe, bool drawShadows,
    const LightingState& lighting)
{
    DrawSceneInternal(rtv, dsv, width, height, gpu, cpuModel, camera,
        mode, drawWireframe, drawShadows, lighting, /*transparentBackground=*/false);

    // O gizmo e ajuda de navegacao na tela — nao entra na imagem exportada.
    DrawOrientationGizmo(camera, width, height);
}

bool Renderer::RenderToImage(UINT width, UINT height,
    const GpuModel& gpu, const SceneModel& cpuModel, const OrbitCamera& camera,
    ShadingMode mode, bool drawWireframe, bool drawShadows,
    const LightingState& lighting, bool transparentBackground,
    std::vector<uint8_t>& outBgra)
{
    outBgra.clear();
    if (width == 0 || height == 0) return false;
    // D3D11 garante ate 16384 em feature level 11; alem disso a alocacao
    // tambem passaria de 1 GB.
    if (width > 16384 || height > 16384) return false;

    D3D11_TEXTURE2D_DESC targetDesc = {};
    targetDesc.Width = width;
    targetDesc.Height = height;
    targetDesc.MipLevels = 1;
    targetDesc.ArraySize = 1;
    // BGRA para os pixels sairem na ordem que o WIC e os DIBs esperam.
    targetDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    targetDesc.SampleDesc.Count = 1;
    targetDesc.Usage = D3D11_USAGE_DEFAULT;
    targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    ComPtr<ID3D11Texture2D> target;
    if (FAILED(m_device->CreateTexture2D(&targetDesc, nullptr, &target))) return false;

    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(m_device->CreateRenderTargetView(target.Get(), nullptr, &rtv))) return false;

    D3D11_TEXTURE2D_DESC depthDesc = targetDesc;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTex;
    if (FAILED(m_device->CreateTexture2D(&depthDesc, nullptr, &depthTex))) return false;

    ComPtr<ID3D11DepthStencilView> dsv;
    if (FAILED(m_device->CreateDepthStencilView(depthTex.Get(), nullptr, &dsv))) return false;

    DrawSceneInternal(rtv.Get(), dsv.Get(), width, height, gpu, cpuModel, camera,
        mode, drawWireframe, drawShadows, lighting, transparentBackground);

    // Copia para uma textura legivel pela CPU
    D3D11_TEXTURE2D_DESC stagingDesc = targetDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_device->CreateTexture2D(&stagingDesc, nullptr, &staging))) return false;

    m_context->CopyResource(staging.Get(), target.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    const size_t rowBytes = (size_t)width * 4;
    outBgra.resize(rowBytes * height);
    for (UINT y = 0; y < height; y++)
    {
        memcpy(outBgra.data() + y * rowBytes,
            (const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch, rowBytes);
    }
    m_context->Unmap(staging.Get(), 0);

    // O alvo fora da tela some aqui; devolve o pipeline a um estado neutro
    // para o proximo desenho na janela.
    ID3D11RenderTargetView* nullRtv = nullptr;
    m_context->OMSetRenderTargets(1, &nullRtv, nullptr);
    return true;
}

void Renderer::DrawSceneInternal(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
    UINT width, UINT height,
    const GpuModel& gpu, const SceneModel& cpuModel, const OrbitCamera& camera,
    ShadingMode mode, bool drawWireframe, bool drawShadows,
    const LightingState& lighting, bool transparentBackground)
{
    if (!gpu.vertexBuffer || !gpu.indexBuffer) return;

    // ---- Matrizes comuns ----
    XMFLOAT3 eye = camera.GetEyePosition();
    XMMATRIX view = camera.GetViewMatrix();
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.01f, 10000.0f);
    XMMATRIX world = XMMatrixIdentity();

    // Direcao da luz principal a partir do azimute (dial de rotacao) e da
    // elevacao do tema. "toLight" aponta do modelo para a luz; a direcao de
    // viagem da luz e o oposto.
    float az = XMConvertToRadians(lighting.rotationDeg);
    float el = XMConvertToRadians(lighting.elevationDeg);
    XMFLOAT3 lightDirNorm(
        -cosf(el) * sinf(az),
        -sinf(el),
        -cosf(el) * cosf(az));
    XMVECTOR lightDirVec = XMVector3Normalize(XMLoadFloat3(&lightDirNorm));
    XMStoreFloat3(&lightDirNorm, lightDirVec);

    // Frustum ortografico da luz, ajustado ao bounding box do modelo
    XMFLOAT3 mn = cpuModel.boundsMin;
    XMFLOAT3 mx = cpuModel.boundsMax;
    XMFLOAT3 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
    float sx = mx.x - mn.x, sy = mx.y - mn.y, sz = mx.z - mn.z;
    float radius = 0.5f * sqrtf(sx * sx + sy * sy + sz * sz);
    if (radius < 0.001f) radius = 1.0f;

    XMVECTOR centerVec = XMLoadFloat3(&center);
    XMVECTOR lightEye = XMVectorSubtract(centerVec, XMVectorScale(lightDirVec, radius * 2.5f));
    XMMATRIX lightView = XMMatrixLookAtLH(lightEye, centerVec, XMVectorSet(0, 1, 0, 0));
    float orthoSize = radius * 3.4f; // cobre o modelo + area de sombra no chao
    XMMATRIX lightProj = XMMatrixOrthographicLH(orthoSize, orthoSize, 0.1f, radius * 6.0f);
    XMMATRIX lightViewProj = XMMatrixMultiply(lightView, lightProj);

    // Preenche o frame constant buffer (mesma funcao p/ todos os passos,
    // variando so a matriz World).
    auto writeFrameCB = [&](const XMMATRIX& worldM)
    {
        D3D11_MAPPED_SUBRESOURCE m;
        m_context->Map(m_frameCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        FrameConstants* fc = (FrameConstants*)m.pData;
        fc->World = XMMatrixTranspose(worldM);
        fc->View = XMMatrixTranspose(view);
        fc->Projection = XMMatrixTranspose(proj);
        fc->LightViewProj = XMMatrixTranspose(lightViewProj);
        fc->LightDirection = lightDirNorm;
        fc->ShadowsEnabled = drawShadows ? 1 : 0;
        fc->CameraPosition = eye;
        fc->AmbientIntensity = lighting.ambientIntensity;
        fc->MainLightColor = lighting.mainLightColor;
        fc->MainLightIntensity = lighting.mainLightIntensity;
        fc->AmbientColor = lighting.ambientColor;
        fc->ShadowSoftness = lighting.shadowSoftness;
        for (int i = 0; i < 3; i++)
        {
            XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&lighting.aux[i].direction));
            XMFLOAT3 dn;
            XMStoreFloat3(&dn, d);
            fc->AuxDir[i] = XMFLOAT4(dn.x, dn.y, dn.z, lighting.aux[i].enabled ? 1.0f : 0.0f);
            fc->AuxColor[i] = XMFLOAT4(lighting.aux[i].color.x, lighting.aux[i].color.y,
                lighting.aux[i].color.z, lighting.aux[i].intensity);
        }
        fc->GroundColor = lighting.groundColor;
        fc->GroundOpacity = lighting.groundOpacity;
        m_context->Unmap(m_frameCB.Get(), 0);
    };

    writeFrameCB(world);
    m_context->VSSetConstantBuffers(0, 1, m_frameCB.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_frameCB.GetAddressOf());

    // ---- Estado comum de input assembly ----
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, gpu.vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(gpu.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->IASetInputLayout(m_inputLayout.Get());

    // ---- PASSO 1: shadow map (profundidade do ponto de vista da luz) ----
    if (drawShadows)
    {
        // Desvincula o shadow map do slot t1 antes de usa-lo como DSV
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        m_context->PSSetShaderResources(0, 2, nullSRVs);

        m_context->OMSetRenderTargets(0, nullptr, m_shadowDSV.Get());
        m_context->ClearDepthStencilView(m_shadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

        D3D11_VIEWPORT shadowVP = {};
        shadowVP.Width = (float)kShadowMapSize;
        shadowVP.Height = (float)kShadowMapSize;
        shadowVP.MinDepth = 0.0f;
        shadowVP.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &shadowVP);

        m_context->VSSetShader(m_vsShadow.Get(), nullptr, 0);
        m_context->PSSetShader(nullptr, nullptr, 0); // depth-only
        m_context->RSSetState(m_rsShadow.Get());
        m_context->OMSetDepthStencilState(m_dsState.Get(), 0);

        m_context->DrawIndexed(gpu.indexCount, 0, 0);
    }

    // ---- PASSO 2: cena principal ----
    // Fundo transparente (exportacao com alfa): limpa com alfa 0 e deixa a
    // malha, o chao e a sombra escreverem o alfa deles.
    float clearColor[4] = { lighting.background.x, lighting.background.y, lighting.background.z, 1.0f };
    if (transparentBackground)
        clearColor[0] = clearColor[1] = clearColor[2] = clearColor[3] = 0.0f;
    m_context->OMSetRenderTargets(1, &rtv, dsv);
    m_context->ClearRenderTargetView(rtv, clearColor);
    m_context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    m_context->VSSetShader(m_vsMain.Get(), nullptr, 0);
    m_context->PSSetShader(m_psMain.Get(), nullptr, 0);
    m_context->RSSetState(m_rsSolid.Get());
    m_context->OMSetDepthStencilState(m_dsState.Get(), 0);
    m_context->OMSetBlendState(m_blendOpaque.Get(), nullptr, 0xFFFFFFFF);

    ID3D11SamplerState* samplers[2] = { m_samplerLinear.Get(), m_samplerShadow.Get() };
    m_context->PSSetSamplers(0, 2, samplers);
    m_context->PSSetShaderResources(1, 1, m_shadowSRV.GetAddressOf());

    for (const SubMesh& sm : gpu.subMeshes)
    {
        D3D11_MAPPED_SUBRESOURCE mappedMat;
        m_context->Map(m_materialCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedMat);
        MaterialConstants* mc = (MaterialConstants*)mappedMat.pData;
        mc->UseMaterial = (mode == ShadingMode::Material) ? 1 : 0;

        ID3D11ShaderResourceView* srv = nullptr;
        if (sm.materialIndex >= 0 && sm.materialIndex < (int)cpuModel.materials.size())
        {
            mc->DiffuseColor = cpuModel.materials[sm.materialIndex].diffuseColor;
            if (sm.materialIndex < (int)gpu.materialTextures.size() &&
                gpu.materialTextures[sm.materialIndex].Valid())
            {
                srv = gpu.materialTextures[sm.materialIndex].srv.Get();
                mc->HasTexture = 1;
            }
            else
            {
                mc->HasTexture = 0;
            }
        }
        else
        {
            mc->DiffuseColor = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
            mc->HasTexture = 0;
        }
        m_context->Unmap(m_materialCB.Get(), 0);
        m_context->PSSetConstantBuffers(1, 1, m_materialCB.GetAddressOf());
        m_context->PSSetShaderResources(0, 1, &srv);

        m_context->DrawIndexed(sm.indexCount, sm.indexStart, 0);
    }

    // ---- PASSO 3: plano de chao (cor do chao e/ou sombra projetada) ----
    if (drawShadows || lighting.groundOpacity > 0.0f)
    {
        XMMATRIX groundWorld = XMMatrixMultiply(
            XMMatrixScaling(radius * 5.0f, 1.0f, radius * 5.0f),
            XMMatrixTranslation(center.x, mn.y, center.z));

        writeFrameCB(groundWorld);

        m_context->IASetVertexBuffers(0, 1, m_groundVB.GetAddressOf(), &stride, &offset);
        m_context->VSSetShader(m_vsGround.Get(), nullptr, 0);
        m_context->PSSetShader(m_psGround.Get(), nullptr, 0);
        m_context->OMSetBlendState(m_blendAlpha.Get(), nullptr, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_dsNoWrite.Get(), 0);
        m_context->Draw(6, 0);

        m_context->OMSetBlendState(m_blendOpaque.Get(), nullptr, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_dsState.Get(), 0);
    }

    // ---- PASSO 4: grade do chao (opcional) ----
    if (lighting.showGrid && m_gridVertexCount > 0)
    {
        // Levantada um fio acima do plano de chao para os dois nao brigarem
        // no z-buffer quando ambos estao visiveis.
        XMMATRIX gridWorld = XMMatrixMultiply(
            XMMatrixScaling(radius * 5.0f, 1.0f, radius * 5.0f),
            XMMatrixTranslation(center.x, mn.y + radius * 0.002f, center.z));

        writeFrameCB(gridWorld);

        // Tom das linhas conforme o fundo (ou o chao, quando visivel): claro
        // sobre superficie escura e escuro sobre superficie clara, senao a
        // grade some por falta de contraste.
        {
            const XMFLOAT3& surface = (lighting.groundOpacity > 0.5f)
                ? lighting.groundColor : lighting.background;
            const float luminance = 0.2126f * surface.x + 0.7152f * surface.y + 0.0722f * surface.z;
            const XMFLOAT4 tint = (luminance < 0.5f)
                ? XMFLOAT4(0.95f, 0.95f, 1.00f, 1.0f)
                : XMFLOAT4(0.20f, 0.20f, 0.26f, 1.0f);

            D3D11_MAPPED_SUBRESOURCE mappedGrid;
            if (SUCCEEDED(m_context->Map(m_materialCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedGrid)))
            {
                MaterialConstants* mc = (MaterialConstants*)mappedGrid.pData;
                mc->DiffuseColor = tint;
                mc->HasTexture = 0;
                mc->UseMaterial = 0;
                m_context->Unmap(m_materialCB.Get(), 0);
            }
            m_context->PSSetConstantBuffers(1, 1, m_materialCB.GetAddressOf());
        }

        m_context->IASetVertexBuffers(0, 1, m_gridVB.GetAddressOf(), &stride, &offset);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        m_context->VSSetShader(m_vsGrid.Get(), nullptr, 0);
        m_context->PSSetShader(m_psGrid.Get(), nullptr, 0);
        m_context->RSSetState(m_rsSolid.Get());
        m_context->OMSetBlendState(m_blendAlpha.Get(), nullptr, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_dsNoWrite.Get(), 0);
        m_context->Draw(m_gridVertexCount, 0);

        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->OMSetBlendState(m_blendOpaque.Get(), nullptr, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_dsState.Get(), 0);
    }

    // Restaura o buffer da malha e World = identidade para o wireframe
    m_context->IASetVertexBuffers(0, 1, gpu.vertexBuffer.GetAddressOf(), &stride, &offset);
    writeFrameCB(world);

    // ---- PASSO 5: overlay de wireframe (opcional) ----
    if (drawWireframe)
    {
        m_context->VSSetShader(m_vsWire.Get(), nullptr, 0);
        m_context->PSSetShader(m_psWire.Get(), nullptr, 0);
        m_context->RSSetState(m_rsWireframe.Get());
        m_context->DrawIndexed(gpu.indexCount, 0, 0);
    }
}

void Renderer::RenderUvView(ID3D11RenderTargetView* rtv, UINT width, UINT height,
    const GpuModel& gpu, const SceneModel& cpuModel, const UvViewState& uv)
{
    if (!rtv) return;

    const float clear[4] = { 0.13f, 0.13f, 0.15f, 1.0f };
    m_context->OMSetRenderTargets(1, &rtv, nullptr);
    m_context->ClearRenderTargetView(rtv, clear);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    if (!gpu.vertexBuffer || !gpu.indexBuffer || width == 0 || height == 0) return;

    // Textura do material selecionado, se houver.
    const GpuTexture* texture = nullptr;
    if (uv.materialIndex >= 0 && uv.materialIndex < (int)gpu.materialTextures.size() &&
        gpu.materialTextures[uv.materialIndex].Valid())
        texture = &gpu.materialTextures[uv.materialIndex];

    // O quadrado de UV [0,1] e exibido com a proporcao da imagem (como nos
    // editores de UV), e cabe inteiro no viewport com uma margem.
    float imageAspect = texture
        ? (float)texture->width / (float)std::max(1u, texture->height)
        : 1.0f;
    float viewportAspect = (float)width / (float)height;

    const float margin = 0.86f;
    float halfY = std::min(margin, margin * viewportAspect / imageAspect);
    float halfX = halfY * imageAspect / viewportAspect;
    halfX *= uv.zoom;
    halfY *= uv.zoom;

    const XMFLOAT4 transform(halfX, halfY, uv.panX, uv.panY);

    m_context->RSSetState(m_rsOverlay.Get());
    m_context->OMSetDepthStencilState(m_dsDisabled.Get(), 0);
    m_context->OMSetBlendState(m_blendAlpha.Get(), nullptr, 0xFFFFFFFF);
    m_context->PSSetSamplers(0, 1, m_samplerClamp.GetAddressOf());
    m_context->VSSetConstantBuffers(0, 1, m_overlayCB.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_overlayCB.GetAddressOf());

    const XMFLOAT4 white(1, 1, 1, 1);
    const XMFLOAT4 fullCell(0, 0, 1, 1);

    // 1) Fundo do quadrado de UV — deixa a area util visivel mesmo sem textura.
    WriteOverlayConstants(transform, white);
    OverlayBegin();
    OverlayQuad(uv.panX, uv.panY, halfX, halfY, fullCell, XMFLOAT4(0.20f, 0.20f, 0.23f, 1.0f));
    OverlayFlush(m_psOverlaySolid.Get(), nullptr);

    // 2) Imagem de textura
    if (texture)
    {
        OverlayBegin();
        OverlayQuad(uv.panX, uv.panY, halfX, halfY, fullCell, white);
        OverlayFlush(m_psOverlaySprite.Get(), texture->srv.Get());
    }

    // 3) Borda do quadrado de UV (1 px de cada lado)
    {
        float thicknessX = 1.0f / (float)width;
        float thicknessY = 1.0f / (float)height;
        const XMFLOAT4 borderColor(0.55f, 0.55f, 0.60f, 1.0f);
        OverlayBegin();
        OverlayQuad(uv.panX, uv.panY - halfY, halfX, thicknessY, fullCell, borderColor);
        OverlayQuad(uv.panX, uv.panY + halfY, halfX, thicknessY, fullCell, borderColor);
        OverlayQuad(uv.panX - halfX, uv.panY, thicknessX, halfY, fullCell, borderColor);
        OverlayQuad(uv.panX + halfX, uv.panY, thicknessX, halfY, fullCell, borderColor);
        OverlayFlush(m_psOverlaySolid.Get(), nullptr);
    }

    // 4) Desenho das UVs da malha, em wireframe. As submeshes do material
    //    selecionado saem em destaque; as demais ficam esmaecidas.
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetVertexBuffers(0, 1, gpu.vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(gpu.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vsUvMesh.Get(), nullptr, 0);
    m_context->PSSetShader(m_psOverlaySolid.Get(), nullptr, 0);
    m_context->RSSetState(m_rsWireframe.Get());

    const XMFLOAT4 dimLines(0.45f, 0.50f, 0.55f, 0.55f);
    const XMFLOAT4 activeLines(0.20f, 0.95f, 0.45f, 0.95f);

    for (int pass = 0; pass < 2; pass++)
    {
        const bool active = (pass == 1);
        WriteOverlayConstants(transform, active ? activeLines : dimLines);
        for (const SubMesh& sub : gpu.subMeshes)
        {
            if ((sub.materialIndex == uv.materialIndex) != active) continue;
            m_context->DrawIndexed(sub.indexCount, sub.indexStart, 0);
        }
    }

    (void)cpuModel;
}

void Renderer::EndFrame(IDXGISwapChain* swapChain)
{
    swapChain->Present(1, 0);
}
