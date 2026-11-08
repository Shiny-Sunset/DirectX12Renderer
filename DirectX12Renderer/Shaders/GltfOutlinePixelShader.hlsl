#include "GltfShaderHeader.hlsli"

float4 GltfOutlinePS(OutlineOutput input) : SV_TARGET
{
    return float4(0.05, 0.03, 0.06, 1.0);
}