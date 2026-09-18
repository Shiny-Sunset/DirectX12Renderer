#include "GameTimer.h"

#include <algorithm>

void GameTimer::Reset()
{
	_prevTime = Clock::now();
	_rawDeltaTime = 0.0f;
	_deltaTime = 0.0f;
}

float GameTimer::Tick()
{
	const auto now = Clock::now();

	// time_point の差(duration)を float 秒に直す
	const float elapsed = std::chrono::duration<float>(now - _prevTime).count();
	_prevTime = now;

	_rawDeltaTime = std::min(elapsed, MaxDeltaTime);
	_deltaTime = _rawDeltaTime * _timeScale;
	_totalTime += _deltaTime;

	return _deltaTime;
}

void GameTimer::SetTimeScale(float scale)
{
	// 負の値を入れると時間が巻き戻り、アニメーションの時刻が範囲外になる
	_timeScale = std::max(0.0f, scale);
}
