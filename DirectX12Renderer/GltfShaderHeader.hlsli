struct Output
{
    float4 svpos : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 tpos : TPOS;
};

// 輪郭線パス用（位置だけあればよい）
struct OutlineOutput
{
    float4 svpos : SV_POSITION;
};

cbuffer SceneBuffer : register(b0)
{
    matrix view;
    matrix proj;
    matrix lightCamera;
    float3 eye;
    float _pad0;
    float3 lightVec;
    float shadowMapTexel;
    float lightRange;
    float3 _pad1;
};

Texture2D<float4> baseColorTex : register(t0);
SamplerState smp : register(s0);

cbuffer Transform : register(b1)
{
    matrix world;
    matrix bones[256];
};

cbuffer Material : register(b2)
{
    float4 baseColorFactor;
};

Texture2D<float> shadowMap : register(t1);
SamplerComparisonState shadowSmp : register(s1);