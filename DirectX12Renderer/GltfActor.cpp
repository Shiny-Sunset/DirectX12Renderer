#include "GltfActor.h"

#include "Dx12Wrapper.h"
#include "GltfRenderer.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
	// モデルの正面がワールドの -Z を向いているため、+Z を向くように補正する
	// (glTF は右手系なので、読み込み時に Z を反転している都合による)
	constexpr float ModelYawOffset = DirectX::XM_PI;
}

GltfActor::GltfActor(Dx12Wrapper& dx12, GltfRenderer& renderer, GltfModel& model)
	: _dx12(dx12), _renderer(renderer), _model(model)
{
}

GltfActor::~GltfActor()
{
	if (_mappedTransform != nullptr)
	{
		_transformBuff->Unmap(0, nullptr);
		_mappedTransform = nullptr;
	}
}

bool GltfActor::Init(bool ownBuffer)
{
	_boneMatrices.resize(_model.JointNodes().size());
	_globals.resize(_model.Nodes().size());
	if (!CreateTransformBuffer()) return false;
	_animNodes = _model.Nodes();
	_blendNodes = _model.Nodes();
	UpdateBoneMatrices();

	return true;
}

void GltfActor::Update(float deltaTime)
{
	if (_currentAnimation >= 0)
	{
		AccumTimer t(Profile::msSample);
		// -- 再生位置を進める --
		const float duration = _model.Animations()[_currentAnimation].duration;
		_animTime += deltaTime;

		if (_loop)
		{
			_animTime = std::fmod(_animTime, duration);
		}
		else
		{
			_animTime = std::min(_animTime, duration);   // 最後で止める
		}

		if (_prevAnimation >= 0)
		{
			// 移行元も止めずに進める（歩行サイクルが固まらないように）
			_prevAnimTime = std::fmod(_prevAnimTime + deltaTime, _model.Animations()[_prevAnimation].duration);

			_blendRemain -= deltaTime;
			if (_blendRemain <= 0.0f)
			{
				_prevAnimation = -1;   // 移行完了
			}
		}

		// -- ポーズを作る --
		if (_prevAnimation < 0)
		{
			SampleAnimation(_currentAnimation, _animTime, _animNodes, _animCache);
		}
		else
		{
			SampleAnimation(_prevAnimation, _prevAnimTime, _blendNodes, _blendCache);   // 移行元
			SampleAnimation(_currentAnimation, _animTime, _animNodes, _animCache);     // 移行先

			// 残り時間が減るほど、移行先の比率が上がる
			const float weight = 1.0f - (_blendRemain / _blendDuration);
			BlendNodes(_blendNodes, _animNodes, weight, _animNodes);
		}
	}

	AccumTimer t(Profile::msBone);
	UpdateBoneMatrices();
	UpdateWorldMatrix();
}

DirectX::XMMATRIX GltfActor::WorldMatrix() const
{
	return DirectX::XMMatrixScaling(_scale, _scale, _scale)
		* DirectX::XMMatrixRotationY(_rotationY + ModelYawOffset)
		* DirectX::XMMatrixTranslation(_position.x, _position.y, _position.z);
}

void GltfActor::UpdateWorldMatrix()
{
	if (_mappedTransform == nullptr) return;
	Transform()->instance.world = WorldMatrix();   // 式を 2 箇所に持たない
}

void GltfActor::Draw()
{
	assert(_mappedTransform != nullptr);
	if (!_visible) return;
	auto cmdList = _dx12.CommandList();
	// ルートパラメーター 0 番にシーン定数 b0
	cmdList->SetGraphicsRootConstantBufferView(0, _dx12.SceneConstantBufferAddress());
	cmdList->SetGraphicsRootShaderResourceView(1, InstanceAddress());   // t2
	cmdList->SetGraphicsRootShaderResourceView(4, BoneAddress());       // t3

	cmdList->SetGraphicsRootDescriptorTable(3, _model.ShadowHandle());	// [0] シャドウ

	// 頂点バッファの設定
	cmdList->IASetVertexBuffers(0, 1, &_model.VertexBufferView());

	// インデックスバッファの設定
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
				p.indexCount, 1, p.startIndexLocation, p.baseVertexLocation, 0
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
			p.indexCount, 1, p.startIndexLocation, p.baseVertexLocation, 0
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
			p.indexCount, 1, p.startIndexLocation, p.baseVertexLocation, 0
		);
	}
}

