#include "EnemyPool.h"
#include "Dx12Wrapper.h"
#include "GltfRenderer.h"
#include "GltfModel.h"
#include "GltfActor.h"
#include "CharacterController.h"
#include "Enemy.h"

EnemyPool::EnemyPool(Dx12Wrapper& dx12, GltfRenderer& renderer, GltfModel& model)
	:_dx12(dx12), _renderer(renderer), _model(model)
{ }

EnemyPool::~EnemyPool() = default;

bool EnemyPool::Init(size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        auto actor = std::make_unique<GltfActor>(_dx12, _renderer, _model);
        if (!actor->Init()) return false;
        actor->SetVisible(false);          // 最初は非表示
        _actors.push_back(std::move(actor));

        auto ctrl = std::make_unique<CharacterController>(*_actors.back());
        ctrl->SetRadius(0.3f);             // アリは小さい
        ctrl->SetCollisionEnabled(false);  // 使用中になるまで判定しない
        _controllers.push_back(std::move(ctrl));

        _enemies.push_back(std::make_unique<Enemy>(*_actors.back(), *_controllers.back()));
        _active.push_back(false);
    }
    return true;
}

bool EnemyPool::Spawn(const DirectX::XMFLOAT3& pos, float speedScale)
{
    for (size_t i = 0; i < _active.size(); ++i)
    {
        if (_active[i]) continue;   // 使用中は飛ばす

        _active[i] = true;
        _enemies[i]->Reset(pos);              // HP・状態・位置を初期化
        _enemies[i]->SetChaseSpeed(GameConfig::Enemy::ChaseSpeed * speedScale);
        _actors[i]->SetVisible(true);
        _controllers[i]->SetCollisionEnabled(true);
        _actors[i]->Update(0.0f);
        return true;
    }
    return false;   // 空きが無い（同時出現数の上限に達している）
}

void EnemyPool::Update(float deltaTime, const GameConfig::Enemy::Target& king, const GameConfig::Enemy::Target& player)
{
    for (size_t i = 0; i < _enemies.size(); ++i)
    {
        if (!_active[i]) continue;

        _enemies[i]->Update(deltaTime, king, player);

        if (!_actors[i]->IsVisible())
        {
            _active[i] = false;
            _controllers[i]->SetCollisionEnabled(false);
        }
    }
}

void EnemyPool::UpdateActors(float deltaTime)
{
    for (size_t i = 0; i < _actors.size(); ++i)
    {
        if (!_active[i]) continue;
        _actors[i]->Update(deltaTime);
    }
}

void EnemyPool::Draw()
{
    for (size_t i = 0; i < _actors.size(); ++i)
    {
        if (!_active[i]) continue;
        _actors[i]->Draw();
    }
}

void EnemyPool::DrawShadow()
{
    for (size_t i = 0; i < _actors.size(); ++i)
    {
        if (!_active[i]) continue;
        _actors[i]->DrawShadow();
    }
}

int EnemyPool::AliveCount() const
{
    int cnt = 0;
    for (size_t i = 0; i < _active.size(); ++i)
    {
        if (_active[i] && !_enemies[i]->IsDead()) ++cnt;
    }
    return cnt;
}

void EnemyPool::ResetAll()
{
    for (size_t i = 0; i < _active.size(); ++i)
    {
        _active[i] = false;
        _actors[i]->SetVisible(false);
        _controllers[i]->SetCollisionEnabled(false);
    }
}