#pragma once
#include<cstdint>

namespace ShadowConfig
{
    // シャドウマップの解像度
    constexpr std::uint32_t ShadowMapSize = 2048;
    constexpr std::uint32_t CascadeCount = 3;

    constexpr float CascadeAreas[CascadeCount] = { 12.0f, 28.0f, 60.0f };   // m 四方
    // 切り替える半径 = 箱の内接円（area / 2）より少し内側
    // 法線方向へ押し出したぶんが箱からはみ出さないよう、0.5 ではなく 0.45 にする
    constexpr float CascadeRadiusRatio = 0.45f;
}