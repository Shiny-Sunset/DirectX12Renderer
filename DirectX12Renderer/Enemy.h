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
        Idle,      // その場で待機
        Chase,     // プレイヤーを追いかける
        Attack,    // その場で攻撃する
        Damaged,   // 被弾してのけぞっている
        Dead,      // 撃破された
    };

    Enemy(GltfActor& actor, CharacterController& controller);

    // -- 毎フレームの更新 --
    // @param playerPos プレイヤーの位置（追跡の目標）
    void Update(float deltaTime, const DirectX::XMFLOAT3& playerPos);

    // -- 外からの働きかけ --
    // ダメージを受ける
    // @param attackId 攻撃の通し番号（同じ番号なら二重ヒットとして無視する）
    // @param fromX, fromZ 攻撃してきた相手の位置（ノックバックの向き）
    void TakeDamage(int amount, unsigned int attackId, float fromX, float fromZ);

    // -- 状態の取得 --
    State CurrentState() const { return _state; }
    const char* StateName() const;
    int HP() const { return _hp; }
    bool IsDead() const { return _state == State::Dead; }

    // -- 当たり判定 --
    Sphere BodySphere() const;     // 自分の体
    Sphere AttackSphere() const;   // 攻撃の判定（アクティブな時間だけ意味を持つ）
    bool IsAttackActive() const;   // 攻撃判定が出ている時間か

private:
    // -- 内部処理 --
    // 状態を切り替える（アニメーションの変更もここでまとめて行う）
    void ChangeState(State next);

    // 指定した方向へ体を向ける（急に回らないよう補間する）
    // @param dirX 向きたい方向（正規化済み）
    // @param dirZ 同上
    void FaceTowards(float dirX, float dirZ, float deltaTime);

    // -- 定数：探索と移動 --
    static constexpr float DetectRange = 5.0f;    // この距離まで近づくと気づく
    static constexpr float LoseRange = 8.0f;      // この距離まで離れると見失う
                                                  // (気づく距離と変えて、境目でのばたつきを防ぐ)
    static constexpr float StopDistance = 1.0f;   // これ以上は近づかない
    static constexpr float ChaseSpeed = 2.0f;     // 追跡する速さ（m/秒）
    static constexpr float TurnSpeed = 8.0f;      // 向きを変える速さ

    // -- 定数：戦闘 --
    static constexpr float AttackRange = 1.2f;      // この距離で攻撃を始める
    static constexpr float AttackDuration = 0.8f;   // 攻撃全体の長さ
    static constexpr float AttackHitTime = 0.3f;    // 攻撃判定が出る時刻
    static constexpr float AttackCooldown = 1.5f;   // 次に攻撃できるまでの間隔
    static constexpr float DamagedTime = 0.4f;      // のけぞっている時間
    static constexpr float KnockbackSpeed = 4.0f;   // 被弾時に弾かれる速さ

    // -- 参照するもの（所有しない） --
    GltfActor& _actor;
    CharacterController& _controller;

    // -- 状態 --
    State _state = State::Idle;
    float _stateTime = 0.0f;             // 今の状態になってからの経過秒数
    int _hp = 2;
    float _cooldown = 0.0f;              // 次の攻撃までの残り時間
    unsigned int _lastHitAttackId = 0;   // 最後に食らった攻撃の番号（多段ヒット防止）
};
