cbuffer SceneBuffer : register(b0)
{
    matrix view;
    matrix proj;
    matrix lightCamera;
    float3 eye;
    float _pad0;
    float3 lightVec;
    float _pad1;
};

cbuffer TransformBuffer : register(b1)
{
    matrix world; // 箱ごとに差し替わる（ルート定数で渡される）
};

Texture2D<float> shadowMap : register(t0); // 深度なので 1 成分
SamplerComparisonState shadowSmp : register(s0);

struct StageOutput
{
    float4 svpos : SV_POSITION;
    float3 worldPos : POSITION; // 格子模様を描くのに使う
    float3 normal : NORMAL;
    float4 tpos : TPOS; // 光源から見た位置
};