#include "GltfActor.h"

#include "Dx12Wrapper.h"
#include "GltfRenderer.h"
#include "Util.h"

#define CGLTF_IMPLEMENTATION
#include "External/cgltf.h"

namespace
{
	// -- 座標系の変換 --
	// glTF は右手系(Y-up, -Z 前方)、このレンダラーは左手系。
	// Z 鏡映 M = diag(1, 1, -1, 1) による共役変換 M * A * M で取り込む。
	//
	// ★ glTF から座標・回転・行列を読むときは必ずこの関数を通すこと。
	//    片方だけ変換すると、バインドポーズでは気づけずアニメーションで初めて破綻する。

	// 位置・平行移動・法線・方向ベクトル
	DirectX::XMFLOAT3 ToLH(const float v[3])
	{
		return { v[0], v[1], -v[2] };
	}

	// クォータニオン
	// 回転軸が鏡映されるため x と y が反転する(z と w はそのまま)
	DirectX::XMFLOAT4 QuatToLH(const float q[4])
	{
		return { -q[0], -q[1], q[2], q[3] };
	}

	// 4x4 行列
	// 行と列の「どちらか一方だけ」が z 成分(添字 2)の要素を符号反転する
	void MatrixToLH(DirectX::XMFLOAT4X4& m)
	{
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				if ((r == 2) != (c == 2)) m.m[r][c] = -m.m[r][c];
			}
		}
	}

	// モデルの正面がワールドの -Z を向いているため、+Z を向くように補正する
	// (glTF は右手系なので、読み込み時に Z を反転している都合による)
	constexpr float ModelYawOffset = DirectX::XM_PI;
}

GltfActor::GltfActor(Dx12Wrapper& dx12, GltfRenderer& renderer)
	: _dx12(dx12), _renderer(renderer)
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

bool GltfActor::Init(const std::string& modelPath)
{
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

	if (!LoadGltfFile(modelPath, vertices, indices)) return false;
	if (!CreateVertexAndIndexBuffer(vertices, indices)) return false;
	if (!CreateMaterialBuffer()) return false;
	if (!CreateTransformBuffer()) return false;
	_animNodes = _nodes;	_blendNodes = _nodes;
	UpdateBoneMatrices();
	if (!CreateMaterialAndTextureView()) return false;

	return true;
}

