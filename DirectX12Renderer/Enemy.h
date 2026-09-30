#pragma once
#include <DirectXMath.h>
#include "Collision.h"
#include "GameConfig.h"

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
    // @param target 本来狙う相手（王）
    // @param player ヘイトが向いているときに狙う相手
    void Update(float deltaTime, const GameConfig::Enemy::Target& target, const GameConfig::Enemy::Target& player);

    // -- 外からの働きかけ --
    // ダメージを受ける
    // @param attackId 攻撃の通し番号（同じ番号なら二重ヒットとして無視する）
    // @param fromX, fromZ 攻撃してきた相手の位置（ノックバックの向き）
    void TakeDamage(int amount, unsigned int attackId, float fromX, float fromZ);

    void NotifyAttackLanded();

    // -- 状態の取得 --
    State CurrentState() const { return _state; }
    const char* StateName() const;
    int HP() const { return _hp; }
    bool IsDead() const { return _state == State::Dead; }

    float ChaseSpeed() const { return _chaseSpeed; }
    void SetChaseSpeed(float s) { _chaseSpeed = s; }

    // -- 当たり判定 --
    Sphere BodySphere() const;     // 自分の体
    Sphere AttackSphere() const;   // 攻撃の判定（アクティブな時間だけ意味を持つ）
    bool IsAttackActive() const;   // 攻撃判定が出ている時間か

    void Reset(const DirectX::XMFLOAT3& pos);

private:
    // -- 内部処理 --
    // 状態を切り替える（アニメーションの変更もここでまとめて行う）
    void ChangeState(State next);

    // 指定した方向へ体を向ける（急に回らないよう補間する）
    // @param dirX 向きたい方向（正規化済み）
    // @param dirZ 同上
    void FaceTowards(float dirX, float dirZ, float deltaTime);

    // -- 定数：探索と移動 --
    float DetectRange = GameConfig::Enemy::DetectRange;    // この距離まで近づくと気づく
    float LoseRange = GameConfig::Enemy::LoseRange;      // この距離まで離れると見失う
                                                  // (気づく距離と変えて、境目でのばたつきを防ぐ)
    float StopDistance = GameConfig::Enemy::StopDistance;   // これ以上は近づかない
    float _chaseSpeed = GameConfig::Enemy::ChaseSpeed;     // 追跡する速さ（m/秒）
    float TurnSpeed = GameConfig::Enemy::TurnSpeed;      // 向きを変える速さ

    // -- 障害物の回避 --
    // 壁に押し付けられて動けないとき、一定時間だけ横へ逸れる
    static constexpr float AvoidTime = 0.7f;        // 逸れる時間
    static constexpr float StuckThreshold = 0.3f;   // 予定の何割しか進めなければ詰まりとみなす

    // -- 定数：戦闘 --
    float AttackRange = GameConfig::Enemy::AttackRange;      // この距離で攻撃を始める
    float AttackDuration = GameConfig::Enemy::AttackDuration;   // 攻撃全体の長さ
    float AttackHitTime = GameConfig::Enemy::AttackHitTime;    // 攻撃判定が出る時刻
    float AttackCooldown = GameConfig::Enemy::AttackCooldown;   // 次に攻撃できるまでの間隔
    float DamagedTime = GameConfig::Enemy::DamagedTime;      // のけぞっている時間
    float KnockbackSpeed = GameConfig::Enemy::KnockbackSpeed;   // 被弾時に弾かれる速さ

    // -- 参照するもの（所有しない） --
    GltfActor& _actor;
    CharacterController& _controller;

    // -- 状態 --
    State _state = State::Idle;
    float _stateTime = 0.0f;             // 今の状態になってからの経過秒数
    int _hp = GameConfig::Enemy::MaxHP;
    float _cooldown = 0.0f;              // 次の攻撃までの残り時間
    unsigned int _lastHitAttackId = 0;   // 最後に食らった攻撃の番号（多段ヒット防止）

    DirectX::XMFLOAT3 _prevPos = { 0.0f, 0.0f, 0.0f };
    float _avoidTimer = 0.0f;
    float _avoidSide = 1.0f;   // +1 で右、-1 で左
    float _aggroTimer = 0.0f;
    bool _attackLanded = false;
};
