#include "EnemyPool.h"
#include "Dx12Wrapper.h"
#include "GltfRenderer.h"
#include "GltfModel.h"
#include "GltfActor.h"
#include "CharacterController.h"
#include "Enemy.h"
#include "Util.h"

EnemyPool::EnemyPool(Dx12Wrapper& dx12, GltfRenderer& renderer, GltfModel& model)
	:_dx12(dx12), _renderer(renderer), _model(model)
{ }

EnemyPool::~EnemyPool()
{
    if (_mappedInstance != nullptr)
    {
        _instanceBuff->Unmap(0, nullptr);
        _mappedInstance = nullptr;
    }

    if (_mappedBone != nullptr)
    {
        _boneBuff->Unmap(0, nullptr);
        _mappedBone = nullptr;
    }
}

bool EnemyPool::Init(size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        auto actor = std::make_unique<GltfActor>(_dx12, _renderer, _model);
        if (!actor->Init(false)) return false;
        actor->SetVisible(false);          // 最初は非表示
        _actors.push_back(std::move(actor));

        auto ctrl = std::make_unique<CharacterController>(*_actors.back());
        ctrl->SetRadius(GameConfig::Enemy::Radius);   // アリは小さい
        ctrl->SetCollisionEnabled(false);  // 使用中になるまで判定しない
        _controllers.push_back(std::move(ctrl));

        _enemies.push_back(std::make_unique<Enemy>(*_actors.back(), *_controllers.back()));
        _active.push_back(false);
    }
    _activeLimit = count;
    if (!CreateInstanceBuffers()) return false;
    return true;
}

