#pragma once
#include <DirectXMath.h>
#include "Collision.h"

class GltfActor;

// キャラクター 1 体の移動・重力・接地を受け持つ
// 位置そのものは GltfActor が持ち、このクラスは「速度」だけを管理する
class CharacterController
{
public:
	explicit CharacterController(GltfActor& actor);

    // 水平方向の移動速度を指定する（毎フレーム、入力や AI から設定する）
    // @param vx X 方向の速度（m/秒）
    // @param vz Z 方向の速度（m/秒）
    void SetMoveVelocity(float vx, float vz);

    // ジャンプする（接地しているときだけ有効）
    void Jump();

    // 重力を適用し、位置を進める
    void Update(float deltaTime);

    bool IsGrounded() const { return _grounded; }
    float VelocityY() const { return _velocity.y; }

    // -- 調整用 --
    void SetJumpHeight(float h) { _jumpHeight = h; }
    float JumpHeight() const { return _jumpHeight; }
    void SetGravity(float g) { _gravity = g; }
    float Gravity() const { return _gravity; }

    // 当たり判定用の球を返す
    // 足元が position なので、中心は半径のぶんだけ上へ持ち上げる
    Sphere BodySphere() const;

    // 押し戻し（水平方向のみ）
    void PushXZ(float dx, float dz);

    void SetRadius(float r) { _radius = r; }
    float Radius() const { return _radius; }

    // 押し戻しの負担割合。大きいほど押されにくい
    // （1.0 = 普通、10.0 = ほぼ動かない、0 = 完全に固定）
    void SetPushWeight(float w) { _pushWeight = w; }
    float PushWeight() const { return _pushWeight; }

private:
    GltfActor& _actor;

    DirectX::XMFLOAT3 _velocity = { 0.0f, 0.0f, 0.0f };
    bool _grounded = true;

    float _gravity = -20.0f;     // m/秒²（下向きなので負）
    float _jumpHeight = 1.2f;    // ジャンプの最高到達点（m）

    // 地面の高さ。当たり判定を入れるまでは平らな床として扱う
    static constexpr float GroundHeight = 0.0f;

    float _radius = 0.4f;
    float _pushWeight = 1.0f;
};