void GltfActor::Update(float deltaTime)
{
	if (_currentAnimation >= 0)
	{
		// -- 再生位置を進める --
		_animTime = std::fmod(_animTime + deltaTime, _animations[_currentAnimation].duration);

		if (_prevAnimation >= 0)
		{
			// 移行元も止めずに進める（歩行サイクルが固まらないように）
			_prevAnimTime = std::fmod(_prevAnimTime + deltaTime, _animations[_prevAnimation].duration);

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
	cmdList->IASetVertexBuffers(0, 1, &_vbView);

	// インデックスバッファの設定
	cmdList->IASetIndexBuffer(&_ibView);

	auto incSize = _dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
	// プリミティブごとに、担当する範囲だけを描く
	// 0 周目: 輪郭線
	// 押し出したメッシュの裏面を描き、本体で覆われなかった縁だけが線として残る
	// 半透明マテリアルは面の縁が出てしまうため除外する
	if (_outlineEnabled)
	{
		cmdList->SetPipelineState(_renderer.OutlinePipelineState());
		for (const auto& p : _primitives)
		{
			if (p.isBlend) continue;

			cmdList->DrawIndexedInstanced(
				p.indexCount, 1, p.startIndexLocation, p.baseVertexLocation, 0
			);
		}
	}

	// 1 周目: 不透明
	cmdList->SetPipelineState(_renderer.PipelineState());
	for (const auto& p : _primitives)
	{
		if (p.isBlend) continue;        // ← この行が抜けている

		auto matIdx = (p.materialIndex >= 0) ? p.materialIndex : 0;
		auto handle = heapH;
		handle.ptr += incSize * (2 + matIdx * DescriptorsPerMaterial);
		cmdList->SetGraphicsRootDescriptorTable(1, handle);

		cmdList->DrawIndexedInstanced(
			p.indexCount, 1, p.startIndexLocation, p.baseVertexLocation, 0
		);
	}

	// 2 周目: 半透明(背景が確定してからでないとブレンドできない)
	cmdList->SetPipelineState(_renderer.BlendPipelineState());
	for (const auto& p : _primitives)
	{
		if (!p.isBlend) continue;

		auto matIdx = (p.materialIndex >= 0) ? p.materialIndex : 0;
		auto handle = heapH;
		handle.ptr += incSize * (2 + matIdx * DescriptorsPerMaterial);
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
	cmdList->IASetVertexBuffers(0, 1, &_vbView);
	cmdList->IASetIndexBuffer(&_ibView);

	// 全プリミティブをまとめて描く（半透明は影を落とさない方が自然なので除外）
	for (const auto& prim : _primitives)
	{
		if (prim.isBlend) continue;
		cmdList->DrawIndexedInstanced(prim.indexCount, 1, prim.startIndexLocation, prim.baseVertexLocation, 0);
	}
}

bool GltfActor::LoadGltfFile(
    const std::string& modelPath,
    std::vector<Vertex>& vertices,
    std::vector<uint16_t>& indices
)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    // ① JSON チャンクをパース
    cgltf_result res = cgltf_parse_file(&options, modelPath.c_str(), &data);
    if (res != cgltf_result_success) {
		std::cout << "Failed cgltf_parse_file" << std::endl;
		return false; 
	}

    // ② BIN チャンクを buffers[].data に載せる
    //    これを忘れるとアクセサ読み出しが全部失敗する
    res = cgltf_load_buffers(&options, data, modelPath.c_str());
    if (res != cgltf_result_success) { 
		cgltf_free(data); 
		return false; 
	}

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi)
    {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi)
        {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            // 必要なアクセサを拾う
            const cgltf_accessor* posAcc = nullptr;
            const cgltf_accessor* nrmAcc = nullptr;
            const cgltf_accessor* uvAcc = nullptr;
			const cgltf_accessor* jntAcc = nullptr;
			const cgltf_accessor* wgtAcc = nullptr;

            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
            {
                const cgltf_attribute& attr = prim.attributes[ai];
                if (attr.index != 0) continue;   // TEXCOORD_1 などは無視する

                switch (attr.type)
                {
                case cgltf_attribute_type_position: posAcc = attr.data; break;
                case cgltf_attribute_type_normal:   nrmAcc = attr.data; break;
                case cgltf_attribute_type_texcoord: uvAcc = attr.data; break;
				case cgltf_attribute_type_joints:   jntAcc = attr.data; break;
				case cgltf_attribute_type_weights:  wgtAcc = attr.data; break;
                default: break;
                }
            }
            if (posAcc == nullptr) continue;

            // この時点の vertices.size() / indices.size() が
            // baseVertexLocation / startIndexLocation になる

			 // この時点の値を控えてから頂点を積む
			Primitive p = {};
			p.baseVertexLocation = static_cast<INT>(vertices.size());
			p.startIndexLocation = static_cast<UINT>(indices.size());
			p.indexCount = static_cast<UINT>(prim.indices->count);
			p.materialIndex = (prim.material != nullptr)
				? static_cast<int>(prim.material - data->materials)
				: -1;
			p.isBlend = (prim.material != nullptr)
				&& (prim.material->alpha_mode == cgltf_alpha_mode_blend);

			// 頂点
			for (cgltf_size k = 0; k < posAcc->count; ++k)
			{
				Vertex v = {};
				float f[4] = {};

				cgltf_accessor_read_float(posAcc, k, f, 3);
				v.pos = ToLH(f);   // 右手系 → 左手系

				if (nrmAcc != nullptr)
				{
					cgltf_accessor_read_float(nrmAcc, k, f, 3);
					v.normal = ToLH(f);
				}
				if (uvAcc != nullptr)
				{
					cgltf_accessor_read_float(uvAcc, k, f, 2);
					v.uv = { f[0], f[1] };
				}
				if (jntAcc != nullptr)
				{
					cgltf_uint j[4] = {};
					cgltf_accessor_read_uint(jntAcc, k, j, 4);
					v.joints[0] = static_cast<uint8_t>(j[0]);
					v.joints[1] = static_cast<uint8_t>(j[1]);
					v.joints[2] = static_cast<uint8_t>(j[2]);
					v.joints[3] = static_cast<uint8_t>(j[3]);
				}
				if (wgtAcc != nullptr)
				{
					cgltf_accessor_read_float(wgtAcc, k, f, 4);
					v.weights = { f[0], f[1], f[2], f[3] };
				}
				else
				{
					// スキン情報が無い場合は先頭ボーンに 100% 割り当てて潰れを防ぐ
					v.weights = { 1.0f, 0.0f, 0.0f, 0.0f };
				}

				vertices.push_back(v);
			}

			// インデックス
			// 位置を Z 鏡映しているため面の裏表が反転する。
			// 巻き順(2 番目と 3 番目)を入れ替えて、D3D の既定(時計回り = 表)に合わせる。
			for (cgltf_size k = 0; k + 2 < prim.indices->count; k += 3)
			{
				indices.push_back(static_cast<uint16_t>(cgltf_accessor_read_index(prim.indices, k + 0)));
				indices.push_back(static_cast<uint16_t>(cgltf_accessor_read_index(prim.indices, k + 2)));
				indices.push_back(static_cast<uint16_t>(cgltf_accessor_read_index(prim.indices, k + 1)));
			}

			_primitives.push_back(p);
        }
    }

	// 輪郭線用の平均法線を作る（全プリミティブを積み終わってから）
	BuildSmoothNormals(vertices);

#ifdef _DEBUG
	std::cout << "glTF: meshes=" << data->meshes_count
		<< " primitives=" << _primitives.size()
		<< " vertices=" << vertices.size()
		<< " indices=" << indices.size() << std::endl;
#endif

	if (!LoadMaterials(data))
	{
		cgltf_free(data);
		return false;
	}

	if (!LoadNodesAndSkin(data))
	{
		cgltf_free(data);
		return false;
	}

	if (!LoadAnimations(data))
	{
		cgltf_free(data);
		return false;
	}

    cgltf_free(data);   // 解放を忘れずに

	return true;
}