bool EnemyPool::CreateInstanceBuffers()
{
    _jointCount = static_cast<UINT>(_model.JointNodes().size());
    const UINT capacity = static_cast<UINT>(_enemies.size());

    _instanceStride = static_cast<UINT>(AlignmentedSize(
        sizeof(GltfInstanceData) * capacity, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
    _boneStride = static_cast<UINT>(AlignmentedSize(
        sizeof(DirectX::XMMATRIX) * capacity * _jointCount, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
    _visibleStride = static_cast<UINT>(AlignmentedSize(
        sizeof(UINT) * capacity, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));

    // ヒープの設定
    D3D12_HEAP_PROPERTIES transformheapProp = {};

    transformheapProp.Type = D3D12_HEAP_TYPE_UPLOAD;        // CPUからアクセス可能(マップ可能)
    transformheapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    transformheapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // リソースの設定
    D3D12_RESOURCE_DESC transformresDesc = {};

    transformresDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    transformresDesc.Width = _instanceStride * Dx12Wrapper::FrameCount;
    transformresDesc.Height = 1;                          // 必ず 1
    transformresDesc.DepthOrArraySize = 1;                // 必ず 1
    transformresDesc.MipLevels = 1;                       // 必ず 1
    transformresDesc.Format = DXGI_FORMAT_UNKNOWN;        // 必ず UNKNOWN
    transformresDesc.SampleDesc.Count = 1;                // 必ず 1
    transformresDesc.SampleDesc.Quality = 0;              // 必ず 0
    transformresDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // 必ず ROW_MAJOR
    transformresDesc.Flags = D3D12_RESOURCE_FLAG_NONE;    // 用途次第

    auto result = _dx12.Device()->CreateCommittedResource(
        &transformheapProp,
        D3D12_HEAP_FLAG_NONE,
        &transformresDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&_instanceBuff)
    );
    if (!CheckResult(result, "CreateCommittedResource _instanceBuff")) return false;

    // 毎フレーム書き換えるので、マップしたままにしておく(Unmap はデストラクタで行う)
    result = _instanceBuff->Map(0, nullptr, reinterpret_cast<void**>(&_mappedInstance));
    if (!CheckResult(result, "_instanceBuff->Map")) return false;

    transformresDesc.Width = _boneStride * Dx12Wrapper::FrameCount;

    result = _dx12.Device()->CreateCommittedResource(
        &transformheapProp,
        D3D12_HEAP_FLAG_NONE,
        &transformresDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&_boneBuff)
    );
    if (!CheckResult(result, "CreateCommittedResource _boneBuff")) return false;

    // 毎フレーム書き換えるので、マップしたままにしておく(Unmap はデストラクタで行う)
    result = _boneBuff->Map(0, nullptr, reinterpret_cast<void**>(&_mappedBone));
    if (!CheckResult(result, "_boneBuff->Map")) return false;

    transformresDesc.Width = _visibleStride * GameConfig::CascadeCount * Dx12Wrapper::FrameCount;

    result = _dx12.Device()->CreateCommittedResource(
        &transformheapProp,
        D3D12_HEAP_FLAG_NONE,
        &transformresDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&_visibleBuff)
    );
    if (!CheckResult(result, "CreateCommittedResource _boneBuff")) return false;

    result = _visibleBuff->Map(0, nullptr, reinterpret_cast<void**>(&_mappedVisible));
    if (!CheckResult(result, "_visibleBuff->Map")) return false;

    _slotActors.resize(capacity);

    return true;
}

D3D12_GPU_VIRTUAL_ADDRESS EnemyPool::InstanceAddress() const
{
    return _instanceBuff->GetGPUVirtualAddress() + _dx12.FrameIndex() * _instanceStride;
}

D3D12_GPU_VIRTUAL_ADDRESS EnemyPool::BoneAddress() const
{
    // 同じリソースの中の bones の位置を指す
    return _boneBuff->GetGPUVirtualAddress() + _dx12.FrameIndex() * _boneStride;
}

bool EnemyPool::BuildInstanceData()
{
    auto* inst = reinterpret_cast<GltfInstanceData*>(
        _mappedInstance + _dx12.FrameIndex() * _instanceStride);
    auto* bones = reinterpret_cast<DirectX::XMMATRIX*>(
        _mappedBone + _dx12.FrameIndex() * _boneStride);

    _drawCount = 0;
    for (size_t i = 0; i < _actors.size(); ++i)
    {
        // 描画側と同じ条件で選ぶこと（食い違うと段階 3 で行列がずれる）
        if (!_active[i] || !_actors[i]->IsVisible()) continue;

        const UINT slot = _drawCount++;
        _slotActors[slot] = _actors[i].get();     // 可視リストを作るときに引く

        inst[slot].world = _actors[i]->WorldMatrix();
        inst[slot].boneOffset = slot * _jointCount;

        const auto& bm = _actors[i]->BoneMatrices();
        std::copy(bm.begin(), bm.end(), bones + slot * _jointCount);
    }

    // カスケードごとに「どの体を描くか」の一覧を作る。
    // 箱は同心なので、小さいカスケードに入る体は大きいほうにも必ず入る
    for (UINT c = 0; c < GameConfig::CascadeCount; ++c)
    {
        auto* list = reinterpret_cast<UINT*>(_mappedVisible
            + (_dx12.FrameIndex() * GameConfig::CascadeCount + c) * _visibleStride);

        UINT n = 0;
        for (UINT slot = 0; slot < _drawCount; ++slot)
        {
            if (!CastsShadowIn(*_slotActors[slot], c)) continue;
            list[n++] = slot;
        }
        _visibleCount[c] = n;
    }
    return true;
}

bool EnemyPool::Spawn(const DirectX::XMFLOAT3& pos, float speedScale)
{
    for (size_t i = 0; i < _activeLimit; ++i)
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

    {
        AccumTimer t(Profile::msInstance);
        BuildInstanceData();
    }
}

void EnemyPool::Draw()
{
    if (_drawCount == 0) return;

    auto cmdList = _dx12.CommandList();
    _renderer.SetModelHeap(_model);

    cmdList->SetGraphicsRootConstantBufferView(0, _dx12.SceneConstantBufferAddress());
    cmdList->SetGraphicsRootShaderResourceView(1, InstanceAddress());
    cmdList->SetGraphicsRootShaderResourceView(4, BoneAddress());
    cmdList->SetGraphicsRootDescriptorTable(3, _model.ShadowHandle());

    cmdList->IASetVertexBuffers(0, 1, &_model.VertexBufferView());
    cmdList->IASetIndexBuffer(&_model.IndexBufferView());

    // プリミティブごとに、担当する範囲だけを描く
    // 0 周目: 輪郭線
    // 押し出したメッシュの裏面を描き、本体で覆われなかった縁だけが線として残る
    // 半透明マテリアルは面の縁が出てしまうため除外する
    if (_outlineEnabled)
    {
        cmdList->SetPipelineState(_renderer.OutlinePipelineState());
        cmdList->SetGraphicsRootDescriptorTable(2, _model.MaterialHandle(0));
        for (const auto& p : _model.Primitives())
        {
            if (p.isBlend) continue;

            cmdList->DrawIndexedInstanced(
                p.indexCount, _drawCount, p.startIndexLocation, p.baseVertexLocation, 0
            );
        }
    }

    // 1 周目: 不透明
    cmdList->SetPipelineState(_renderer.PipelineState());
    for (const auto& p : _model.Primitives())
    {
        if (p.isBlend) continue;        // 半透明は 2 周目で描く

        auto matIdx = (p.materialIndex >= 0) ? p.materialIndex : 0;
        cmdList->SetGraphicsRootDescriptorTable(2, _model.MaterialHandle(matIdx));

        cmdList->DrawIndexedInstanced(
            p.indexCount, _drawCount, p.startIndexLocation, p.baseVertexLocation, 0
        );
    }

    // 2 周目: 半透明(背景が確定してからでないとブレンドできない)
    cmdList->SetPipelineState(_renderer.BlendPipelineState());
    for (const auto& p : _model.Primitives())
    {
        if (!p.isBlend) continue;

        auto matIdx = (p.materialIndex >= 0) ? p.materialIndex : 0;
        cmdList->SetGraphicsRootDescriptorTable(2, _model.MaterialHandle(matIdx));

        cmdList->DrawIndexedInstanced(
            p.indexCount, _drawCount, p.startIndexLocation, p.baseVertexLocation, 0
        );
    }
}

void EnemyPool::DrawShadow()
{
    const UINT c = _dx12.CurrentCascade();
    if (_visibleCount[c] == 0) return;

    auto cmdList = _dx12.CommandList();

    cmdList->SetGraphicsRootConstantBufferView(0, _dx12.SceneConstantBufferAddress());
    cmdList->SetGraphicsRootShaderResourceView(1, InstanceAddress());   // t2
    cmdList->SetGraphicsRootShaderResourceView(2, BoneAddress());       // t3
    cmdList->SetGraphicsRootShaderResourceView(4, VisibleAddress(c));   // t4

    cmdList->IASetVertexBuffers(0, 1, &_model.VertexBufferView());
    cmdList->IASetIndexBuffer(&_model.IndexBufferView());

    // 全プリミティブをまとめて描く（半透明は影を落とさない方が自然なので除外）
    for (const auto& prim : _model.Primitives())
    {
        if (prim.isBlend) continue;
        cmdList->DrawIndexedInstanced(prim.indexCount, _visibleCount[c], prim.startIndexLocation, prim.baseVertexLocation, 0);
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

void EnemyPool::SetActiveLimit(size_t n)
{
    _activeLimit = std::min(n, _enemies.size());

    // 上限を下げたとき、範囲外で生きているものを片付ける
    // （放っておくと、倒されるまで残り続けて数が合わなくなる）
    for (size_t i = _activeLimit; i < _active.size(); ++i)
    {
        if (!_active[i]) continue;
        _active[i] = false;
        _actors[i]->SetVisible(false);
        _controllers[i]->SetCollisionEnabled(false);
    }
}

D3D12_GPU_VIRTUAL_ADDRESS EnemyPool::VisibleAddress(UINT c) const
{
    return _visibleBuff->GetGPUVirtualAddress()
        + (_dx12.FrameIndex() * GameConfig::CascadeCount + c) * _visibleStride;
}

bool EnemyPool::CastsShadowIn(const GltfActor& actor, UINT cascade) const
{
    const auto pos = DirectX::XMLoadFloat3(&actor.Position());
    const auto clip = DirectX::XMVector3TransformCoord(pos, _dx12.LightCamera(cascade));

    DirectX::XMFLOAT3 ndc;
    DirectX::XMStoreFloat3(&ndc, clip);

    // 行列と箱の大きさは、必ず同じカスケードから取る
    const float margin = GameConfig::Enemy::ShadowMargin * 2.0f
        / (GameConfig::CascadeAreas[cascade] * _dx12.ShadowAreaScale());

    return fabsf(ndc.x) <= 1.0f + margin && fabsf(ndc.y) <= 1.0f + margin;
}
