#pragma once
#include <DirectXMath.h>
#include "Collision.h"

class GltfActor;

// キャラクター 1 体の「体」を受け持つ
// 移動速度・重力・接地・当たり判定を扱う
// 位置そのものは GltfActor が持ち、このクラスは速度を管理して位置を書き換える
class CharacterController
{
public:
    explicit CharacterController(GltfActor& actor);

    // -- 毎フレームの更新 --
    // 重力を適用し、速度に従って位置を進める
    void Update(float deltaTime);

    // -- 移動 --
    // 水平方向の移動速度を指定する（毎フレーム、入力や AI から設定する）
    // @param vx X 方向の速度（m/秒）
    // @param vz Z 方向の速度（m/秒）
    void SetMoveVelocity(float vx, float vz);

    // ジャンプする（接地しているときだけ有効）
    void Jump();

    bool IsGrounded() const { return _grounded; }
    float VelocityY() const { return _velocity.y; }

    // -- 当たり判定 --
    // 当たり判定用の球を返す
    // 足元が position なので、中心は半径のぶんだけ上へ持ち上げる
    Sphere BodySphere() const;

    // 押し戻し（水平方向のみ）
    void PushXZ(float dx, float dz);

    // 押し戻された結果、足場の上に乗った
    // @param groundY 足を置く高さ
    void LandOn(float groundY);

    // 押し戻し（3 軸）
    void Push(float dx, float dy, float dz);

    void Reset()
    {
        _velocity = { 0.0f, 0.0f, 0.0f };
        _grounded = true;
        _pushWeight = 1.0f;   // 死亡時に 0 にしているので戻す
    }

    // -- 調整値 --
    void SetJumpHeight(float h) { _jumpHeight = h; }
    float JumpHeight() const { return _jumpHeight; }
    void SetGravity(float g) { _gravity = g; }
    float Gravity() const { return _gravity; }
    void SetRadius(float r) { _radius = r; }
    float Radius() const { return _radius; }

    // 押し戻しの負担割合。大きいほど押されにくい
    // （1.0 = 普通、10.0 = ほぼ動かない、0 = 完全に固定）
    void SetPushWeight(float w) { _pushWeight = w; }
    float PushWeight() const { return _pushWeight; }

    // 今いる場所の床の高さ（毎フレーム、ステージから教えてもらう）
    void SetGroundHeight(float y) { _groundHeight = y; }

    const DirectX::XMFLOAT3& Position() const;

    float VelocityX() const { return _velocity.x; }
    float VelocityZ() const { return _velocity.z; }

private:
    // -- 参照するもの（所有しない） --
    GltfActor& _actor;

    // -- 状態 --
    DirectX::XMFLOAT3 _velocity = { 0.0f, 0.0f, 0.0f };
    bool _grounded = true;

    // -- 調整値 --
    float _gravity = -20.0f;     // m/秒²（下向きなので負）
    float _jumpHeight = 1.2f;    // ジャンプの最高到達点（m）
    float _radius = 0.4f;        // 当たり判定の球の半径（m）
    float _pushWeight = 1.0f;    // 押し戻しの負担割合
    float _groundHeight = 0.0f; // 地面の高さ
    float _coyoteTime = 0.0f;
};
