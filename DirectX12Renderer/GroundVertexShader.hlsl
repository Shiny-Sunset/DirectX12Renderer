cbuffer SceneBuffer : register(b0)
{
    matrix view;
    matrix proj;
    float3 eye;
};

struct GroundOutput
{
    float4 svpos : SV_POSITION;
    float3 worldPos : POSITION; // 格子模様を描くのに使う
};

GroundOutput GroundVS(float4 pos : POSITION)
{
    GroundOutput output;
    output.svpos = mul(mul(proj, view), pos); // ワールド行列は単位行列なので掛けない
    output.worldPos = pos.xyz;
    return output;
}