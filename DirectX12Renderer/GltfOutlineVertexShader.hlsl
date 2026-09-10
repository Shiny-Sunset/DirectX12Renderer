#include "GltfShaderHeader.hlsli"

OutlineOutput GltfOutlineVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float3 smoothNormal : SMOOTH_NORMAL,
    float2 uv : TEXCOORD,
    uint4 joints : JOINTS,
    float4 weights : WEIGHTS
)
{
    // 輪郭線の太さ
    static const float outlineWidth = 0.005;

    OutlineOutput output;

    // 本体と同じボーン行列でスキニングする
    // これを忘れるとアニメーション中に輪郭線だけ取り残される
    matrix bm = bones[joints.x] * weights.x
              + bones[joints.y] * weights.y
              + bones[joints.z] * weights.z
              + bones[joints.w] * weights.w;

    pos = mul(bm, pos);

    // 押し出し方向にも同じ変換を掛ける（w = 0 で平行移動成分を無効化）
    float3 dir = mul(bm, float4(smoothNormal, 0)).xyz;
    pos.xyz += normalize(dir) * outlineWidth;

    output.svpos = mul(mul(mul(proj, view), world), pos);
    return output;
}