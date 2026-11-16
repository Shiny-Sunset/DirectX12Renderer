#include "GltfShaderHeader.hlsli"

Output GltfVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
    uint4 joints : JOINTS,
    float4 weights : WEIGHTS,
    uint instanceID : SV_InstanceID
)
{
    Output output;
    
    InstanceData inst = g_instances[instanceID];
    matrix world = inst.world;
    uint bo = inst.boneOffset;

    // 4 本のボーン行列を重み付きで合成する
    matrix bm = g_bones[bo + joints.x] * weights.x
              + g_bones[bo + joints.y] * weights.y
              + g_bones[bo + joints.z] * weights.z
              + g_bones[bo + joints.w] * weights.w;
    
    pos = mul(bm, pos);
    float4 worldPos = mul(world, pos);
    output.worldPos = worldPos.xyz;
    
    
    normal.w = 0;
    normal = mul(bm, normal);
    float3 worldNormal = normalize(mul(world, normal).xyz);
    
    output.normal = worldNormal;
    output.svpos = mul(mul(viewProj, world), pos);

    output.uv = uv;
    return output;
}