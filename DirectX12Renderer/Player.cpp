#include "Player.h"
#include "GltfActor.h"
#include "CharacterController.h"
#include <algorithm>

Player::Player(GltfActor& actor, CharacterController& controller) :_actor(actor), _controller(controller)
{

}

void Player::Update(float deltaTime, const Input& input, float cameraYaw, bool inputEnabled)
{
    _stateTime += deltaTime;
	_invincibleTime = std::max(0.0f, _invincibleTime - deltaTime);

    switch (_state)
    {
    case State::Move:
        UpdateMove(deltaTime, input, cameraYaw, inputEnabled);
        break;

    case State::Attack:
		_controller.SetMoveVelocity(0.0f, 0.0f);   // その場で攻撃する
		if (_stateTime >= AttackDuration)
		{
			ChangeState(State::Move);
		}
		break;
	case State::Damaged:
		_controller.SetMoveVelocity(0.0f, 0.0f);
		if (_stateTime >= DamagedTime)   // 0.4f あたり
		{
			ChangeState(State::Move);
		}
		break;
    case State::Dead:
        _controller.SetMoveVelocity(0.0f, 0.0f);   // 今は止まるだけ
        break;
    }
}

void Player::UpdateMove(float deltaTime, const Input& input, float cameraYaw, bool inputEnabled)
{
	// -- 1. 入力を「前後」「左右」の量に変換する --
	float inputX = 0.0f;   // 右が +
	float inputZ = 0.0f;   // 前が +
	if (inputEnabled)
	{
		if (input.IsPressed('W')) inputZ += 1.0f;
		if (input.IsPressed('S')) inputZ -= 1.0f;
		if (input.IsPressed('D')) inputX += 1.0f;
		if (input.IsPressed('A')) inputX -= 1.0f;
	}

	const bool onGround = _controller.IsGrounded();

	// -- 2. ジャンプ --
	if (inputEnabled && input.IsTriggered(VK_SPACE))
	{
		_controller.Jump();
	}

	if (inputEnabled && input.IsTriggered(VK_LBUTTON) && _controller.IsGrounded())
	{
		ChangeState(State::Attack);
		return;   // この後の移動処理はしない
	}

	// -- 3. 移動 --
	if (inputX == 0.0f && inputZ == 0.0f)
	{
		_controller.SetMoveVelocity(0.0f, 0.0f);   //速度を止めるのを忘れずに
		if (onGround) _actor.PlayAnimation("Idle");
	}
	else
	{
		// -- カメラの向きを基準に、ワールドでの進行方向を作る --
		const DirectX::XMFLOAT3 forward = { sinf(cameraYaw), 0.0f, cosf(cameraYaw) };
		const DirectX::XMFLOAT3 right = { forward.z, 0.0f, -forward.x };

		DirectX::XMVECTOR dir = DirectX::XMVectorAdd(
			DirectX::XMVectorScale(DirectX::XMLoadFloat3(&forward), inputZ),
			DirectX::XMVectorScale(DirectX::XMLoadFloat3(&right), inputX));
		dir = DirectX::XMVector3Normalize(dir);

		const bool isRunning = input.IsPressed(VK_SHIFT);
		const float speed = isRunning ? _runSpeed : _walkSpeed;

		DirectX::XMFLOAT3 d;
		DirectX::XMStoreFloat3(&d, dir);
		_controller.SetMoveVelocity(d.x * speed, d.z * speed);

		// 向きの補間（位置は動かさない）
		const float targetYaw = atan2f(d.x, d.z);
		float diff = DirectX::XMScalarModAngle(targetYaw - _actor.RotationY());
		const float t = std::min(1.0f, TurnSpeed * deltaTime);
		_actor.SetRotationY(_actor.RotationY() + diff * t);

		if (onGround) _actor.PlayAnimation(isRunning ? "Run" : "Walk");
	}

	// -- 4. 空中のアニメーション --
	if (!onGround) _actor.PlayAnimation("Curl_up_loop", 0.1f);
}

bool Player::IsAttackActive() const
{
	return _state == State::Attack && _stateTime >= HitStart && _stateTime < HitEnd;
}

Sphere Player::AttackSphere() const
{
	const float yaw = _actor.RotationY();
	const auto& p = _actor.Position();

	// 体の正面 0.8m、高さ 0.4m の位置に半径 0.6m の球を置く
	return {
			{ p.x + sinf(yaw) * 0.8f, p.y + 0.4f, p.z + cosf(yaw) * 0.8f },
			0.6f
	};
}

void Player::ChangeState(State next)
{
	if (_state == next) return;
	_state = next;
	_stateTime = 0.0f;

	switch (_state)
	{
	case State::Move:    /* Move 中は UpdateMove がアニメーションを決める */ break;
	case State::Attack:  _actor.PlayAnimation("Curl_up1", 0.05f); ++_attackId; break;
	case State::Damaged: _actor.PlayAnimation("Curl_up_loop", 0.05f); break;
	case State::Dead:    _actor.PlayAnimation("Curl_up_loop", 0.2f); break;
	}
}

void Player::TakeDamage(int amount)
{
	if (_state == State::Dead) return;
	if (_invincibleTime > 0.0f) return;   // 無敵中は無視

	_hp -= amount;
	_invincibleTime = InvincibleTime;
	ChangeState(_hp > 0 ? State::Damaged : State::Dead);
}

Sphere Player::BodySphere() const
{
	return _controller.BodySphere();
}

const char* Player::StateName() const
{
	switch (_state)
	{
	case State::Move:    return "Move";
	case State::Attack:  return "Attack";
	case State::Damaged: return "Damaged";
	case State::Dead:    return "Dead";
	}
	return "Unknown";
}