#include "GateShaderHeader.hlsli"

float4 GatePS(GateOutput input) : SV_TARGET
{
    const float r = length(input.uv);
    if (r > 1.0)
        discard; // 四角の角を落として円にする

    const float ang = atan2(input.uv.y, input.uv.x);

    // 渦。中心へ行くほど速く回す（(1 - r) の項）
    const float swirl = ang + time * 1.2 + (1.0 - r) * 3.0;

    // 角度をそのままノイズに渡すと、0 と 2π の継ぎ目に切れ目が出る
    // sin/cos を通して「円周上で連続な 2 次元座標」にしてから渡す
    const float2 np = float2(cos(swirl), sin(swirl)) * (1.0 + r * 2.0);
    const float n = Fbm(np * 2.0 + time * 0.3);

    // 輪は光らせ、中心は暗く落とす
    const float ring = smoothstep(0.95, 0.60, r) * smoothstep(0.10, 0.45, r);
    const float core = smoothstep(0.50, 0.00, r);
    
    float a = saturate(ring * n * 1.6 + core * 0.35);
    a *= smoothstep(1.0, 0.85, r); // 外周をなめらかに消す

    // 出力はリニア。レンダーターゲットが *_SRGB なので、ここで sRGB を書くと明るく出る
    const float3 inner = float3(0.45, 0.05, 0.70); // 紫
    const float3 outer = float3(0.05, 0.60, 0.85); // 水色
    const float3 col = lerp(inner, outer, saturate(n));

    return float4(col, a); // 加算合成なので、RGB にアルファを掛けるのはブレンド側
}