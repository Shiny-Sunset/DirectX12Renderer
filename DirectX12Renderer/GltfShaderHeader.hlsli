struct Output
{
    float4 svpos : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
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

cbuffer Material : register(b1)
{
    float4 baseColorFactor;
};