bool GltfActor::LoadMaterials(const cgltf_data* data)
{
	_materials.resize(data->materials_count);
	_materialTextures.resize(data->materials_count);

	// 同じ画像を複数のマテリアルが参照するので、画像単位でキャッシュする
	std::unordered_map<const cgltf_image*, ComPtr<ID3D12Resource>> imageCache;
	for (cgltf_size i = 0; i < data->materials_count; ++i)
	{
		const cgltf_material& mat = data->materials[i];

		// -- ベースカラー係数 --
		// 指定が無ければ (1,1,1,1)
		_materials[i].baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
		if (mat.has_pbr_metallic_roughness)
		{
			const auto& f = mat.pbr_metallic_roughness.base_color_factor;
			_materials[i].baseColorFactor = { f[0], f[1], f[2], f[3] };
		}

		// -- ベースカラーテクスチャ --
		if (!mat.has_pbr_metallic_roughness) continue;

		const cgltf_texture* tex = mat.pbr_metallic_roughness.base_color_texture.texture;
		if (tex == nullptr || tex->image == nullptr) continue;

		const cgltf_image* img = tex->image;

		// 読み込み済みなら使い回す
		auto it = imageCache.find(img);
		if (it != imageCache.end())
		{
			_materialTextures[i] = it->second;
			continue;
		}

		// GLB は画像が bufferView に埋め込まれている
		if (img->buffer_view == nullptr) continue;   // 外部ファイル参照は未対応

		const uint8_t* bytes = cgltf_buffer_view_data(img->buffer_view);
		if (bytes == nullptr) continue;

		auto texbuff = _dx12.CreateTextureFromMemory(bytes, img->buffer_view->size);
		imageCache[img] = texbuff;
		_materialTextures[i] = texbuff;
	}
	return true;
}

