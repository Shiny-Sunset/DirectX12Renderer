#pragma once
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include "GameConfig.h"

class Dx12Wrapper;
class GltfRenderer;
class GltfModel;
class GltfActor;
class CharacterController;
class Enemy;

// アリをあらかじめ作っておき、使い回すためのクラス
// 実行中にリソースを確保しないので、スポーン時にカクつかない
class EnemyPool
{
public:
    EnemyPool(Dx12Wrapper& dx12, GltfRenderer& renderer, GltfModel& model);
    ~EnemyPool();

    // @param count 用意する数。同時に存在できる最大数になる
    bool Init(size_t count);

    // 未使用の 1 体を指定位置に出す
    // @return 空きが無ければ false
    bool Spawn(const DirectX::XMFLOAT3& pos, float speedScale);

    // @param targetPos 敵が目指す位置（王）
    // @param targetRadius 目標の当たり判定の半径
    void Update(float deltaTime, const GameConfig::Enemy::Target& king, const GameConfig::Enemy::Target& player);

    // 見た目の更新（当たり判定の解決後に呼ぶ）
    void UpdateActors(float deltaTime);

    void Draw();
    void DrawShadow();

    // 全員を未使用に戻す（リスタート用）
    void ResetAll();

    int AliveCount() const;

    // 当たり判定の登録用。Application が持つ配列へ追加してもらう
    const std::vector<std::unique_ptr<CharacterController>>& Controllers() const { return _controllers; }

    // ヒット判定などで 1 体ずつ触るため
    size_t Size() const { return _enemies.size(); }
    Enemy* At(size_t i) { return _enemies[i].get(); }
    bool IsActive(size_t i) const { return _active[i]; }

private:
    Dx12Wrapper& _dx12;
    GltfRenderer& _renderer;
    GltfModel& _model;

    std::vector<std::unique_ptr<GltfActor>> _actors;
    std::vector<std::unique_ptr<CharacterController>> _controllers;
    std::vector<std::unique_ptr<Enemy>> _enemies;
    std::vector<bool> _active;   // 使用中かどうか
};