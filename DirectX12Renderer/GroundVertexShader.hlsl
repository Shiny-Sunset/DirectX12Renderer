#include "GroundShaderHeader.hlsli"

GroundOutput GroundVS(float4 pos : POSITION)
{
    GroundOutput output;
    output.svpos = mul(mul(proj, view), pos); // ワールド行列は単位行列なので掛けない
    output.worldPos = pos.xyz;
    output.tpos = mul(lightCamera, pos); // 光源から見た位置
    return output;
}