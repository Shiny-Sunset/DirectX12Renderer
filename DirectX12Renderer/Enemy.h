#pragma once
#include <DirectXMath.h>
#include "Collision.h"

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
        Attack,
        Damaged, 
        Dead,
    };

    Enemy(GltfActor& actor, CharacterController& controller);

    // @param playerPos プレイヤーの位置（追跡の目標）
    void Update(float deltaTime, const DirectX::XMFLOAT3& playerPos);

    State CurrentState() const { return _state; }
    const char* StateName() const;   // デバッグ表示用

    // ダメージを受ける
    // @param attackId 攻撃の通し番号（同じ番号なら二重ヒットとして無視する）
    // @param fromX, fromZ 攻撃してきた相手の位置（ノックバックの向き）
    void TakeDamage(int amount, unsigned int attackId, float fromX, float fromZ);

    bool IsDead() const { return _state == State::Dead; }
    Sphere BodySphere() const;   // Application から判定に使う

    // 攻撃判定の球（アクティブな時間だけ有効）
    Sphere AttackSphere() const;
    bool IsAttackActive() const;

    int HP() const { return _hp; }

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
    int _hp = 2;
    unsigned int _lastHitAttackId = 0;

    static constexpr float DamagedTime = 0.4f;     // のけぞる時間
    static constexpr float KnockbackSpeed = 4.0f;  // 弾かれる速さ

    static constexpr float AttackRange = 1.2f;      // この距離で攻撃を始める
    static constexpr float AttackDuration = 0.8f;
    static constexpr float AttackHitTime = 0.3f;    // この時刻に判定が出る
    static constexpr float AttackCooldown = 1.5f;   // 次に攻撃できるまで

    float _cooldown = 0.0f;
    bool _hitDone = false;   // この攻撃で既に当てたか
};