bool GltfActor::LoadNodesAndSkin(const cgltf_data* data)
{
	// -- 全ノードの TRS と親を取り出す --
	_nodes.resize(data->nodes_count);
	for (cgltf_size i = 0; i < data->nodes_count; ++i)
	{
		const cgltf_node& n = data->nodes[i];
		Node& out = _nodes[i];

		out.parent = (n.parent != nullptr)
			? static_cast<int>(n.parent - data->nodes)   // ポインタ差でインデックス化
			: -1;

		if (n.has_translation) out.translation = ToLH(n.translation);
		if (n.has_rotation)    out.rotation = QuatToLH(n.rotation);
		if (n.has_scale)       out.scale = { n.scale[0], n.scale[1], n.scale[2] };
		// has_matrix のモデルは今回は無いので未対応(将来必要なら分解する)
	}

	if (data->skins_count == 0) return true;   // スキン無しモデルも許容する
	const cgltf_skin& skin = data->skins[0];

	// -- joint 添字 -> ノード添字 --
	_jointNodes.resize(skin.joints_count);
	for (cgltf_size j = 0; j < skin.joints_count; ++j)
	{
		_jointNodes[j] = static_cast<int>(skin.joints[j] - data->nodes);
	}

	// -- 逆バインド行列 --
	_inverseBindMatrices.resize(skin.joints_count);
	for (cgltf_size j = 0; j < skin.joints_count; ++j)
	{
		float m[16] = {};
		cgltf_accessor_read_float(skin.inverse_bind_matrices, j, m, 16);

		DirectX::XMFLOAT4X4 f4x4;
		memcpy(&f4x4, m, sizeof(m));
		MatrixToLH(f4x4);
		_inverseBindMatrices[j] = DirectX::XMLoadFloat4x4(&f4x4);
	}

	_boneMatrices.resize(skin.joints_count);

	// 子リストを作る
	std::vector<std::vector<int>> children(_nodes.size());
	for (size_t i = 0; i < _nodes.size(); ++i)
	{
		if (_nodes[i].parent >= 0) children[_nodes[i].parent].push_back(static_cast<int>(i));
	}

	// ルートから深さ優先で並べる
	_nodeOrder.clear();
	_nodeOrder.reserve(_nodes.size());
	std::vector<int> stack;
	for (size_t i = 0; i < _nodes.size(); ++i)
	{
		if (_nodes[i].parent < 0) stack.push_back(static_cast<int>(i));
	}
	while (!stack.empty())
	{
		int i = stack.back();
		stack.pop_back();
		_nodeOrder.push_back(i);
		for (auto c : children[i]) stack.push_back(c);
	}
	return true;
}

