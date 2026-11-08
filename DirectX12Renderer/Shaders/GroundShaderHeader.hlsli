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
    float time;
    float2 _pad1;
};

Texture2DArray<float> shadowMap : register(t0); // 深度なので 1 成分
SamplerComparisonState shadowSmp : register(s0);

struct GroundOutput
{
    float4 svpos : SV_POSITION;
    float3 worldPos : POSITION; // 格子模様を描くのに使う
};