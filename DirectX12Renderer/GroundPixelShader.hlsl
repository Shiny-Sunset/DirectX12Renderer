#include "GroundShaderHeader.hlsli"

float4 GroundPS(GroundOutput input) : SV_TARGET
{
    static const float ShadowBias = 0.005;

    // -- 格子模様（元のコード） --
    float2 uv = input.worldPos.xz;
    float2 width = fwidth(uv);
    float2 grid = abs(frac(uv - 0.5) - 0.5) / max(width, 1e-5);
    float lineMask = saturate(min(grid.x, grid.y));

    float3 lineColor = float3(0.30, 0.30, 0.35);
    float3 baseColor = float3(0.55, 0.55, 0.60);
    float3 color = lerp(lineColor, baseColor, lineMask);

    // -- 影の判定 --
    float3 posFromLight = input.tpos.xyz / input.tpos.w;
    float2 shadowUV = (posFromLight.xy + float2(1, -1)) * float2(0.5, -0.5);

    // 比較とフィルタリングを GPU がまとめて行う
    // 戻り値は「光が当たっている割合」で、0〜1 の中間値になる
    float lit = shadowMap.SampleCmpLevelZero(shadowSmp, shadowUV, posFromLight.z - ShadowBias);

    float shadowWeight = lerp(0.5, 1.0, lit);

    return float4(color * shadowWeight, 1.0);
}