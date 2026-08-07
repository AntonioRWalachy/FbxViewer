// Shader principal + passo de sombra + plano de chao.
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

cbuffer MaterialConstants : register(b1)
{
    float4 DiffuseColor;
    int    HasTexture;
    int    UseMaterial; // 0 = modo "sem material" (cinza solido), 1 = modo "com material"
    float2 _padding2;
};

Texture2D DiffuseTexture : register(t0);
Texture2D ShadowMap : register(t1);
SamplerState SamplerLinear : register(s0);
SamplerComparisonState ShadowSampler : register(s1);

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 UV       : TEXCOORD0;
    float4 Color    : COLOR0; // cor por vertice (PLY / glTF COLOR_0); branca por padrao
};

struct PSInput
{
    float4 Position  : SV_POSITION;
    float3 Normal    : NORMAL;
    float2 UV        : TEXCOORD0;
    float4 ShadowPos : TEXCOORD1;
    float4 Color     : COLOR0;
};

// ---------------------------------------------------------------------------
// Sombra: amostragem PCF 3x3 do shadow map. Retorna 1 = iluminado, 0 = sombra.
// ---------------------------------------------------------------------------
float ComputeShadow(float4 shadowPos)
{
    if (ShadowsEnabled == 0)
        return 1.0f;

    float3 proj = shadowPos.xyz / shadowPos.w;
    float2 uv = proj.xy * float2(0.5f, -0.5f) + 0.5f;

    // Fora do frustum da luz = sem sombra
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f || proj.z > 1.0f)
        return 1.0f;

    const float bias = 0.0018f;
    const float texel = 1.0f / 2048.0f;
    float shadow = 0.0f;
    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            shadow += ShadowMap.SampleCmpLevelZero(ShadowSampler,
                uv + float2(x, y) * texel, proj.z - bias);
        }
    }
    return shadow / 9.0f;
}

// ---------------------------------------------------------------------------
// Passo principal (malha com/sem material)
// ---------------------------------------------------------------------------
PSInput VS_Main(VSInput input)
{
    PSInput output;
    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);
    output.ShadowPos = mul(worldPos, LightViewProj);

    float3x3 worldRot = (float3x3)World;
    output.Normal = normalize(mul(input.Normal, worldRot));
    output.UV = input.UV;
    output.Color = input.Color;
    return output;
}

float4 PS_Main(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.Normal);
    float3 L = normalize(-LightDirection);
    float ndotl = saturate(dot(N, L));

    float shadowFactor = ComputeShadow(input.ShadowPos);

    // Acumulacao de luz: ambiente do tema + luz principal (afetada pela
    // sombra e girada pelo dial) + ate 3 luzes auxiliares opcionais.
    float3 lightAccum = AmbientColor * AmbientIntensity;
    lightAccum += MainLightColor * (ndotl * 0.85f * shadowFactor);

    [unroll]
    for (int i = 0; i < 3; i++)
    {
        float enabled = AuxDir[i].w;
        float3 Laux = normalize(-AuxDir[i].xyz);
        float nd = saturate(dot(N, Laux));
        lightAccum += AuxColor[i].rgb * (nd * 0.45f * enabled);
    }
    float3 lighting3 = saturate(lightAccum);

    float4 baseColor;
    if (UseMaterial)
    {
        baseColor = DiffuseColor * input.Color;
        if (HasTexture)
        {
            float4 texColor = DiffuseTexture.Sample(SamplerLinear, input.UV);
            baseColor *= texColor;
        }
    }
    else
    {
        // Modo "sem material": cinza neutro, so com shading
        baseColor = float4(0.75f, 0.75f, 0.78f, 1.0f);
    }

    float3 finalColor = baseColor.rgb * lighting3;
    return float4(finalColor, baseColor.a);
}

// ---------------------------------------------------------------------------
// Passo de profundidade do ponto de vista da luz (gera o shadow map).
// Sem pixel shader: so a profundidade importa.
// ---------------------------------------------------------------------------
float4 VS_Shadow(VSInput input) : SV_POSITION
{
    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    return mul(worldPos, LightViewProj);
}

// ---------------------------------------------------------------------------
// Plano de chao: invisivel, exceto onde recebe sombra (escurece com alpha),
// como no visualizador 3D nativo do Windows.
// ---------------------------------------------------------------------------
struct PSGroundInput
{
    float4 Position  : SV_POSITION;
    float4 ShadowPos : TEXCOORD0;
};

PSGroundInput VS_Ground(VSInput input)
{
    PSGroundInput output;
    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);
    output.ShadowPos = mul(worldPos, LightViewProj);
    return output;
}

float4 PS_Ground(PSGroundInput input) : SV_TARGET
{
    float shadowFactor = ComputeShadow(input.ShadowPos);
    float darkness = (1.0f - shadowFactor) * 0.45f;
    return float4(0.0f, 0.0f, 0.0f, darkness);
}
