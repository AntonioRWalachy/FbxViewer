// Passos 2D desenhados por cima da cena: o gizmo de orientacao no canto do
// viewport e a aba de visualizacao de UV.
// O cbuffer ESPELHA a struct OverlayConstants em Renderer.h.

cbuffer OverlayConstants : register(b0)
{
    float4 Transform; // xy = meia-extensao em NDC, zw = deslocamento
    float4 Tint;      // multiplica a cor final
};

Texture2D OverlayTexture : register(t0);
SamplerState OverlaySampler : register(s0);

// Vertices ja calculados em NDC pela CPU (gizmo, quads da aba de UV)
struct OverlayVSInput
{
    float2 Position : POSITION;
    float2 UV       : TEXCOORD0;
    float4 Color    : COLOR0;
};

// Vertices da malha do modelo, mesmo layout do shader principal
struct MeshVSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 UV       : TEXCOORD0;
    float4 Color    : COLOR0;
};

struct OverlayPSInput
{
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
    float4 Color    : COLOR0;
};

OverlayPSInput VS_Overlay(OverlayVSInput input)
{
    OverlayPSInput output;
    output.Position = float4(input.Position, 0.0f, 1.0f);
    output.UV = input.UV;
    output.Color = input.Color * Tint;
    return output;
}

// Desenha a malha no espaco de UV: a coordenada de textura vira a posicao.
// A origem da UV no DirectX e o canto superior esquerdo, entao V cresce para
// baixo — o mesmo mapeamento usado para exibir a imagem de textura, o que
// mantem o desenho das UVs alinhado com ela.
OverlayPSInput VS_UvMesh(MeshVSInput input)
{
    OverlayPSInput output;
    float2 square = float2(input.UV.x * 2.0f - 1.0f, 1.0f - input.UV.y * 2.0f);
    output.Position = float4(square * Transform.xy + Transform.zw, 0.0f, 1.0f);
    output.UV = input.UV;
    output.Color = Tint;
    return output;
}

float4 PS_OverlaySolid(OverlayPSInput input) : SV_TARGET
{
    return input.Color;
}

float4 PS_OverlaySprite(OverlayPSInput input) : SV_TARGET
{
    return OverlayTexture.Sample(OverlaySampler, input.UV) * input.Color;
}

// Esferas do gizmo. O atlas guarda duas mascaras em canais separados:
//   R = cobertura da forma (disco cheio ou anel)  -> vira o alfa
//   G = cobertura da letra X/Y/Z                  -> escurece o preenchimento
float4 PS_GizmoBall(OverlayPSInput input) : SV_TARGET
{
    float2 mask = OverlayTexture.Sample(OverlaySampler, input.UV).rg;
    float3 rgb = lerp(input.Color.rgb, float3(0.10f, 0.10f, 0.12f), mask.g);
    return float4(rgb, mask.r * input.Color.a);
}
