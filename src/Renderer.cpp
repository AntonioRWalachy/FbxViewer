#include "Renderer.h"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

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

    // Direcao fixa da luz principal (normalizada no uso)
    const XMFLOAT3 kLightDir = XMFLOAT3(0.4f, -0.8f, 0.5f);
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

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    m_device->CreateDepthStencilState(&dsDesc, &m_dsState);

    D3D11_DEPTH_STENCIL_DESC dsNoWriteDesc = dsDesc;
    dsNoWriteDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; // chao nao escreve depth
    m_device->CreateDepthStencilState(&dsNoWriteDesc, &m_dsNoWrite);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&sampDesc, &m_samplerLinear);

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

    // Alpha blend para a sombra translucida no chao
    D3D11_BLEND_DESC blendAlphaDesc = {};
    blendAlphaDesc.RenderTarget[0].BlendEnable = TRUE;
    blendAlphaDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendAlphaDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendAlphaDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendAlphaDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendAlphaDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
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

    auto vsBlob = CompileShaderFromFile(mainShaderPath, "VS_Main", "vs_5_0");
    auto psBlob = CompileShaderFromFile(mainShaderPath, "PS_Main", "ps_5_0");
    auto vsShadowBlob = CompileShaderFromFile(mainShaderPath, "VS_Shadow", "vs_5_0");
    auto vsGroundBlob = CompileShaderFromFile(mainShaderPath, "VS_Ground", "vs_5_0");
    auto psGroundBlob = CompileShaderFromFile(mainShaderPath, "PS_Ground", "ps_5_0");
    auto vsWireBlob = CompileShaderFromFile(wireShaderPath, "VS_Wire", "vs_5_0");
    auto psWireBlob = CompileShaderFromFile(wireShaderPath, "PS_Wire", "ps_5_0");

    if (!vsBlob || !psBlob || !vsShadowBlob || !vsGroundBlob || !psGroundBlob || !vsWireBlob || !psWireBlob)
        return false;

    m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vsMain);
    m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_psMain);
    m_device->CreateVertexShader(vsShadowBlob->GetBufferPointer(), vsShadowBlob->GetBufferSize(), nullptr, &m_vsShadow);
    m_device->CreateVertexShader(vsGroundBlob->GetBufferPointer(), vsGroundBlob->GetBufferSize(), nullptr, &m_vsGround);
    m_device->CreatePixelShader(psGroundBlob->GetBufferPointer(), psGroundBlob->GetBufferSize(), nullptr, &m_psGround);
    m_device->CreateVertexShader(vsWireBlob->GetBufferPointer(), vsWireBlob->GetBufferSize(), nullptr, &m_vsWire);
    m_device->CreatePixelShader(psWireBlob->GetBufferPointer(), psWireBlob->GetBufferSize(), nullptr, &m_psWire);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    HRESULT hr = m_device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);
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

ComPtr<ID3D11ShaderResourceView> Renderer::LoadTexture(const std::wstring& path)
{
    if (path.empty()) return nullptr;

    // Usamos o WIC (Windows Imaging Component), que ja vem com o Windows,
    // para decodificar PNG/JPG/BMP/TIFF sem precisar de bibliotecas externas.
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return nullptr; // textura nao encontrada ou formato nao suportado

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return nullptr;

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return nullptr;

    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);
    if (width == 0 || height == 0) return nullptr;

    std::vector<BYTE> pixels(width * height * 4);
    hr = converter->CopyPixels(nullptr, width * 4, (UINT)pixels.size(), pixels.data());
    if (FAILED(hr)) return nullptr;

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
    hr = m_device->CreateTexture2D(&texDesc, &initData, &texture);
    if (FAILED(hr)) return nullptr;

    ComPtr<ID3D11ShaderResourceView> srv;
    hr = m_device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
    if (FAILED(hr)) return nullptr;

    return srv;
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
        outGpu.materialTextures[i] = LoadTexture(model.materials[i].texturePath);

    return true;
}

