#include "GltfActor.h"

#include "Dx12Wrapper.h"
#include "GltfRenderer.h"
#include "Util.h"

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

bool GltfActor::Init()
{
	_boneMatrices.resize(_model.JointNodes().size());
	if (!CreateTransformBuffer()) return false;
	_animNodes = _model.Nodes();
	_blendNodes = _model.Nodes();
	UpdateBoneMatrices();
	if (!CreateMaterialAndTextureView()) return false;

	return true;
}

void GltfActor::Update(float deltaTime)
{
	if (_currentAnimation >= 0)
	{
		// -- 再生位置を進める --
		_animTime = std::fmod(_animTime + deltaTime, _model.Animations()[_currentAnimation].duration);

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
			SampleAnimation(_currentAnimation, _animTime, _animNodes);
		}
		else
		{
			SampleAnimation(_prevAnimation, _prevAnimTime, _blendNodes);   // 移行元
			SampleAnimation(_currentAnimation, _animTime, _animNodes);     // 移行先

			// 残り時間が減るほど、移行先の比率が上がる
			const float weight = 1.0f - (_blendRemain / _blendDuration);
			BlendNodes(_blendNodes, _animNodes, weight, _animNodes);
		}
	}

	UpdateBoneMatrices();
	UpdateWorldMatrix();
}

void GltfActor::UpdateWorldMatrix()
{
	_mappedTransform->world =
		DirectX::XMMatrixScaling(_scale, _scale, _scale)
		* DirectX::XMMatrixRotationY(_rotationY + ModelYawOffset)
		* DirectX::XMMatrixTranslation(_position.x, _position.y, _position.z);
}

