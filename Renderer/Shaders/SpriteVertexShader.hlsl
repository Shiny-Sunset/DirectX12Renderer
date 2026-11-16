#include "SpriteShaderHeader.hlsli"

cbuffer ScreenInfo : register(b0)
{
    float2 screenSize; // ピクセル
};

SpriteOutput SpriteVS(float2 pos : POSITION, float2 uv : TEXCOORD, float4 color : COLOR)
{
    SpriteOutput output;

    // 左上 (0,0) のピクセル座標 → NDC。Y は符号が逆になる
    output.svpos = float4(pos.x / screenSize.x * 2.0 - 1.0,
                          1.0 - pos.y / screenSize.y * 2.0,
                          0.0, 1.0);
    output.uv = uv;
    output.color = color;

    return output;
}