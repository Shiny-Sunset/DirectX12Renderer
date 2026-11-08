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
    
    // 箱と同じ中心からの距離で選ぶ
    // 箱は光源方向に対して垂直な正方形なので、半径 area/2 の球は必ず中に収まる
    float d = length(input.worldPos - shadowCenter.xyz);
    uint c = 0;
    if (d > cascadeRadius.x)
        c = 1;
    if (d > cascadeRadius.y)
        c = 2;
    
    // カスケードごとにテクセルの実寸が違うので、押し出す量も変える
    const float normalOffset = cascadeNormalOffset[c];
    
    float3 biased = input.worldPos + n * normalOffset;
    float4 tpos = mul(lightCamera[c], float4(biased, 1.0));
    
    // -- 影の判定 --
    float brightness = ComputeBrightness(n, lightVec, tpos, c, shadowMap, shadowSmp);
    return float4(color * brightness, 1.0);
}