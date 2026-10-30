#pragma once
#include <d3d12.h>
#include <wrl.h>
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

    bool BuildInstanceData();

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
    UINT DrawCount() const { return _drawCount; }
    UINT ShadowCount(UINT c) const { return _visibleCount[c]; }

    // 全員を未使用に戻す（リスタート用）
    void ResetAll();

    int AliveCount() const;

    // 同時に使ってよい数の上限を変える
    // 下げたときは、はみ出した分をその場で片付ける
    void SetActiveLimit(size_t n);
    size_t ActiveLimit() const { return _activeLimit; }
    size_t Capacity() const { return _enemies.size(); }

    // 当たり判定の登録用。Application が持つ配列へ追加してもらう
    const std::vector<std::unique_ptr<CharacterController>>& Controllers() const { return _controllers; }

    // ヒット判定などで 1 体ずつ触るため
    size_t Size() const { return _enemies.size(); }
    Enemy* At(size_t i) { return _enemies[i].get(); }
    bool IsActive(size_t i) const { return _active[i]; }

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreateInstanceBuffers();

    D3D12_GPU_VIRTUAL_ADDRESS InstanceAddress() const;
    D3D12_GPU_VIRTUAL_ADDRESS BoneAddress() const;

    bool CastsShadowIn(const GltfActor& actor, UINT cascade) const;
    D3D12_GPU_VIRTUAL_ADDRESS VisibleAddress(UINT c) const;

    Dx12Wrapper& _dx12;
    GltfRenderer& _renderer;
    GltfModel& _model;

    std::vector<std::unique_ptr<GltfActor>> _actors;
    std::vector<std::unique_ptr<CharacterController>> _controllers;
    std::vector<std::unique_ptr<Enemy>> _enemies;
    std::vector<bool> _active;   // 使用中かどうか

    size_t _activeLimit = 0;

    ComPtr<ID3D12Resource> _instanceBuff;   // InstanceData × 定員 × FrameCount
    ComPtr<ID3D12Resource> _boneBuff;       // XMMATRIX × 定員 × ジョイント数 × FrameCount
    ComPtr<ID3D12Resource> _visibleBuff;   // uint × 定員 × CascadeCount × FrameCount
    uint8_t* _mappedInstance = nullptr;
    uint8_t* _mappedBone = nullptr;
    uint8_t* _mappedVisible = nullptr;
    UINT _instanceStride = 0;   // 1 フレーム分のバイト数
    UINT _boneStride = 0;
    UINT _jointCount = 0;
    UINT _drawCount = 0;        // このフレームに詰めた数
    UINT _visibleStride = 0;               // 1 カスケード分のバイト数
    UINT _visibleCount[GameConfig::CascadeCount] = {};
    std::vector<GltfActor*> _slotActors;
    bool _outlineEnabled = true;
};
