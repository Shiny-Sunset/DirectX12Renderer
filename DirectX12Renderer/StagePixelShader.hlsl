#include "StageShaderHeader.hlsli"
#include "ShadowCommon.hlsli"

float4 StagePS(StageOutput input) : SV_TARGET
{
    static const float ShadowBias = 0.005;
    
    float3 n = normalize(input.normal);
    float3 toLight = -normalize(lightVec); // 光が来る方向
    float ndl = dot(toLight, n);

    float3 color;
    
    if (abs(n.y) > 0.5)
    {
        // 上下の面：地面と同じ格子模様
        float2 uv = input.worldPos.xz;
        float2 width = fwidth(uv);
        float2 grid = abs(frac(uv - 0.5) - 0.5) / max(width, 1e-5);
        float lineMask = saturate(min(grid.x, grid.y));

        float3 lineColor = float3(0.30, 0.30, 0.35);
        float3 baseColor = float3(0.55, 0.55, 0.60);
        color = lerp(lineColor, baseColor, lineMask);
    }
    else
    {
        // 側面：格子を描くと縦に伸びてしまうので無地にする
        color = float3(0.45, 0.45, 0.50);
    }
    
    
    // -- 影の判定 --
    float brightness = ComputeBrightness(n, lightVec, input.tpos, shadowMap, shadowSmp);
    return float4(color * brightness, 1.0);
}