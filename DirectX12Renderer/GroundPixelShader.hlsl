#include "GroundShaderHeader.hlsli"
#include "ShadowCommon.hlsli"

float4 GroundPS(GroundOutput input) : SV_TARGET
{
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

    // 地面は常に真上を向いている
    float brightness = ComputeBrightness(float3(0, 1, 0), lightVec, input.tpos, shadowMap, shadowSmp);
    return float4(color * brightness, 1.0);
}