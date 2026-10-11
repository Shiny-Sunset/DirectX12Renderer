#include "StageShaderHeader.hlsli"

StageOutput StageVS(float4 pos : POSITION, float4 normal : NORMAL, uint instanceID : SV_InstanceID)
{
    StageOutput output;
    matrix world = g_instances[instanceID];
    pos = mul(world, pos);
    output.svpos = mul(mul(proj, view), pos); // ワールド行列は単位行列なので掛けない
    output.worldPos = pos.xyz;
    output.tpos = mul(lightCamera, pos); // 光源から見た位置
    normal.w = 0; // 平行移動を効かせない
    output.normal = mul(world, normal).xyz;
    return output;
}