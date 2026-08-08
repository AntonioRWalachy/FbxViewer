// Shader principal + passo de sombra + plano de chao + grade.
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
    float  MainLightIntensity;
    float3 AmbientColor;
    float  ShadowSoftness;  // espacamento das amostras do PCF, em texels
    float4 AuxDir[3];       // xyz = direcao, w = 1 se habilitada
    float4 AuxColor[3];     // rgb = cor, w = intensidade
    float3 GroundColor;
    float  GroundOpacity;   // 0 = chao invisivel (so a sombra aparece)
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
// Sombra: PCF 7x7 do shadow map. Retorna 1 = iluminado, 0 = sombra.
// O espacamento das amostras vem de ShadowSoftness: quanto maior, mais larga a
// penumbra. Com 49 amostras a borda fica bem mais macia que o 3x3 anterior, e
// o custo e irrelevante porque o app so redesenha quando algo muda.
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

    const float bias = 0.0022f;
    const float texel = 1.0f / 2048.0f;
    float step = texel * max(ShadowSoftness, 0.25f);

    float shadow = 0.0f;
    float totalWeight = 0.0f;
    [unroll]
    for (int x = -3; x <= 3; x++)
    {
        [unroll]
        for (int y = -3; y <= 3; y++)
        {
            // Peso maior no centro (kernel triangular): evita o aspecto de
            // "bloco" que um PCF de peso uniforme produz quando é largo.
            float weight = (4.0f - abs((float)x)) * (4.0f - abs((float)y));
            shadow += weight * ShadowMap.SampleCmpLevelZero(ShadowSampler,
                uv + float2(x, y) * step, proj.z - bias);
            totalWeight += weight;
        }
    }
    return shadow / totalWeight;
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

    // Acumulacao de luz: ambiente + luz principal (afetada pela sombra e
    // girada pelo dial) + ate 3 luzes auxiliares opcionais. Cada uma tem cor
    // e intensidade proprias, editaveis na barra lateral.
    float3 lightAccum = AmbientColor * AmbientIntensity;
    lightAccum += MainLightColor * (ndotl * 0.85f * MainLightIntensity * shadowFactor);

    [unroll]
    for (int i = 0; i < 3; i++)
    {
        float enabled = AuxDir[i].w;
        float3 Laux = normalize(-AuxDir[i].xyz);
        float nd = saturate(dot(N, Laux));
        lightAccum += AuxColor[i].rgb * (nd * 0.45f * AuxColor[i].w * enabled);
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
// Plano de chao. Com GroundOpacity = 0 ele e invisivel e so a sombra
// projetada aparece (comportamento do visualizador 3D nativo); acima disso o
// chao ganha a cor escolhida pelo usuario, com a sombra por cima.
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
    float shadowAlpha = (1.0f - shadowFactor) * 0.45f;
    float floorAlpha = saturate(GroundOpacity);

    // Composicao "sombra preta sobre o chao", depois o conjunto sobre o fundo.
    // O blend do pipeline e SrcAlpha/InvSrcAlpha, entao devolvemos a cor NAO
    // pre-multiplicada.
    float outAlpha = shadowAlpha + floorAlpha * (1.0f - shadowAlpha);
    if (outAlpha <= 0.0001f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float3 outColor = GroundColor * floorAlpha * (1.0f - shadowAlpha) / outAlpha;
    return float4(outColor, outAlpha);
}

// ---------------------------------------------------------------------------
// Grade do chao. A geometria e um grid unitario no plano XZ (-1..1) escalado
// pela matriz World. A cor do vertice diz o "peso" de cada linha (comum, a
// cada 5 divisoes, ou eixo) e DiffuseColor — vindo do cbuffer de material —
// traz o tom escolhido pela CPU conforme o fundo, para a grade nunca sumir
// por falta de contraste. O alfa some nas bordas para a grade nao terminar
// num corte reto.
// ---------------------------------------------------------------------------
struct PSGridInput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
    float2 LocalXZ  : TEXCOORD0;
};

PSGridInput VS_Grid(VSInput input)
{
    PSGridInput output;
    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    float4 viewPos = mul(worldPos, View);
    output.Position = mul(viewPos, Projection);
    output.Color = input.Color;
    // A posicao local (de -1 a 1) vai interpolada para o pixel shader: o
    // desvanecimento PRECISA ser calculado la. Cada linha da grade tem so
    // dois vertices, os dois na borda do grid — calcular o fade por vertice
    // daria zero nas duas pontas e a linha inteira sumiria.
    output.LocalXZ = input.Position.xz;
    return output;
}

float4 PS_Grid(PSGridInput input) : SV_TARGET
{
    float distanceFromCenter = length(input.LocalXZ);
    float fade = smoothstep(1.0f, 0.6f, distanceFromCenter);
    return float4(input.Color.rgb * DiffuseColor.rgb,
                  input.Color.a * DiffuseColor.a * fade);
}
