#pragma once
#include <DirectXMath.h>
#include "Input.h"
#include "Collision.h"

class GltfActor;
class CharacterController;

class Player
{
public:
	enum class State
	{
		Move,      // 移動・ジャンプができる通常状態
		Attack,    // 攻撃中。移動できない
		Damaged,   // 被弾してのけぞっている
		Dead,      // 死亡。操作を受け付けない
	};

    Player(GltfActor& actor, CharacterController& controller);

    // @param cameraYaw 移動方向の基準となるカメラの向き
    // @param inputEnabled ImGui が入力を使っているときは false
    void Update(float deltaTime, const Input& input, float cameraYaw, bool inputEnabled);

    State CurrentState() const { return _state; }
    const char* StateName() const;

    // -- 調整値（ImGui から変更する） --
    void SetWalkSpeed(float s) { _walkSpeed = s; }
    float WalkSpeed() const { return _walkSpeed; }
    void SetRunSpeed(float s) { _runSpeed = s; }
    float RunSpeed() const { return _runSpeed; }

    // 攻撃判定の球（アクティブな時間だけ有効）
    Sphere AttackSphere() const;
    bool IsAttackActive() const;

    // 自身の当たり判定の球
    Sphere BodySphere() const;

    // 1 回の振りに通し番号を付ける。同じ振りで同じ敵に何度も当てないために使う
    unsigned int AttackId() const { return _attackId; }

    void TakeDamage(int amount);

    int HP() const { return _hp; }

private:
    void ChangeState(State next);
    void UpdateMove(float deltaTime, const Input& input, float cameraYaw, bool inputEnabled);
    void FaceTowards(float dirX, float dirZ, float deltaTime);

    GltfActor& _actor;
    CharacterController& _controller;

    State _state = State::Move;
    float _stateTime = 0.0f;

    float _walkSpeed = 1.8f;
    float _runSpeed = 4.0f;
    static constexpr float TurnSpeed = 12.0f;

    static constexpr float AttackDuration = 0.5f;   // 攻撃全体の長さ
    static constexpr float HitStart = 0.15f;        // 判定が出る
    static constexpr float HitEnd = 0.30f;          // 判定が消える

    unsigned int _attackId = 1;

    static constexpr float InvincibleTime = 1.0f;
    float _invincibleTime = 0.0f;
    int _hp = 3;
    static constexpr float DamagedTime = 0.4f;
};