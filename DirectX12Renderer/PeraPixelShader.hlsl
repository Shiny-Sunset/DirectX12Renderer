#include "PeraShaderHeader.hlsli"

float4 PeraPS(Output input) : SV_TARGET
{
    return tex.Sample(smp, input.uv);
}