#include "GltfShaderHeader.hlsli"

float4 ShadowVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
    uint4 joints : JOINTS,
    float4 weights : WEIGHTS
) : SV_POSITION
{   
    // 4 本のボーン行列を重み付きで合成する
    matrix bm = bones[joints.x] * weights.x
              + bones[joints.y] * weights.y
              + bones[joints.z] * weights.z
              + bones[joints.w] * weights.w;
    
    pos = mul(bm, pos);
    pos = mul(world, pos);
    return mul(lightCamera, pos); // proj × view ではなく lightCamera
}