bool GltfActor::LoadAnimations(const cgltf_data* data)
{
	_animations.resize(data->animations_count);
	for (cgltf_size ai = 0; ai < data->animations_count; ++ai)
	{
		const cgltf_animation& src = data->animations[ai];
		Animation& dst = _animations[ai];
		dst.name = (src.name != nullptr) ? src.name : "";
		dst.channels.reserve(src.channels_count);

		for (cgltf_size ci = 0; ci < src.channels_count; ++ci)
		{
			const cgltf_animation_channel& ch = src.channels[ci];
			if (ch.target_node == nullptr || ch.sampler == nullptr) continue;

			// 対応していないパス(モーフの weights など)は無視する
			int path;
			switch (ch.target_path)
			{
			case cgltf_animation_path_type_translation: path = 0; break;
			case cgltf_animation_path_type_rotation:    path = 1; break;
			case cgltf_animation_path_type_scale:       path = 2; break;
			default: continue;
			}

			// CUBICSPLINE は未対応
			if (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline) continue;

			AnimChannel out;
			out.targetNode = static_cast<int>(ch.target_node - data->nodes);
			out.path = path;
			out.isStep = (ch.sampler->interpolation == cgltf_interpolation_type_step);

			// 時刻
			const cgltf_accessor* inAcc = ch.sampler->input;
			out.times.resize(inAcc->count);
			for (cgltf_size k = 0; k < inAcc->count; ++k)
			{
				cgltf_accessor_read_float(inAcc, k, &out.times[k], 1);
			}

			// 値（rotation は 4 成分、それ以外は 3 成分）
			const cgltf_accessor* outAcc = ch.sampler->output;
			const int comp = (path == 1) ? 4 : 3;
			out.values.resize(outAcc->count);
			for (cgltf_size k = 0; k < outAcc->count; ++k)
			{
				float v[4] = {};
				cgltf_accessor_read_float(outAcc, k, v, comp);
				// 右手系 → 左手系
				if (path == 1)
				{
					// rotation（クォータニオン）
					out.values[k] = QuatToLH(v);
				}
				else if (path == 0)
				{
					// translation
					auto t = ToLH(v);
					out.values[k] = { t.x, t.y, t.z, 0.0f };
				}
				else
				{
					// scale は変換不要
					out.values[k] = { v[0], v[1], v[2], 0.0f };
				}
			}

			if (!out.times.empty())
			{
				dst.duration = std::max(dst.duration, out.times.back());
			}

			dst.channels.push_back(std::move(out));
		}
	}
	return true;
}

void GltfActor::BuildSmoothNormals(std::vector<Vertex>& vertices)
{
	// フラットシェーディングのモデルは、同じ座標に複数の法線が割り当てられている。
	// そのまま押し出すと輪郭線のメッシュが継ぎ目で裂けるため、
	// 位置ごとに法線を平均したものを別に持たせる。
	// (シェーディング用の normal は変更しないので、見た目のフラット感は保たれる)

	using PosKey = std::tuple<float, float, float>;
	std::map<PosKey, DirectX::XMFLOAT3> sum;

	for (const auto& v : vertices)
	{
		auto& s = sum[{ v.pos.x, v.pos.y, v.pos.z }];
		s.x += v.normal.x;
		s.y += v.normal.y;
		s.z += v.normal.z;
	}

	for (auto& v : vertices)
	{
		const auto& s = sum[{ v.pos.x, v.pos.y, v.pos.z }];
		auto n = DirectX::XMLoadFloat3(&s);

		// 法線が打ち消し合ってゼロになる位置がある(真裏を向いた面の継ぎ目)
		// その場合は元の法線をそのまま使う
		if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(n)) < 1e-12f)
		{
			v.smoothNormal = v.normal;
		}
		else
		{
			DirectX::XMStoreFloat3(&v.smoothNormal, DirectX::XMVector3Normalize(n));
		}
	}
}

