#include "GltfShaderHeader.hlsli"

OutlineOutput GltfOutlineVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float3 smoothNormal : SMOOTH_NORMAL,
    float2 uv : TEXCOORD,
    uint4 joints : JOINTS,
    float4 weights : WEIGHTS,
    uint instanceID : SV_InstanceID
)
{
    // 輪郭線の太さ
    static const float outlineWidth = 0.005;

    OutlineOutput output;

    InstanceData inst = g_instances[instanceID];
    matrix world = inst.world;
    uint bo = inst.boneOffset;

    // 4 本のボーン行列を重み付きで合成する
    matrix bm = g_bones[bo + joints.x] * weights.x
              + g_bones[bo + joints.y] * weights.y
              + g_bones[bo + joints.z] * weights.z
              + g_bones[bo + joints.w] * weights.w;

    pos = mul(bm, pos);

    // 押し出し方向にも同じ変換を掛ける（w = 0 で平行移動成分を無効化）
    float3 dir = mul(bm, float4(smoothNormal, 0)).xyz;
    pos.xyz += normalize(dir) * outlineWidth;

    output.svpos = mul(mul(mul(proj, view), world), pos);
    return output;
}