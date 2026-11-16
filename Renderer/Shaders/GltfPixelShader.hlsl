#include "GltfShaderHeader.hlsli"
#include "ShadowCommon.hlsli"

float4 GltfPS(Output input) : SV_TARGET
{
    // -- 調整用パラメータ --
    static const float steps = 4.0; // 階調の数（2〜4 くらい）
    static const float shadowLevel = 0.4; // 影側の明るさ（0 = 真っ黒）

    float3 light = normalize(lightVec); // 光が進む向き
    float3 normal = normalize(input.normal);

    float4 texColor = baseColorTex.Sample(smp, input.uv);
    float3 albedo = texColor.rgb * baseColorFactor.rgb;
    float alpha = texColor.a * baseColorFactor.a;
    
    // ハーフランバート
    // 生の dot は -1〜1 なので、裏側がすべて 0 に潰れて階調が作れない
    // 0〜1 に写し直してから量子化する
    float ndotl = dot(-light, normal);
    float halfLambert = ndotl * 0.5 + 0.5;
    
    // 階調を量子化する
    // floor で段に切り、(steps - 1) で割ることで 0〜1 に正規化する
    float f = halfLambert * steps;
    float toon = saturate((floor(f) + smoothstep(0.0, 0.08, frac(f))) / (steps - 1.0));
    
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
    
    float3 biased = input.worldPos + normal * normalOffset;
    float4 tpos = mul(lightCamera[c], float4(biased, 1.0));
    
    // 影の中かどうかを調べる
    float lit = ComputeShadowLit(tpos, ndotl, c, shadowMap, shadowSmp);
    
    // 影に入っていたら、トゥーンの暗い側へ寄せる
    // min を使うのは、階調を保ったまま「暗い方を採用」するため
    float shade = min(toon, lit);
    
    // 影側が真っ黒にならないよう下限を設ける
    float brightness = lerp(shadowLevel, 1.0, shade);

    return float4(albedo * brightness, alpha);
}