void GltfActor::DrawShadow()
{
	assert(_mappedTransform != nullptr);
	if (!_visible) return;
	auto cmdList = _dx12.CommandList();

	cmdList->SetGraphicsRootConstantBufferView(0, _dx12.SceneConstantBufferAddress());
	cmdList->SetGraphicsRootShaderResourceView(1, InstanceAddress());   // t2
	cmdList->SetGraphicsRootShaderResourceView(2, BoneAddress());       // t3
	cmdList->SetGraphicsRootShaderResourceView(4, _renderer.IdentityVisibleAddress());

	cmdList->IASetVertexBuffers(0, 1, &_model.VertexBufferView());
	cmdList->IASetIndexBuffer(&_model.IndexBufferView());

	// 全プリミティブをまとめて描く（半透明は影を落とさない方が自然なので除外）
	for (const auto& prim : _model.Primitives())
	{
		if (prim.isBlend) continue;
		cmdList->DrawIndexedInstanced(prim.indexCount, 1, prim.startIndexLocation, prim.baseVertexLocation, 0);
	}
}


bool GltfActor::CreateTransformBuffer()
{
	// ヒープの設定
	D3D12_HEAP_PROPERTIES transformheapProp = {};

	transformheapProp.Type = D3D12_HEAP_TYPE_UPLOAD;        // CPUからアクセス可能(マップ可能)
	transformheapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	transformheapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// リソースの設定
	D3D12_RESOURCE_DESC transformresDesc = {};

	transformresDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	_transformStride = static_cast<UINT>(AlignmentedSize(sizeof(TransformBufferData), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
	transformresDesc.Width = _transformStride * Dx12Wrapper::FrameCount;
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
		IID_PPV_ARGS(&_transformBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _transformBuff")) return false;

	// 毎フレーム書き換えるので、マップしたままにしておく(Unmap はデストラクタで行う)
	result = _transformBuff->Map(0, nullptr, reinterpret_cast<void**>(&_mappedTransform));
	if (!CheckResult(result, "_transformBuff->Map")) return false;

	// 使っていない分を触っても壊れないよう、全体を単位行列で埋めてから流し込む
	for (UINT i = 0; i < Dx12Wrapper::FrameCount; ++i)
	{
		auto* t = reinterpret_cast<TransformBufferData*>(_mappedTransform + i * _transformStride);
		t->instance.world = DirectX::XMMatrixIdentity();
		t->instance.boneOffset = 0;
		std::fill_n(t->bones, GltfModel::MaxBoneCount, DirectX::XMMatrixIdentity());
		std::copy(_boneMatrices.begin(), _boneMatrices.end(), t->bones);
	}

	return true;
}


void GltfActor::UpdateBoneMatrices()
{
	// 全ノードのグローバル変換を求める
	auto& globals = _globals;
	for (int i : _model.NodeOrder())
	{
		const GltfModel::Node& n = _animNodes[i];

		// S * R * T は形が決まっている（S は対角、T は 4 行目だけ）ので、
		// 回転行列の 0〜2 行目をスケール倍し、4 行目に平行移動を入れれば同じ結果になる。
	    // 行列積 2 回がベクトル乗算 3 回に減る
		auto local = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&n.rotation));
		local.r[0] = DirectX::XMVectorScale(local.r[0], n.scale.x);
		local.r[1] = DirectX::XMVectorScale(local.r[1], n.scale.y);
		local.r[2] = DirectX::XMVectorScale(local.r[2], n.scale.z);
		local.r[3] = DirectX::XMVectorSet(n.translation.x, n.translation.y, n.translation.z, 1.0f);

		globals[i] = (n.parent < 0) ? local : local * globals[n.parent];
	}

	// joint ごとに 逆バインド行列 × グローバル変換
	for (size_t j = 0; j < _model.JointNodes().size(); ++j)
	{
		_boneMatrices[j] = _model.InverseBindMatrices()[j] * globals[_model.JointNodes()[j]];
	}

	// 自分用のバッファを持たないアクターは、EnemyPool がまとめて詰める
	if (_mappedTransform != nullptr)
		std::copy(_boneMatrices.begin(), _boneMatrices.end(), Transform()->bones);

#ifdef _DEBUG
	static bool logged = false;
	if (!logged)
	{
		std::cout << "glTF skin: nodes=" << _animNodes.size()
			<< " joints=" << _model.JointNodes().size() << std::endl;

		// バインドポーズなら全ボーン行列が単位行列になるはず
		float maxErr = 0.0f;
		for (const auto& bm : _boneMatrices)
		{
			DirectX::XMFLOAT4X4 f; DirectX::XMStoreFloat4x4(&f, bm);
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 4; ++c)
					maxErr = std::max(maxErr, fabsf(f.m[r][c] - (r == c ? 1.0f : 0.0f)));
		}
		std::cout << "  bone matrix max error = " << maxErr << std::endl;

		std::cout << "glTF animations = " << _model.Animations().size() << std::endl;
		for (const auto& a : _model.Animations())
		{
			std::cout << "  '" << a.name << "' channels=" << a.channels.size()
				<< " duration=" << a.duration << std::endl;
		}
	}
#endif
}
void GltfActor::SampleAnimation(int animIndex, float timeSec,
	std::vector<GltfModel::Node>& out, SampleCache& cache) const 
{
	// バインドポーズからやり直す（動かさないノードを残すため）
	if (animIndex < 0) { out = _model.Nodes(); return; }

	const GltfModel::Animation& anim = _model.Animations()[animIndex];


	// -- バインドポーズの復元は、アニメが切り替わったときだけ --
		// 動かさないノードは誰も書き込まないので、1 度戻せば以後そのまま残る。
		// 動かすノードは毎フレーム上書きされるので、戻す必要がない
	if (cache.animIndex != animIndex)
	{
		out = _model.Nodes();
		cache.animIndex = animIndex;
		cache.cursor.assign(anim.channels.size(), 0);
	}

	for (size_t c = 0; c < anim.channels.size(); ++c)
	{
		const auto& ch = anim.channels[c];
		if (ch.times.empty()) continue;
		GltfModel::Node& node = out[ch.targetNode];

		// -- 前回見つけた位置から探す --
		// 再生位置はふつう前へ進むので、たいてい 0〜1 歩で見つかる。
		// ループで巻き戻ったときだけ先頭からやり直す（自己修復するので、
		// ループの検出を別に持つ必要がない）
		size_t& k1 = cache.cursor[c];
		if (k1 >= ch.times.size() || ch.times[k1] > timeSec) k1 = 0;
		while (k1 + 1 < ch.times.size() && ch.times[k1 + 1] <= timeSec) ++k1;

		DirectX::XMVECTOR value;
		if (ch.isStep || k1 + 1 >= ch.times.size())
		{
			// STEP、または最後のキーより後 → 手前のキーの値をそのまま使う
			value = DirectX::XMLoadFloat4(&ch.values[k1]);
		}
		else
		{
			const size_t k2 = k1 + 1;
			const float span = ch.times[k2] - ch.times[k1];
			const float t = (span > 0.0f) ? (timeSec - ch.times[k1]) / span : 0.0f;

			auto v1 = DirectX::XMLoadFloat4(&ch.values[k1]);
			auto v2 = DirectX::XMLoadFloat4(&ch.values[k2]);

			// 回転はクォータニオンなので球面線形補間、位置とスケールは線形補間
			value = (ch.path == 1)
				? DirectX::XMQuaternionSlerp(v1, v2, t)
				: DirectX::XMVectorLerp(v1, v2, t);
		}

		switch (ch.path)
		{
		case 0: DirectX::XMStoreFloat3(&node.translation, value); break;
		case 1: DirectX::XMStoreFloat4(&node.rotation, value);    break;
		case 2: DirectX::XMStoreFloat3(&node.scale, value);       break;
		}
	}
}

