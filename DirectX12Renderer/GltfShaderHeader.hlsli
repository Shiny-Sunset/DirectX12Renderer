struct Output
{
    float4 svpos : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

// 輪郭線パス用（位置だけあればよい）
struct OutlineOutput
{
    float4 svpos : SV_POSITION;
};

cbuffer SceneBuffer : register(b0)
{
    matrix world;
    matrix view;
    matrix proj;
    float3 eye;
};

Texture2D<float4> baseColorTex : register(t0);
SamplerState smp : register(s0);

cbuffer Transform : register(b1)
{
    matrix bones[256];
};

cbuffer Material : register(b2)
{
    float4 baseColorFactor;
};