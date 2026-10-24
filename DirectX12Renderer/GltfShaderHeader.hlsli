struct Output
{
    float4 svpos : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

// 輪郭線パス用（位置だけあればよい）
struct OutlineOutput
{
    float4 svpos : SV_POSITION;
};

// 1 体分のインスタンスデータ
struct InstanceData
{
    matrix world;
    uint boneOffset;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

cbuffer SceneBuffer : register(b0)
{
    matrix view;
    matrix viewProj;
    matrix lightCamera[3];
    float3 eye;
    float _pad0;
    float3 lightVec;
    float shadowMapTexel; // 1 / 2048（全カスケード共通）
    float4 cascadeRadius; // x, y = 分割距離（ビュー空間 Z）
    float4 cascadeNormalOffset; // x, y, z = 押し出し量（m）
    float4 shadowCenter;
    float lightRange;
    float3 _pad1;
};

Texture2D<float4> baseColorTex : register(t0);
SamplerState smp : register(s0);

cbuffer Material : register(b2)
{
    float4 baseColorFactor;
};

Texture2DArray<float> shadowMap : register(t1);
StructuredBuffer<InstanceData> g_instances : register(t2);
StructuredBuffer<uint> g_visible : register(t4);
StructuredBuffer<matrix> g_bones : register(t3);
SamplerComparisonState shadowSmp : register(s1);