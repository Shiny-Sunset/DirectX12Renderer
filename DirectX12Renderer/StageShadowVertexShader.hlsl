#include "StageShaderHeader.hlsli"

float4 StageShadowVS(float4 pos : POSITION, float4 normal : NORMAL) : SV_POSITION
{
    pos = mul(world, pos); // 箱の大きさと位置
    return mul(lightCamera, pos); // 光源から見た位置
}