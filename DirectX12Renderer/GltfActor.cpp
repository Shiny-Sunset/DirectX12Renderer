#include "GltfActor.h"

#include "Dx12Wrapper.h"
#include "GltfRenderer.h"
#include "Util.h"

#define CGLTF_IMPLEMENTATION
#include "External/cgltf.h"

GltfActor::GltfActor(Dx12Wrapper& dx12, GltfRenderer& renderer)
	: _dx12(dx12), _renderer(renderer)
{
}

bool GltfActor::Init(const std::string& modelPath)
{
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

	if (!LoadGltfFile(modelPath, vertices, indices)) return false;
	if (!CreateVertexAndIndexBuffer(vertices, indices)) return false;
	if (!CreateMaterialBuffer()) return false;
	if (!CreateMaterialAndTextureView()) return false;

	return true;
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
	// 1 周目: 不透明
	cmdList->SetPipelineState(_renderer.PipelineState());
	for (const auto& p : _primitives)
	{
		if (p.isBlend) continue;        // ← この行が抜けている

		auto matIdx = (p.materialIndex >= 0) ? p.materialIndex : 0;
		auto handle = heapH;
		handle.ptr += incSize * (1 + matIdx * DescriptorsPerMaterial);
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
		handle.ptr += incSize * (1 + matIdx * DescriptorsPerMaterial);
		cmdList->SetGraphicsRootDescriptorTable(1, handle);

		cmdList->DrawIndexedInstanced(
			p.indexCount, 1, p.startIndexLocation, p.baseVertexLocation, 0
		);
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

            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
            {
                const cgltf_attribute& attr = prim.attributes[ai];
                if (attr.index != 0) continue;   // TEXCOORD_1 などは無視する

                switch (attr.type)
                {
                case cgltf_attribute_type_position: posAcc = attr.data; break;
                case cgltf_attribute_type_normal:   nrmAcc = attr.data; break;
                case cgltf_attribute_type_texcoord: uvAcc = attr.data; break;
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
				v.pos = { f[0], f[1], -f[2] };   // 右手系 → 左手系

				if (nrmAcc != nullptr)
				{
					cgltf_accessor_read_float(nrmAcc, k, f, 3);
					v.normal = { f[0], f[1], -f[2] };
				}
				if (uvAcc != nullptr)
				{
					cgltf_accessor_read_float(uvAcc, k, f, 2);
					v.uv = { f[0], f[1] };
				}

				vertices.push_back(v);
			}

			// インデックス
			for (cgltf_size k = 0; k < prim.indices->count; ++k)
			{
				indices.push_back(static_cast<uint16_t>(cgltf_accessor_read_index(prim.indices, k)));
			}

			_primitives.push_back(p);
        }
    }

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
	// 先頭はシーン用の定数バッファ(b0)、残りがマテリアル(b1)
	descHeapDesc.NumDescriptors = static_cast<UINT>(_materials.size()) * DescriptorsPerMaterial + 1;
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

	// テクスチャが指定されなかったときに使う既定のテクスチャ
	auto whiteTex = _dx12.WhiteTexture();
	auto blackTex = _dx12.BlackTexture();
	auto gradTex = _dx12.GradTexture();

	// 2 番目以降にマテリアルの定数バッファビュー(b1)をマテリアル数ぶん並べる
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