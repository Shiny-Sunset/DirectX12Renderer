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
    
    normal.w = 0;
    normal = mul(bm, normal);
    
    output.svpos = mul(mul(mul(proj, view), world), pos);
    output.normal = mul(world, normal).xyz;
    output.uv = uv;
    return output;
}