#include "GltfShaderHeader.hlsli"

float4 GltfPS(Output input) : SV_TARGET
{
    // -- 調整用パラメータ --
    static const float steps = 4.0; // 階調の数（2〜4 くらい）
    static const float shadowLevel = 0.4; // 影側の明るさ（0 = 真っ黒）

    float3 light = normalize(float3(1, -1, 1)); // 光が進む向き
    float3 normal = normalize(input.normal);

    float4 texColor = baseColorTex.Sample(smp, input.uv);
    float3 albedo = texColor.rgb * baseColorFactor.rgb;
    float alpha = texColor.a * baseColorFactor.a;
    
    // ハーフランバート
    // 生の dot は -1〜1 なので、裏側がすべて 0 に潰れて階調が作れない。
    // 0〜1 に写し直してから量子化する。
    float ndotl = dot(-light, normal);
    float halfLambert = ndotl * 0.5 + 0.5;
    
    // 階調を量子化する
    // floor で段に切り、(steps - 1) で割ることで 0〜1 に正規化する
    float f = halfLambert * steps;
    float toon = saturate((floor(f) + smoothstep(0.0, 0.08, frac(f))) / (steps - 1.0));
    
    // 影側が真っ黒にならないよう下限を設ける
    float brightness = lerp(shadowLevel, 1.0, toon);

    return float4(albedo * brightness, alpha);
}