bool GltfActor::CreateVertexAndIndexBuffer(
    const std::vector<Vertex>& vertices,
    const std::vector<uint16_t>& indices
)
{
	auto device = _dx12.Device();

	// -- 頂点バッファの作成 --
	// 頂点ヒープの設定
	D3D12_HEAP_PROPERTIES heapprop = {};

	heapprop.Type = D3D12_HEAP_TYPE_UPLOAD;	// CPUからアクセス可能(マップ可能)
	heapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// リソースの設定
	D3D12_RESOURCE_DESC resdesc = {};

	resdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resdesc.Width = vertices.size() * sizeof(Vertex);	// 頂点情報が入るだけのサイズ
	resdesc.Height = 1;
	resdesc.DepthOrArraySize = 1;
	resdesc.MipLevels = 1;
	resdesc.Format = DXGI_FORMAT_UNKNOWN;
	resdesc.SampleDesc.Count = 1;
	resdesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	auto result = device->CreateCommittedResource(
		&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_vertBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _vertBuff")) return false;

	// インデックスバッファの作成
	// 設定は、バッファのサイズ以外、頂点バッファの設定を使いまわす
	resdesc.Width = indices.size() * sizeof(indices[0]);

	result = device->CreateCommittedResource(
		&heapprop,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_idxBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _idxBuff")) return false;

	// 頂点情報のコピー(マップ)
	Vertex* vertMap = nullptr;
	result = _vertBuff->Map(0, nullptr, (void**)&vertMap);
	if (!CheckResult(result, "_vertBuff->Map")) return false;

	std::copy(std::begin(vertices), std::end(vertices), vertMap);
	_vertBuff->Unmap(0, nullptr);	// マップの解除

	// インデックスデータをコピー(マップ)
	unsigned short* mappedIdx = nullptr;
	result = _idxBuff->Map(0, nullptr, (void**)&mappedIdx);
	if (!CheckResult(result, "_idxBuff->Map")) return false;

	std::copy(std::begin(indices), std::end(indices), mappedIdx);
	_idxBuff->Unmap(0, nullptr);

	// 頂点バッファビューの作成
	// バッファ全体を「何バイトごとの頂点の列」として解釈する
	_vbView.BufferLocation = _vertBuff->GetGPUVirtualAddress();	// バッファの仮想アドレス
	_vbView.SizeInBytes = vertices.size() * sizeof(Vertex);	// 全体のバイト数
	_vbView.StrideInBytes = sizeof(Vertex);	// 1頂点あたりのバイト数

	// インデックスバッファビューの作成
	_ibView.BufferLocation = _idxBuff->GetGPUVirtualAddress();
	_ibView.Format = DXGI_FORMAT_R16_UINT;	// 今回はunsigned short(16ビット)を使用しているため
	_ibView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(indices[0]));

	return true;
}

bool GltfActor::CreateMaterialBuffer()
{
	auto device = _dx12.Device();

	// 256 バイト境界にそろえる
	_materialBuffSize = AlignmentedSize(sizeof(MaterialForHlsl), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

	// ヒープの設定
	D3D12_HEAP_PROPERTIES materialHeapProp = {};

	materialHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD;	// CPUからアクセス可能(マップ可能)
	materialHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	materialHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// リソースの設定
	D3D12_RESOURCE_DESC materialResDesc = {};

	materialResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	materialResDesc.Width = _materialBuffSize * _materials.size();
	materialResDesc.Height = 1;                          // 必ず 1
	materialResDesc.DepthOrArraySize = 1;                // 必ず 1
	materialResDesc.MipLevels = 1;                       // 必ず 1
	materialResDesc.Format = DXGI_FORMAT_UNKNOWN;        // 必ず UNKNOWN
	materialResDesc.SampleDesc.Count = 1;                // 必ず 1
	materialResDesc.SampleDesc.Quality = 0;              // 必ず 0
	materialResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // 必ず ROW_MAJOR
	materialResDesc.Flags = D3D12_RESOURCE_FLAG_NONE;    // 用途次第

	auto result = device->CreateCommittedResource(
		&materialHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&materialResDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_materialBuff)
	);
	if (!CheckResult(result, "CreateCommittedResource _materialBuff")) return false;

	// マップして詰める
	char* mapMaterial = nullptr;
	result = _materialBuff->Map(0, nullptr, (void**)&mapMaterial);
	if (!CheckResult(result, "_materialBuff->Map")) return false;

	for (const auto& m : _materials)
	{
		*reinterpret_cast<MaterialForHlsl*>(mapMaterial) = m;
		mapMaterial += _materialBuffSize;       // 次のアライメント位置へ
	}
	_materialBuff->Unmap(0, nullptr);

	return true;
}

bool GltfActor::CreateMaterialAndTextureView()
{
	auto device = _dx12.Device();
	auto materialNum = static_cast<UINT>(_materials.size());

	// ディスクリプタヒープの作成
	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NodeMask = 0;
	// 先頭はシーン用の定数バッファ(b0)、続いてボーン行列(b1)、それ以降がマテリアル(b2)
	descHeapDesc.NumDescriptors = static_cast<UINT>(_materials.size()) * DescriptorsPerMaterial + 2;
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

	matCBVDesc.BufferLocation = _materialBuff->GetGPUVirtualAddress();	// バッファアドレス
	matCBVDesc.SizeInBytes = static_cast<UINT>(_materialBuffSize);	// マテリアルバッファサイズ

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
		matCBVDesc.BufferLocation += _materialBuffSize;

		// テクスチャ用のSRV
		auto tex = _materialTextures[i] != nullptr ? _materialTextures[i].Get() : _dx12.WhiteTexture();
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
	std::fill_n(_mappedTransform->bones, MaxBoneCount, DirectX::XMMatrixIdentity());
	std::copy(_boneMatrices.begin(), _boneMatrices.end(), _mappedTransform->bones);

	return true;
}

void GltfActor::UpdateBoneMatrices()
{
	// 全ノードのグローバル変換を求める
	std::vector<DirectX::XMMATRIX> globals(_animNodes.size());
	for (int i : _nodeOrder)
	{
		const Node& n = _animNodes[i];
		auto local =
			DirectX::XMMatrixScaling(n.scale.x, n.scale.y, n.scale.z)
			* DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&n.rotation))
			* DirectX::XMMatrixTranslation(n.translation.x, n.translation.y, n.translation.z);

		globals[i] = (n.parent < 0) ? local : local * globals[n.parent];
	}

	// joint ごとに 逆バインド行列 × グローバル変換
	for (size_t j = 0; j < _jointNodes.size(); ++j)
	{
		_boneMatrices[j] = _inverseBindMatrices[j] * globals[_jointNodes[j]];
	}

	// GPU へ
	std::fill_n(_mappedTransform->bones, MaxBoneCount, DirectX::XMMatrixIdentity());
	std::copy(_boneMatrices.begin(), _boneMatrices.end(), _mappedTransform->bones);

