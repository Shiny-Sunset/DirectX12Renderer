#include "BasicShaderHeader.hlsli"

float4 BasicPS(Output input) : SV_TARGET
{
    float3 light = normalize(float3(1, -1, 1)); // 光の向かうベクトル
    float3 lightColor = float3(1, 1, 1);    // ライトの色
    
    // ディフューズ計算
    float diffuseB = dot(-light, input.normal.xyz);
    float4 toonDif = toon.Sample(smpToon, float2(0, 1.0 - diffuseB));

    // 光の反射ベクトル
    float3 refLight = normalize(reflect(light, input.normal.xyz));
    float specularB = pow(saturate(dot(refLight, -input.ray)), specular.a);
    
    // ビュー空間の法線 xy (-1〜1) を uv (0〜1) に変換する
    // v は上下が逆なので反転させる
    float3 vnormal = normalize(input.vnormal.xyz);	// 補間で長さが崩れるため正規化し直す
    float2 sphereMapUV = (vnormal.xy + float2(1, -1)) * float2(0.5, -0.5);

    float4 texColor = tex.Sample(smp, input.uv);
    float4 sphColor = sph.Sample(smp, sphereMapUV);  // 乗算スフィアマップ
    float4 spaColor = spa.Sample(smp, sphereMapUV);  // 加算スフィアマップ

    // 乗算スフィアマップは掛け、加算スフィアマップは足す
    // 加算分はテクスチャ色に乗せるが、アルファには影響させない

    return max(toonDif * diffuse * texColor * sphColor
        + float4(saturate(spaColor.rgb) * texColor.rgb, 0) + float4(specularB * specular.rgb, 1), float4(texColor.rgb * ambient, 1));
}