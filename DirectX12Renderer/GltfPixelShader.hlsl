#include "GltfShaderHeader.hlsli"

float4 GltfPS(Output input) : SV_TARGET
{
    float3 light = normalize(float3(1, -1, 1));
    float brightness = saturate(dot(-light, normalize(input.normal)));

    float4 texColor = baseColorTex.Sample(smp, input.uv);
    float3 albedo = texColor.rgb * baseColorFactor.rgb;
    float alpha = texColor.a * baseColorFactor.a;

    // 環境光で底上げして真っ黒を避ける
    return float4(albedo * (0.3 + brightness * 0.7), alpha);
}