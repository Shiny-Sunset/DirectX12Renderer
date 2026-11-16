#include "GroundShaderHeader.hlsli"

GroundOutput GroundVS(float4 pos : POSITION)
{
    GroundOutput output;
    output.svpos = mul(viewProj, pos); // ワールド行列は単位行列なので掛けない
    output.worldPos = pos.xyz;
    return output;
}