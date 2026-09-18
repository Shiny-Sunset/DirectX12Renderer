#pragma once
#include <chrono>

// 1 フレームの経過時間(デルタタイム)を計測するクラス
// 時間を測るのは Application だけにして、他のクラスは受け取った値を使う
class GameTimer
{
public:
    // 計測の基準を今の時刻にする（ループに入る直前に呼ぶ）
    void Reset();

    // 前回の Tick からの経過時間を計測して返す（1 フレームに 1 回だけ呼ぶ）
    // @return タイムスケールを掛けたデルタタイム（秒）
    float Tick();

    float DeltaTime() const { return _deltaTime; }         // タイムスケールを掛けた dt（ゲームの処理はこれを使う）
    float RawDeltaTime() const { return _rawDeltaTime; }   // 掛ける前の実時間の dt（表示用）
    float TotalTime() const { return _totalTime; }         // ゲーム内の経過時間の合計（秒）

    void  SetTimeScale(float scale);   // 1 = 通常、0.5 = スロー、0 = 一時停止
    float TimeScale() const { return _timeScale; }

private:
    using Clock = std::chrono::steady_clock;

    // 大きすぎる dt を切り詰める上限（秒）
    // ウィンドウのドラッグ中やブレークポイントで止めている間はループが進まないため、
    // 再開した瞬間に数秒分の dt が来る。そのまま使うと移動処理が一気に進んでしまう
    static constexpr float MaxDeltaTime = 0.1f;

    Clock::time_point _prevTime;
    float _rawDeltaTime = 0.0f;
    float _deltaTime = 0.0f;
    float _totalTime = 0.0f;
    float _timeScale = 1.0f;
};