void GltfActor::BlendNodes(const std::vector<GltfModel::Node>& a, const std::vector<GltfModel::Node>& b,
	float weight, std::vector<GltfModel::Node>& out) const
{
	for (size_t i = 0; i < out.size(); ++i)
	{
		const GltfModel::Node& na = a[i];
		const GltfModel::Node& nb = b[i];
		GltfModel::Node& no = out[i];

		// 位置と拡大は線形補間
		DirectX::XMStoreFloat3(&no.translation, DirectX::XMVectorLerp(
			DirectX::XMLoadFloat3(&na.translation),
			DirectX::XMLoadFloat3(&nb.translation), weight));

		DirectX::XMStoreFloat3(&no.scale, DirectX::XMVectorLerp(
			DirectX::XMLoadFloat3(&na.scale),
			DirectX::XMLoadFloat3(&nb.scale), weight));

		// 回転はクォータニオンなので球面線形補間
		DirectX::XMStoreFloat4(&no.rotation, DirectX::XMQuaternionSlerp(
			DirectX::XMLoadFloat4(&na.rotation),
			DirectX::XMLoadFloat4(&nb.rotation), weight));

		no.parent = na.parent;   // 親子関係は両者で同じ
	}
}

bool GltfActor::PlayAnimation(const std::string& name, float blendSeconds, bool loop)
{
	for (size_t i = 0; i < _model.Animations().size(); ++i)
	{
		if (_model.Animations()[i].name != name) continue;
		if (_model.Animations()[i].duration <= 0.0f) continue;

		const int index = static_cast<int>(i);
		if (index == _currentAnimation) return true;   // 再生中のものなら何もしない

		// 今のアニメーションを「移行元」として残す
		if (_currentAnimation >= 0 && blendSeconds > 0.0f)
		{
			_prevAnimation = _currentAnimation;
			_prevAnimTime = _animTime;
			_blendRemain = blendSeconds;
			_blendDuration = blendSeconds;
		}
		else
		{
			_prevAnimation = -1;    // 即座に切り替え
			_blendRemain = 0.0f;
		}

		_currentAnimation = index;
		_animTime = 0.0f;
		_loop = loop;
		return true;
	}
	std::cout << "PlayAnimation " << name << " is not found" << std::endl;
	return false;
}

void GltfActor::StopAnimation()
{
	_currentAnimation = -1;
	_prevAnimation = -1;
	_animTime = 0.0f;
	_prevAnimTime = 0.0f;
	_blendRemain = 0.0f;

	// バインドポーズへ戻す
	_animNodes = _model.Nodes();
	UpdateBoneMatrices();
}

GltfActor::TransformBufferData* GltfActor::Transform()
{
	return reinterpret_cast<TransformBufferData*>(
		_mappedTransform + _dx12.FrameIndex() * _transformStride);
}

D3D12_GPU_VIRTUAL_ADDRESS GltfActor::InstanceAddress() const
{
	return _transformBuff->GetGPUVirtualAddress() + _dx12.FrameIndex() * _transformStride;
}

D3D12_GPU_VIRTUAL_ADDRESS GltfActor::BoneAddress() const
{
	// 同じリソースの中の bones の位置を指す
	return InstanceAddress() + offsetof(TransformBufferData, bones);
}