#include "StageShaderHeader.hlsli"

float4 StageShadowVS(float4 pos : POSITION, float4 normal : NORMAL, uint instanceID : SV_InstanceID) : SV_POSITION
{
    pos = mul(g_instances[instanceID], pos); // 箱の大きさと位置
    return mul(lightCamera, pos); // 光源から見た位置
}