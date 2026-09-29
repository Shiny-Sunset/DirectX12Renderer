#include "Enemy.h"

#include "GltfActor.h"
#include "CharacterController.h"

#include <algorithm>
#include <cmath>

Enemy::Enemy(GltfActor& actor, CharacterController& controller)
    : _actor(actor), _controller(controller)
{
    // 初期状態は Idle。ChangeState は「同じ状態なら何もしない」ので直接呼ぶ
    _actor.PlayAnimation("Idle", 0.0f);
}

void Enemy::Update(float deltaTime, const DirectX::XMFLOAT3& targetPos, float targetRadius)
{
    _stateTime += deltaTime;
    _cooldown = std::max(0.0f, _cooldown - deltaTime);

    // -- プレイヤーまでの距離（水平方向のみ） --
    const auto& myPos = _actor.Position();
    const float dx = targetPos.x - myPos.x;
    const float dz = targetPos.z - myPos.z;
    const float dist = sqrtf(dx * dx + dz * dz);
    // 相手の表面までの距離。自分の半径も引いて「隙間」を測る
    const float gap = dist - targetRadius - _controller.Radius();

    switch (_state)
    {
    case State::Idle:
        _controller.SetMoveVelocity(0.0f, 0.0f);
        //if (dist < DetectRange)
        //{
        //    ChangeState(State::Chase);
        //}
        ChangeState(State::Chase);
        break;

    case State::Chase:
        //if (dist > LoseRange)
        //{
        //    ChangeState(State::Idle);
        //    break;
        //}

        // -- 距離に関係なく、常にプレイヤーの方を向く --
        // dist が 0 に近いと方向が定まらないので、そのときは向きを変えない
        if (dist > 1e-4f)
        {
            FaceTowards(dx / dist, dz / dist, deltaTime);
        }

        // -- 近すぎなければ前進する --
        if (gap > StopDistance)
        {
            // 王へ向かう方向
            float vx = dx / dist;
            float vz = dz / dist;

            // -- 詰まりの検知 --
            // 実際に動けた距離が、予定の 3 割に満たなければ壁にぶつかっている
            const auto& pos = _actor.Position();
            const float movedX = pos.x - _prevPos.x;
            const float movedZ = pos.z - _prevPos.z;
            const float moved = sqrtf(movedX * movedX + movedZ * movedZ);
            const float expected = ChaseSpeed() * deltaTime;

            if (_avoidTimer <= 0.0f && expected > 1e-5f && moved < expected * StuckThreshold)
            {
                _avoidTimer = AvoidTime;
                _avoidSide = (rand() % 2 == 0) ? 1.0f : -1.0f;   // 左右どちらへ逸れるかは運任せ
            }
            _prevPos = pos;

            // -- 回避中は横向きの成分を混ぜる --
            if (_avoidTimer > 0.0f)
            {
                _avoidTimer -= deltaTime;

                // 進行方向を 90 度回した向き（右手が +）
                const float sideX = vz * _avoidSide;
                const float sideZ = -vx * _avoidSide;

                // 前 3 割・横 7 割で回り込む
                vx = vx * 0.3f + sideX * 0.7f;
                vz = vz * 0.3f + sideZ * 0.7f;

                const float len = sqrtf(vx * vx + vz * vz);
                if (len > 1e-5f) { vx /= len; vz /= len; }
            }
            _controller.SetMoveVelocity(vx * ChaseSpeed(), vz * ChaseSpeed());
        }
        else
        {
            _controller.SetMoveVelocity(0.0f, 0.0f);
            if (gap < AttackRange && _cooldown <= 0.0f)
            {
                ChangeState(State::Attack);
            }
        }
        break;

    case State::Attack:
        _controller.SetMoveVelocity(0.0f, 0.0f);
        if (_stateTime >= AttackDuration)
        {
            _cooldown = AttackCooldown;
            ChangeState(State::Chase);
        }
        break;

    case State::Damaged:
        // ノックバックの速度は TakeDamage で与えたものをそのまま使い、
        // のけぞりが終わったところで止める
        if (_stateTime >= DamagedTime)
        {
            _controller.SetMoveVelocity(0.0f, 0.0f);
            ChangeState(State::Chase);
        }
        break;

    case State::Dead:
        _controller.SetMoveVelocity(0.0f, 0.0f);
        if (_stateTime > 1.0f) _actor.SetVisible(false);
        break;
    }
}

