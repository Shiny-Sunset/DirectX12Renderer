#include "GltfShaderHeader.hlsli"

Output GltfVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
    uint4 joints : JOINTS,
    float4 weights : WEIGHTS 
)
{
    Output output;
    
    // 4 本のボーン行列を重み付きで合成する
    matrix bm = bones[joints.x] * weights.x
              + bones[joints.y] * weights.y
              + bones[joints.z] * weights.z
              + bones[joints.w] * weights.w;
    
    pos = mul(bm, pos);
    float4 worldPos = mul(world, pos);
    
    // シャドウマップの 1 テクセルの実寸の約 2 倍
    static const float NormalOffset = 0.025;
    
    normal.w = 0;
    normal = mul(bm, normal);
    float3 worldNormal = normalize(mul(world, normal).xyz);
    
    output.normal = worldNormal;
    output.svpos = mul(mul(mul(proj, view), world), pos);

    // 影を引く位置だけ、法線方向へ押し出す
    float4 shadowPos = worldPos + float4(worldNormal * NormalOffset, 0.0);
    output.tpos = mul(lightCamera, shadowPos);
    output.uv = uv;
    return output;
}