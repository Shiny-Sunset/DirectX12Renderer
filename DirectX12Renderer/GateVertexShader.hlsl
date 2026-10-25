#include "GateShaderHeader.hlsli"

StructuredBuffer<float4> g_gates : register(t0); // xyz = 位置, w = 半径

GateOutput GateVS(float2 corner : POSITION, uint id : SV_InstanceID)
{
    const float4 g = g_gates[id];

    // 四角を XZ 平面に寝かせて置く。corner は -1〜1
    float3 world = g.xyz + float3(corner.x, 0.0, corner.y) * g.w;
    world.y += 0.02; // 地面と同じ高さだと Z ファイティングでチラつく

    GateOutput o;
    o.svpos = mul(viewProj, float4(world, 1.0));
    o.uv = corner; // そのまま中心からの座標として使う
    return o;
}