#include "StageShaderHeader.hlsli"

cbuffer CascadeIndex : register(b1)
{
    uint g_cascade;
};

float4 StageShadowVS(float4 pos : POSITION, float4 normal : NORMAL, uint instanceID : SV_InstanceID) : SV_POSITION
{
    pos = mul(g_instances[instanceID], pos); // 箱の大きさと位置
    return mul(lightCamera[g_cascade], pos); // 光源から見た位置
}