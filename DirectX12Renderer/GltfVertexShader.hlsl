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
    output.svpos = mul(mul(mul(proj, view), world), pos);
    normal.w = 0;
    output.normal = mul(world, normal).xyz;
    output.uv = uv;
    return output;
}