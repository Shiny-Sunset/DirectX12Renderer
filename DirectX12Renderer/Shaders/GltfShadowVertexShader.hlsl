#include "GltfShaderHeader.hlsli"

cbuffer CascadeIndex : register(b1)
{
    uint g_cascade;
};

float4 ShadowVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
    uint4 joints : JOINTS,
    float4 weights : WEIGHTS,
    uint instanceID : SV_InstanceID
) : SV_POSITION
{   
    InstanceData inst = g_instances[g_visible[instanceID]];
    matrix world = inst.world;
    uint bo = inst.boneOffset;

    // 4 本のボーン行列を重み付きで合成する
    matrix bm = g_bones[bo + joints.x] * weights.x
              + g_bones[bo + joints.y] * weights.y
              + g_bones[bo + joints.z] * weights.z
              + g_bones[bo + joints.w] * weights.w;
    
    pos = mul(bm, pos);
    pos = mul(world, pos);
    return mul(lightCamera[g_cascade], pos); // proj × view ではなく lightCamera
}