void Enemy::TakeDamage(int amount, unsigned int attackId, float fromX, float fromZ)
{
    if (_state == State::Dead) return;
    if (attackId == _lastHitAttackId) return;   // 同じ振りで既に当たっている
    _lastHitAttackId = attackId;

    _hp -= amount;

    // 攻撃者から離れる向きへ弾く
    const auto& p = _actor.Position();
    const float dx = p.x - fromX;
    const float dz = p.z - fromZ;
    const float len = sqrtf(dx * dx + dz * dz);
    if (len > 1e-4f)
    {
        _controller.SetMoveVelocity(dx / len * KnockbackSpeed, dz / len * KnockbackSpeed);
    }

    ChangeState(_hp > 0 ? State::Damaged : State::Dead);
}

const char* Enemy::StateName() const
{
    switch (_state)
    {
    case State::Idle:    return "Idle";
    case State::Chase:   return "Chase";
    case State::Attack:  return "Attack";
    case State::Damaged: return "Damaged";
    case State::Dead:    return "Dead";
    }
    return "Unknown";
}

Sphere Enemy::BodySphere() const
{
    return _controller.BodySphere();
}

Sphere Enemy::AttackSphere() const
{
    const float yaw = _actor.RotationY();
    const auto& p = _actor.Position();

    // 体の正面 0.7m、高さ 0.4m に半径 0.5m の球を置く
    // (sinf(yaw), 0, cosf(yaw)) が自分の正面ベクトル
    return {
        { p.x + sinf(yaw) * 0.7f, p.y + 0.4f, p.z + cosf(yaw) * 0.7f },
        0.5f
    };
}

bool Enemy::IsAttackActive() const
{
    return _state == State::Attack
        && _stateTime >= AttackHitTime
        && _stateTime < AttackHitTime + 0.1f;
}

void Enemy::Reset(const DirectX::XMFLOAT3& pos)
{
    _hp = 2;
    _cooldown = 0.0f;
    _lastHitAttackId = 0;
    _state = State::Idle;      // ChangeState だと同じ状態のとき何もしないので直接代入する
    _stateTime = 0.0f;
    _prevPos = pos;
    _avoidTimer = 0.0f;

    _actor.SetPosition(pos);
    _actor.SetRotationY(0.0f);
    _actor.SetVisible(true);
    _actor.PlayAnimation("Idle", 0.0f);

    _controller.Reset();
}

void Enemy::ChangeState(State next)
{
    if (_state == next) return;   // 同じ状態なら何もしない

    _state = next;
    _stateTime = 0.0f;

    // 死んだら当たり判定から外れる（見えない壁として残らないように）
    if (_state == State::Dead)
    {
        _controller.SetCollisionEnabled(false);
    }

    // 状態に応じたアニメーションは、ここだけで切り替える
    switch (_state)
    {
    case State::Idle:    _actor.PlayAnimation("Idle"); break;
    case State::Chase:   _actor.PlayAnimation("Walk"); break;
    case State::Attack:  _actor.PlayAnimation("Attack", 0.05f); break;
    case State::Damaged: _actor.PlayAnimation("Attack", 0.05f); break;   // 専用が無いので流用
    case State::Dead:    _actor.PlayAnimation("Idle", 0.2f); break;
    }
}

void Enemy::FaceTowards(float dirX, float dirZ, float deltaTime)
{
    const float targetYaw = atan2f(dirX, dirZ);
    const float diff = DirectX::XMScalarModAngle(targetYaw - _actor.RotationY());
    _actor.SetRotationY(_actor.RotationY() + diff * std::min(1.0f, TurnSpeed * deltaTime));
}