void Renderer::RenderScene(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv,
    UINT width, UINT height,
    const GpuModel& gpu, const SceneModel& cpuModel, const OrbitCamera& camera,
    ShadingMode mode, bool drawWireframe, bool drawShadows)
{
    if (!gpu.vertexBuffer || !gpu.indexBuffer) return;

    // ---- Matrizes comuns ----
    XMFLOAT3 eye = camera.GetEyePosition();
    XMMATRIX view = camera.GetViewMatrix();
    float aspect = (height > 0) ? (float)width / (float)height : 1.0f;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.01f, 10000.0f);
    XMMATRIX world = XMMatrixIdentity();

    // Frustum ortografico da luz, ajustado ao bounding box do modelo
    XMFLOAT3 mn = cpuModel.boundsMin;
    XMFLOAT3 mx = cpuModel.boundsMax;
    XMFLOAT3 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
    float sx = mx.x - mn.x, sy = mx.y - mn.y, sz = mx.z - mn.z;
    float radius = 0.5f * sqrtf(sx * sx + sy * sy + sz * sz);
    if (radius < 0.001f) radius = 1.0f;

    XMVECTOR lightDirVec = XMVector3Normalize(XMLoadFloat3(&kLightDir));
    XMVECTOR centerVec = XMLoadFloat3(&center);
    XMVECTOR lightEye = XMVectorSubtract(centerVec, XMVectorScale(lightDirVec, radius * 2.5f));
    XMMATRIX lightView = XMMatrixLookAtLH(lightEye, centerVec, XMVectorSet(0, 1, 0, 0));
    float orthoSize = radius * 3.4f; // cobre o modelo + area de sombra no chao
    XMMATRIX lightProj = XMMatrixOrthographicLH(orthoSize, orthoSize, 0.1f, radius * 6.0f);
    XMMATRIX lightViewProj = XMMatrixMultiply(lightView, lightProj);

    XMFLOAT3 lightDirNorm;
    XMStoreFloat3(&lightDirNorm, lightDirVec);

    // ---- Atualiza o frame constant buffer (usado por TODOS os passos) ----
    D3D11_MAPPED_SUBRESOURCE mapped;
    m_context->Map(m_frameCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    FrameConstants* fc = (FrameConstants*)mapped.pData;
    fc->World = XMMatrixTranspose(world);
    fc->View = XMMatrixTranspose(view);
    fc->Projection = XMMatrixTranspose(proj);
    fc->LightViewProj = XMMatrixTranspose(lightViewProj);
    fc->LightDirection = lightDirNorm;
    fc->ShadowsEnabled = drawShadows ? 1 : 0;
    fc->CameraPosition = eye;
    m_context->Unmap(m_frameCB.Get(), 0);
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
    float clearColor[4] = { 0.18f, 0.18f, 0.20f, 1.0f }; // cinza escuro, como o viewer nativo
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
            if (sm.materialIndex < (int)gpu.materialTextures.size() && gpu.materialTextures[sm.materialIndex])
            {
                srv = gpu.materialTextures[sm.materialIndex].Get();
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

    // ---- PASSO 3: plano de chao com sombra projetada ----
    if (drawShadows)
    {
        // Reaproveita o frameCB trocando so a matriz World (escala/posicao do quad)
        XMMATRIX groundWorld = XMMatrixMultiply(
            XMMatrixScaling(radius * 5.0f, 1.0f, radius * 5.0f),
            XMMatrixTranslation(center.x, mn.y, center.z));

        m_context->Map(m_frameCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        fc = (FrameConstants*)mapped.pData;
        fc->World = XMMatrixTranspose(groundWorld);
        fc->View = XMMatrixTranspose(view);
        fc->Projection = XMMatrixTranspose(proj);
        fc->LightViewProj = XMMatrixTranspose(lightViewProj);
        fc->LightDirection = lightDirNorm;
        fc->ShadowsEnabled = 1;
        fc->CameraPosition = eye;
        m_context->Unmap(m_frameCB.Get(), 0);

        m_context->IASetVertexBuffers(0, 1, m_groundVB.GetAddressOf(), &stride, &offset);
        m_context->VSSetShader(m_vsGround.Get(), nullptr, 0);
        m_context->PSSetShader(m_psGround.Get(), nullptr, 0);
        m_context->OMSetBlendState(m_blendAlpha.Get(), nullptr, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_dsNoWrite.Get(), 0);
        m_context->Draw(6, 0);

        // Restaura estado e frameCB com World = identidade p/ o wireframe
        m_context->OMSetBlendState(m_blendOpaque.Get(), nullptr, 0xFFFFFFFF);
        m_context->OMSetDepthStencilState(m_dsState.Get(), 0);
        m_context->IASetVertexBuffers(0, 1, gpu.vertexBuffer.GetAddressOf(), &stride, &offset);

        m_context->Map(m_frameCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        fc = (FrameConstants*)mapped.pData;
        fc->World = XMMatrixTranspose(world);
        fc->View = XMMatrixTranspose(view);
        fc->Projection = XMMatrixTranspose(proj);
        fc->LightViewProj = XMMatrixTranspose(lightViewProj);
        fc->LightDirection = lightDirNorm;
        fc->ShadowsEnabled = drawShadows ? 1 : 0;
        fc->CameraPosition = eye;
        m_context->Unmap(m_frameCB.Get(), 0);
    }

    // ---- PASSO 4: overlay de wireframe (opcional) ----
    if (drawWireframe)
    {
        m_context->VSSetShader(m_vsWire.Get(), nullptr, 0);
        m_context->PSSetShader(m_psWire.Get(), nullptr, 0);
        m_context->RSSetState(m_rsWireframe.Get());
        m_context->DrawIndexed(gpu.indexCount, 0, 0);
    }
}

void Renderer::EndFrame(IDXGISwapChain* swapChain)
{
    swapChain->Present(1, 0);
}