void GltfActor::Draw()
{
	auto cmdList = _dx12.CommandList();

	// ディスクリプタヒープの指定
	// CBV_SRV_UAV のヒープは同時に 1 本しかバインドできない
	// (切り替えると、それ以前に設定したディスクリプタテーブルは無効になる)
	// ComPtr の operator& は中身を Release してしまうため、必ず Get() を使うこと
	ID3D12DescriptorHeap* heaps[] = { _descHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	// ヒープの先頭ハンドル(0 番 = シーン用の定数バッファ)
	auto heapH = _descHeap->GetGPUDescriptorHandleForHeapStart();

	// ルートパラメーター 0 番に行列(b0)を関連付ける
	cmdList->SetGraphicsRootDescriptorTable(0, heapH);

	// 頂点バッファの設定
	cmdList->IASetVertexBuffers(0, 1, &_model.VertexBufferView());

	// インデックスバッファの設定
	cmdList->IASetIndexBuffer(&_model.IndexBufferView());

	auto incSize = _dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
	// プリミティブごとに、担当する範囲だけを描く
	// 0 周目: 輪郭線
	// 押し出したメッシュの裏面を描き、本体で覆われなかった縁だけが線として残る
	// 半透明マテリアルは面の縁が出てしまうため除外する
	if (_outlineEnabled)
	{
		cmdList->SetPipelineState(_renderer.OutlinePipelineState());
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
		if (p.isBlend) continue;        // ← この行が抜けている

		auto matIdx = (p.materialIndex >= 0) ? p.materialIndex : 0;
		auto handle = heapH;
		handle.ptr += incSize * (2 + matIdx * GltfModel::DescriptorsPerMaterial);
		cmdList->SetGraphicsRootDescriptorTable(1, handle);

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
		auto handle = heapH;
		handle.ptr += incSize * (2 + matIdx * GltfModel::DescriptorsPerMaterial);
		cmdList->SetGraphicsRootDescriptorTable(1, handle);

		cmdList->DrawIndexedInstanced(
			p.indexCount, 1, p.startIndexLocation, p.baseVertexLocation, 0
		);
	}
}

void GltfActor::DrawShadow()
{
	auto cmdList = _dx12.CommandList();

	cmdList->SetPipelineState(_renderer.ShadowPipelineState());
	cmdList->SetGraphicsRootSignature(_renderer.RootSignature());

	ID3D12DescriptorHeap* heaps[] = { _descHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	// b0(シーン) と b1(ボーン) だけ渡す。マテリアルとテクスチャは要らない
	cmdList->SetGraphicsRootDescriptorTable(0, _descHeap->GetGPUDescriptorHandleForHeapStart());

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->IASetVertexBuffers(0, 1, &_model.VertexBufferView());
	cmdList->IASetIndexBuffer(&_model.IndexBufferView());

	// 全プリミティブをまとめて描く（半透明は影を落とさない方が自然なので除外）
	for (const auto& prim : _model.Primitives())
	{
		if (prim.isBlend) continue;
		cmdList->DrawIndexedInstanced(prim.indexCount, 1, prim.startIndexLocation, prim.baseVertexLocation, 0);
	}
}


bool GltfActor::CreateMaterialAndTextureView()
{
	auto device = _dx12.Device();
	auto materialNum = static_cast<UINT>(_model.MaterialCount());

	// ディスクリプタヒープの作成
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NodeMask = 0;
	// 先頭はシーン用の定数バッファ(b0)、続いてボーン行列(b1)、それ以降がマテリアル(b2)
	descHeapDesc.NumDescriptors = static_cast<UINT>(_model.MaterialCount()) * GltfModel::DescriptorsPerMaterial + 2;
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	auto result = device->CreateDescriptorHeap(&descHeapDesc, IID_PPV_ARGS(&_descHeap));
	if (!CheckResult(result, "CreateDescriptorHeap _descHeap")) return false;

	// 通常テクスチャビュー作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	// デフォルト
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;	// 2D テクスチャ
	srvDesc.Texture2D.MipLevels = 1;	// ミップマップは使用しないので 1

	// ビューの作成
	D3D12_CONSTANT_BUFFER_VIEW_DESC matCBVDesc = {};

	matCBVDesc.BufferLocation = _model.MaterialBuffer()->GetGPUVirtualAddress();	// バッファアドレス
	matCBVDesc.SizeInBytes = static_cast<UINT>(_model.MaterialStride());	// マテリアルバッファサイズ

	// ディスクリプタヒープの先頭を記録
	auto descHeapH = _descHeap->GetCPUDescriptorHandleForHeapStart();

	auto incSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	auto handle = descHeapH;

	// 先頭(0 番)にシーン用の定数バッファビュー(b0)を作る
	_dx12.CreateSceneConstantBufferView(descHeapH);
	handle.ptr += incSize;

	// [1] ボーン行列用の定数バッファビュー(b1)
	D3D12_CONSTANT_BUFFER_VIEW_DESC transformCBVDesc = {};
	transformCBVDesc.BufferLocation = _transformBuff->GetGPUVirtualAddress();
	transformCBVDesc.SizeInBytes = static_cast<UINT>(_transformBuff->GetDesc().Width);
	device->CreateConstantBufferView(&transformCBVDesc, handle);
	handle.ptr += incSize;

	// テクスチャが指定されなかったときに使う既定のテクスチャ
	auto whiteTex = _dx12.WhiteTexture();
	auto blackTex = _dx12.BlackTexture();
	auto gradTex = _dx12.GradTexture();

	// 3 番目以降にマテリアルの定数バッファビュー(b2)をマテリアル数ぶん並べる
	for (UINT i = 0; i < materialNum; ++i)
	{
		// マテリアル用CBV
		device->CreateConstantBufferView(&matCBVDesc, handle);
		handle.ptr += incSize;
		matCBVDesc.BufferLocation += _model.MaterialStride();

		// テクスチャ用のSRV
		auto tex = _model.MaterialTexture(i) != nullptr ? _model.MaterialTexture(i) : _dx12.WhiteTexture();
		srvDesc.Format = tex->GetDesc().Format;
		device->CreateShaderResourceView(tex, &srvDesc, handle);
		handle.ptr += incSize;
	}

	return true;
}

bool GltfActor::CreateTransformBuffer()
{
	// ヒープの設定
	D3D12_HEAP_PROPERTIES transformHeapProp = {};

	transformHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD;        // CPUからアクセス可能(マップ可能)
	transformHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	transformHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// リソースの設定
	D3D12_RESOURCE_DESC transformResDesc = {};

	transformResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	transformResDesc.Width = AlignmentedSize(
		sizeof(TransformBufferData),
		D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
	);
	transformResDesc.Height = 1;                          // 必ず 1
	transformResDesc.DepthOrArraySize = 1;                // 必ず 1
	transformResDesc.MipLevels = 1;                       // 必ず 1
	transformResDesc.Format = DXGI_FORMAT_UNKNOWN;        // 必ず UNKNOWN
	transformResDesc.SampleDesc.Count = 1;                // 必ず 1
	transformResDesc.SampleDesc.Quality = 0;              // 必ず 0
	transformResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // 必ず ROW_MAJOR
	transformResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;    // 用途次第

	auto result = _dx12.Device()->CreateCommittedResource(
		&transformHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&transformResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_transformBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _transformBuff")) return false;

	// 毎フレーム書き換えるので、マップしたままにしておく(Unmap はデストラクタで行う)
	result = _transformBuff->Map(0, nullptr, (void**)&_mappedTransform);
	if (!CheckResult(result, "_transformBuff->Map")) return false;

	// 使っていない分を触っても壊れないよう、全体を単位行列で埋めてから流し込む
	_mappedTransform->world = DirectX::XMMatrixIdentity();
	std::fill_n(_mappedTransform->bones, GltfModel::MaxBoneCount, DirectX::XMMatrixIdentity());
	std::copy(_boneMatrices.begin(), _boneMatrices.end(), _mappedTransform->bones);

	return true;
}

void GltfActor::UpdateBoneMatrices()
{
	// 全ノードのグローバル変換を求める
	std::vector<DirectX::XMMATRIX> globals(_animNodes.size());
	for (int i : _model.NodeOrder())
	{
		const GltfModel::Node& n = _animNodes[i];
		auto local =
			DirectX::XMMatrixScaling(n.scale.x, n.scale.y, n.scale.z)
			* DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&n.rotation))
			* DirectX::XMMatrixTranslation(n.translation.x, n.translation.y, n.translation.z);

		globals[i] = (n.parent < 0) ? local : local * globals[n.parent];
	}

	// joint ごとに 逆バインド行列 × グローバル変換
	for (size_t j = 0; j < _model.JointNodes().size(); ++j)
	{
		_boneMatrices[j] = _model.InverseBindMatrices()[j] * globals[_model.JointNodes()[j]];
	}

	// GPU へ
	std::fill_n(_mappedTransform->bones, GltfModel::MaxBoneCount, DirectX::XMMatrixIdentity());
	std::copy(_boneMatrices.begin(), _boneMatrices.end(), _mappedTransform->bones);

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
void GltfActor::SampleAnimation(int animIndex, float timeSec, std::vector<GltfModel::Node>& out) const
{
	// バインドポーズからやり直す（動かさないノードを残すため）
	out = _model.Nodes();

	if (animIndex < 0) return;
	const GltfModel::Animation& anim = _model.Animations()[animIndex];

	for (const auto& ch : anim.channels)
	{
		if (ch.times.empty()) continue;
		GltfModel::Node& node = out[ch.targetNode];

		// 現在時刻を挟む 2 キーを探す（times は昇順が保証されている）
		size_t k1 = 0;
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

void GltfActor::ApplyAnimation(float timeSec)
{
	// バインドポーズからやり直す（動かさないノードを残すため）
	_animNodes = _model.Nodes();

	if (_currentAnimation < 0) return;
	const GltfModel::Animation& anim = _model.Animations()[_currentAnimation];

	for (const auto& ch : anim.channels)
	{
		if (ch.times.empty()) continue;
		GltfModel::Node& node = _animNodes[ch.targetNode];

		// 現在時刻を挟む 2 キーを探す（times は昇順が保証されている）
		size_t k1 = 0;
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

bool GltfActor::PlayAnimation(const std::string& name, float blendSeconds)
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
		return true;
	}
	std::cout << "PlayAnimation " << name << " is not found" << std::endl;
	return false;
}