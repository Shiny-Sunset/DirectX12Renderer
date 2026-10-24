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
    
    
    // 箱と同じ中心からの距離で選ぶ。
    // 箱は光源方向に対して垂直な正方形なので、半径 area/2 の球は必ず中に収まる
    float d = length(input.worldPos - shadowCenter.xyz);
    uint c = 0;
    if (d > cascadeRadius.x)
        c = 1;
    if (d > cascadeRadius.y)
        c = 2;
    
    // カスケードごとにテクセルの実寸が違うので、押し出す量も変える
    const float normalOffset = cascadeNormalOffset[c];
    float3 normal = float3(0, 1, 0);
    float3 biased = input.worldPos + normal * normalOffset;
    float4 tpos = mul(lightCamera[c], float4(biased, 1.0));

    // -- 影の判定 --
    // 地面は常に真上を向いている
    float brightness = ComputeBrightness(normal, lightVec, tpos, c, shadowMap, shadowSmp);
    
    return float4(color * brightness, 1.0);
}