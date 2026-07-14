// Shader simples usado para o overlay de wireframe (linhas em cor solida).
// O cbuffer FrameConstants ESPELHA a struct de mesmo nome em Renderer.h.

cbuffer FrameConstants : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    matrix LightViewProj;
    float3 LightDirection;
    int    ShadowsEnabled;
    float3 CameraPosition;
    float  AmbientIntensity;
    float3 MainLightColor;
    float  _pad0;
    float3 AmbientColor;
    float  _pad1;
    float4 AuxDir[3];   // xyz = direcao, w = 1 se habilitada
    float4 AuxColor[3]; // rgb da luz auxiliar
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 UV       : TEXCOORD0;
};

struct PSInput
{
    float4 Position : SV_POSITION;
};

PSInput VS_Wire(VSInput input)
{
    PSInput output;
    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);
    return output;
}

float4 PS_Wire(PSInput input) : SV_TARGET
{
    return float4(0.05f, 0.85f, 0.25f, 1.0f); // verde, como o visualizador 3D nativo
}
