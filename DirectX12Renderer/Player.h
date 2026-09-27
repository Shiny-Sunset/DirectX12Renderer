#pragma once
#include <DirectXMath.h>
#include "Collision.h"

class GltfActor;
class CharacterController;
class Input;

// 操作キャラクター 1 体の「判断」を受け持つ
// 見た目は GltfActor、体は CharacterController が持ち、このクラスは
// 「入力を受けて、今どの状態で、次に何をするか」だけを決める
class Player
{
public:
    // プレイヤーの状態。同時に 1 つしか成立しない
    enum class State
    {
        Move,      // 移動・ジャンプができる通常状態
        Attack,    // 攻撃中。移動できない
        Damaged,   // 被弾してのけぞっている
        Dead,      // 死亡。操作を受け付けない
    };

    Player(GltfActor& actor, CharacterController& controller);

    // -- 毎フレームの更新 --
    // @param cameraYaw 移動方向の基準となるカメラの向き
    // @param inputEnabled ImGui が入力を使っているときは false
    void Update(float deltaTime, const Input& input, float cameraYaw, bool inputEnabled);

    // -- 外からの働きかけ --
    void TakeDamage(int amount);

    // -- 状態の取得 --
    State CurrentState() const { return _state; }
    const char* StateName() const;
    int HP() const { return _hp; }

    // -- 当たり判定 --
    Sphere BodySphere() const;     // 自分の体
    Sphere AttackSphere() const;   // 攻撃の判定（アクティブな時間だけ意味を持つ）
    bool IsAttackActive() const;   // 攻撃判定が出ている時間か

    // 1 回の振りごとの通し番号。同じ振りで同じ敵に何度も当てないために使う
    unsigned int AttackId() const { return _attackId; }

    // -- 調整値（ImGui から変更する） --
    void SetWalkSpeed(float s) { _walkSpeed = s; }
    float WalkSpeed() const { return _walkSpeed; }
    void SetRunSpeed(float s) { _runSpeed = s; }
    float RunSpeed() const { return _runSpeed; }

private:
    // -- 内部処理 --
    // 状態を切り替える（アニメーションの変更もここでまとめて行う）
    void ChangeState(State next);
    void UpdateMove(float deltaTime, const Input& input, float cameraYaw, bool inputEnabled);

    // 指定した方向へ体を向ける（急に回らないよう補間する）
    // @param dirX 向きたい方向（正規化済み）
    // @param dirZ 同上
    void FaceTowards(float dirX, float dirZ, float deltaTime);

    // -- 定数 --
    static constexpr float TurnSpeed = 12.0f;       // 向きを変える速さ
    static constexpr float AttackDuration = 0.5f;   // 攻撃全体の長さ
    static constexpr float HitStart = 0.15f;        // 攻撃判定が出る時刻
    static constexpr float HitEnd = 0.30f;          // 攻撃判定が消える時刻
    static constexpr float DamagedTime = 0.4f;      // のけぞっている時間
    static constexpr float InvincibleTime = 1.0f;   // 被弾後の無敵時間

    // -- 参照するもの（所有しない） --
    GltfActor& _actor;
    CharacterController& _controller;

    // -- 状態 --
    State _state = State::Move;
    float _stateTime = 0.0f;        // 今の状態になってからの経過秒数
    int _hp = 3;
    float _invincibleTime = 0.0f;   // 残りの無敵時間
    unsigned int _attackId = 1;     // 攻撃を出すたびに増える

    // -- 調整値 --
    float _walkSpeed = 1.8f;   // m/秒
    float _runSpeed = 4.0f;
};
