#pragma once

// 画面に出す文字列
// FontAtlas の charset をここから作るので、足した文字は必ずアトラスに入る
namespace UIText
{
    inline constexpr const wchar_t* Title = L"DANGO DEFENSE";
    inline constexpr const wchar_t* Subtitle = L"王さまをアリから守れ！";
    inline constexpr const wchar_t* Start = L"スペースキーでスタート";
    inline constexpr const wchar_t* Retry = L"R キーでもう一度";
    inline constexpr const wchar_t* GameOver = L"ゲームオーバー";
    inline constexpr const wchar_t* HudKing = L"王さま";
    inline constexpr const wchar_t* HudTime = L"生存時間";
    inline constexpr const wchar_t* HudKill = L"討伐数";
    inline constexpr const wchar_t* Score = L"スコア";
    inline constexpr const wchar_t* Best = L"ベスト";
    inline constexpr const wchar_t* NewRecord = L"新記録！";
    inline constexpr const wchar_t* HelpMove = L"WASD  移動      Shift  ダッシュ";
    inline constexpr const wchar_t* HelpJump = L"Space ジャンプ  左クリック  攻撃";
    inline constexpr const wchar_t* HelpCam = L"右ドラッグ  カメラ     F1  デバッグ";
    inline constexpr const wchar_t* Pause = L"ポーズ";
    inline constexpr const wchar_t* Resume = L"ESC でもどる";
    inline constexpr const wchar_t* UnitSec = L"秒";

    // charset を作るための一覧。文字列を足したらここにも足す
    inline constexpr const wchar_t* const All[] = {
        Title, Subtitle, Start, Retry, GameOver,
        HudKing, HudTime, HudKill, Score, Best, NewRecord,
        HelpMove, HelpJump, HelpCam, Pause, Resume, UnitSec,
    };
}