#ifdef _DEBUG
	static bool logged = false;
	if (!logged)
	{
		std::cout << "glTF skin: nodes=" << _animNodes.size()
			<< " joints=" << _jointNodes.size() << std::endl;

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

		std::cout << "glTF animations = " << _animations.size() << std::endl;
		for (const auto& a : _animations)
		{
			std::cout << "  '" << a.name << "' channels=" << a.channels.size()
				<< " duration=" << a.duration << std::endl;
		}
	}
#endif
}
void GltfActor::SampleAnimation(int animIndex, float timeSec, std::vector<Node>& out) const
{
	// バインドポーズからやり直す（動かさないノードを残すため）
	out = _nodes;

	if (animIndex < 0) return;
	const Animation& anim = _animations[animIndex];

	for (const auto& ch : anim.channels)
	{
		if (ch.times.empty()) continue;
		Node& node = out[ch.targetNode];

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
	_animNodes = _nodes;

	if (_currentAnimation < 0) return;
	const Animation& anim = _animations[_currentAnimation];

	for (const auto& ch : anim.channels)
	{
		if (ch.times.empty()) continue;
		Node& node = _animNodes[ch.targetNode];

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

void GltfActor::BlendNodes(const std::vector<Node>& a, const std::vector<Node>& b,
	float weight, std::vector<Node>& out) const
{
	for (size_t i = 0; i < out.size(); ++i)
	{
		const Node& na = a[i];
		const Node& nb = b[i];
		Node& no = out[i];

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
	for (size_t i = 0; i < _animations.size(); ++i)
	{
		if (_animations[i].name != name) continue;
		if (_animations[i].duration <= 0.0f) continue;

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