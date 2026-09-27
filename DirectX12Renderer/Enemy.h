#pragma once
#include <DirectXMath.h>

class GltfActor;
class CharacterController;

// 敵 1 体の「判断」を受け持つ
// 見た目は GltfActor、体は CharacterController が持ち、このクラスは何もしない時間も含めて
// 「今どういう状態で、次に何をするか」だけを決める
class Enemy
{
public:
    // 敵の状態。同時に 1 つしか成立しない
    enum class State
    {
        Idle,    // その場で待機
        Chase,   // プレイヤーを追いかける
    };

    Enemy(GltfActor& actor, CharacterController& controller);

    // @param playerPos プレイヤーの位置（追跡の目標）
    void Update(float deltaTime, const DirectX::XMFLOAT3& playerPos);

    State CurrentState() const { return _state; }
    const char* StateName() const;   // デバッグ表示用

private:
    // 状態を切り替える（アニメーションの変更もここでまとめて行う）
    void ChangeState(State next);

    // 指定した方向へ体を向ける（急に回らないよう補間する）
    // @param dirX 向きたい方向（正規化済み）
    // @param dirZ 同上
    void FaceTowards(float dirX, float dirZ, float deltaTime);

    GltfActor& _actor;
    CharacterController& _controller;

    State _state = State::Idle;
    float _stateTime = 0.0f;   // 今の状態になってからの経過秒数

    // -- 調整値 --
    static constexpr float DetectRange = 5.0f;    // この距離まで近づくと気づく
    static constexpr float LoseRange = 8.0f;      // この距離まで離れると見失う
    static constexpr float StopDistance = 1.0f;   // これ以上は近づかない
    static constexpr float ChaseSpeed = 2.0f;     // 追跡する速さ（m/秒）
    static constexpr float TurnSpeed = 8.0f;      // 向きを変える速さ
};