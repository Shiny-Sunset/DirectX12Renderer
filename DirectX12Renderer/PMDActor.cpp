#include "PMDActor.h"

#include "Dx12Wrapper.h"
#include "Util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

using Microsoft::WRL::ComPtr;

PMDActor::PMDActor(Dx12Wrapper& dx12)
	: _dx12(dx12)
{
}

PMDActor::~PMDActor()
{
	if (_mappedTransform != nullptr)
	{
		_transformBuff->Unmap(0, nullptr);
		_mappedTransform = nullptr;
	}
}


bool PMDActor::Init(const std::string& modelPath)
{
	std::vector<unsigned char> vertices;
	std::vector<unsigned short> indices;
	std::vector<PMDMaterial> pmdMaterials;
	std::vector<PMDBone> pmdBones;

	if (!LoadPMDFile(modelPath, vertices, indices, pmdMaterials, pmdBones)) return false;
	if (!CreateVertexAndIndexBuffer(vertices, indices)) return false;
	if (!CreateMaterialBuffer(pmdMaterials)) return false;
	if (!LoadTextures(modelPath, pmdMaterials)) return false;
	if (!CreateBoneNodeTable(pmdBones)) return false; 
	if (!CreateTransformBuffer()) return false; 
	if (!CreateMaterialAndTextureView()) return false;

	return true;
}

void PMDActor::Update()
{
	// 経過時間を秒(float)で測る
	auto elapsedSec = std::chrono::duration<float>(
		std::chrono::steady_clock::now() - _startTime
	).count();

	// 経過フレーム数(VMD は 30fps 基準)
	// 整数に丸めると補間の t が階段状になるので、float のまま扱う
	float frameNo = 30.0f * elapsedSec;

	if (frameNo > _duration)
	{
		// 先頭に巻き戻す
		// 超過分を次の周に持ち越す
		auto cycleSec = std::chrono::duration<float>(_duration / 30.0f);
		_startTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(cycleSec);
		frameNo -= _duration;
	}

	// 行列情報クリア
	std::fill(_boneMatrices.begin(), _boneMatrices.end(), DirectX::XMMatrixIdentity());

	// モーションデータを更新
	for (auto& bonemotion : _motionData)
	{
		auto itBoneNode = _boneNodeTable.find(bonemotion.first);
		if (itBoneNode == _boneNodeTable.end())
		{
			continue;       // モデルに無いボーンのモーションは無視する
		}
		auto& node = itBoneNode->second;

		// 合致するものを探す
		auto& keyframes = bonemotion.second;
		auto rit = std::find_if(keyframes.rbegin(), keyframes.rend(), [frameNo](const KeyFrame& keyframe) {
			return static_cast<float>(keyframe.frameNo) <= frameNo;
			});

		// 合致するものがなければ処理を飛ばす
		if (rit == keyframes.rend()) 
		{
			continue;
		}

		DirectX::XMMATRIX rotation;
		auto it = rit.base();	// it は rit より 1 大きいイテレータ(rit < 現在フレーム < it)
		if (it != keyframes.end())
		{
			auto t = static_cast<float>(frameNo - rit->frameNo) / static_cast<float>(it->frameNo - rit->frameNo);
			t = GetYFromXOnBezier(t, it->p1, it->p2, 12);

			rotation = DirectX::XMMatrixRotationQuaternion(
				DirectX::XMQuaternionSlerp(rit->quaternion, it->quaternion, t)
			);
		}
		else
		{
			rotation = DirectX::XMMatrixRotationQuaternion(rit->quaternion);
		}

		auto& pos = node.startPos;
		// 回転の中心をボーンの基準点に移すため、原点へ運んでから回して戻す
		auto mat = DirectX::XMMatrixTranslation(-pos.x, -pos.y, -pos.z)
			* rotation
			* DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
		_boneMatrices[node.boneIdx] = mat;
	}
	for (auto* root : _boneRootNodes)
	{
		RecursiveMatrixMultiply(root, DirectX::XMMatrixIdentity());
	}
	copy(_boneMatrices.begin(), _boneMatrices.end(), _mappedTransform->bones);
}

bool PMDActor::LoadPMDFile(
	const std::string& modelPath,
	std::vector<unsigned char>& vertices,
	std::vector<unsigned short>& indices,
	std::vector<PMDMaterial>& pmdMaterials,
	std::vector<PMDBone>& pmdBones
)
{
	char signature[3] = {};	// シグネチャ
	PMDHeader pmdheader = {};

	auto fp = fopen(modelPath.c_str(), "rb");
	if (fp == nullptr)
	{
		// 失敗時の処理
		std::cout << "fopen " << modelPath << " is Failed" << std::endl;
		return false;
	}
#ifdef _DEBUG
	std::cout << "fopen " << modelPath << " is OK" << std::endl;
#endif // _DEBUG

	fread(signature, sizeof(signature), 1, fp);
	fread(&pmdheader, sizeof(pmdheader), 1, fp);

	unsigned int vertNum;	// 頂点数
	fread(&vertNum, sizeof(vertNum), 1, fp);

	vertices.resize(vertNum * PmdVertexSize);	// バッファの確保
	fread(vertices.data(), vertices.size(), 1, fp);	// 読み込み

	unsigned int indicesNum;	// インデックス数
	fread(&indicesNum, sizeof(indicesNum), 1, fp);	// 読み込み

	// インデックス数を読み込むまで数が不明(PMD のインデックスは 2 バイト)
	indices.resize(indicesNum);
	fread(indices.data(), indices.size() * sizeof(indices[0]), 1, fp);

	// マテリアルの読み込み
	unsigned int materialNum;	// マテリアル数
	fread(&materialNum, sizeof(materialNum), 1, fp);

	pmdMaterials.resize(materialNum);
	fread(
		pmdMaterials.data(),
		pmdMaterials.size() * sizeof(PMDMaterial),
		1,
		fp
	);

	// ボーンの読み込み
	// PMD ではボーン数だけが 2 バイト(WORD)
	// 頂点数・インデックス数・マテリアル数の 4 バイト(DWORD)と混同しないこと
	unsigned short boneNum;	// ボーン数
	fread(&boneNum, sizeof(boneNum), 1, fp);

	pmdBones.resize(boneNum);
	fread(
		pmdBones.data(),
		pmdBones.size() * sizeof(PMDBone),
		1,
		fp
	);

#ifdef _DEBUG
	std::cout << "PMDBone count = " << boneNum << std::endl;
#endif // _DEBUG

	fclose(fp);

	return true;
}

bool PMDActor::LoadVMDFile(const std::string& motionPath)
{
	auto fp = fopen(motionPath.c_str(), "rb");
	if (fp == nullptr)
	{
		// 失敗時の処理
		std::cout << "fopen " << motionPath << " is Failed" << std::endl;
		return false;
	}
#ifdef _DEBUG
	std::cout << "fopen " << motionPath << " is OK" << std::endl;
#endif // _DEBUG

	fseek(fp, 50, SEEK_SET);	// 最初の 50 バイトは飛ばしてOK

	unsigned int motionDataNum = 0;
	fread(&motionDataNum, sizeof(motionDataNum), 1, fp);

	// VMDのモーションデータを読む
	std::vector<VMDMotion> vmdMotionData(motionDataNum);
	fread(
		vmdMotionData.data(),
		vmdMotionData.size() * sizeof(VMDMotion),
		1,
		fp
	);

	// 実際に使用するモーションテーブルへ変換
	for (auto& vmdMotion : vmdMotionData)
	{
		std::string boneName(vmdMotion.boneName, strnlen(vmdMotion.boneName, sizeof(vmdMotion.boneName)));
		_motionData[boneName].emplace_back(
			vmdMotion.frameNo,
			DirectX::XMLoadFloat4(&vmdMotion.quaternion),
			DirectX::XMFLOAT2((float)vmdMotion.bezier[3] / 127.0f, (float)vmdMotion.bezier[7] / 127.0f),
			DirectX::XMFLOAT2((float)vmdMotion.bezier[11] / 127.0f, (float)vmdMotion.bezier[15] / 127.0f)
		);
		_duration = std::max(_duration, vmdMotion.frameNo);
	}

	// フレーム番号順に並べておく(ファイル上は順不同)
	for (auto& [name, keyframes] : _motionData)
	{
		std::sort(
			keyframes.begin(), keyframes.end(),
			[](const KeyFrame& a, const KeyFrame& b) { return a.frameNo < b.frameNo; }
		);
	}

	fclose(fp);
	// moveBone();
	return true;

}

bool PMDActor::CreateVertexAndIndexBuffer(
	const std::vector<unsigned char>& vertices,
	const std::vector<unsigned short>& indices
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
	resdesc.Width = vertices.size();	// 頂点情報が入るだけのサイズ
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
	unsigned char* vertMap = nullptr;
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
	_vbView.SizeInBytes = static_cast<UINT>(vertices.size());	// 全体のバイト数
	_vbView.StrideInBytes = PmdVertexSize;	// 1頂点あたりのバイト数

	// インデックスバッファビューの作成
	_ibView.BufferLocation = _idxBuff->GetGPUVirtualAddress();
	_ibView.Format = DXGI_FORMAT_R16_UINT;	// 今回はunsigned short(16ビット)を使用しているため
	_ibView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(indices[0]));

	return true;
}

bool PMDActor::CreateMaterialBuffer(const std::vector<PMDMaterial>& pmdMaterials)
{
	auto device = _dx12.Device();

	_materials.resize(pmdMaterials.size());

	// コピー
	for (size_t i = 0; i < pmdMaterials.size(); ++i)
	{
		_materials[i].indicesNum = pmdMaterials[i].indicesNum;
		_materials[i].material.diffuse = pmdMaterials[i].diffuse;
		_materials[i].material.alpha = pmdMaterials[i].alpha;
		_materials[i].material.specular = pmdMaterials[i].specular;
		_materials[i].material.specularity = pmdMaterials[i].specularity;
		_materials[i].material.ambient = pmdMaterials[i].ambient;

		_materials[i].additional.texPath = pmdMaterials[i].texFilePath;
		_materials[i].additional.toonIdx = pmdMaterials[i].toonIdx;
		_materials[i].additional.edgeFlg = pmdMaterials[i].edgeFlg != 0;
	}

	// -- マテリアルバッファの作成 --
	// マテリアルバッファのサイズを計算
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

	// マップマテリアルにコピー
	char* mapMaterial = nullptr;

	result = _materialBuff->Map(0, nullptr, (void**)&mapMaterial);
	if (!CheckResult(result, "_materialBuff->Map")) return false;

	for (auto& m : _materials)
	{
		*((MaterialForHlsl*)mapMaterial) = m.material;	// データコピー
		mapMaterial += _materialBuffSize;	// 次のアライメント位置まで進める
	}
	_materialBuff->Unmap(0, nullptr);

	return true;
}

bool PMDActor::CreateTransformBuffer()
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

	// PMD は位置を動かさないので、ワールド行列は単位行列のままにしておく
	_mappedTransform->world = DirectX::XMMatrixIdentity();

	// 使っていない分を触っても壊れないよう、全体を単位行列で埋めてから流し込む
	std::fill_n(_mappedTransform->bones, MaxBoneCount, DirectX::XMMatrixIdentity());
	std::copy(_boneMatrices.begin(), _boneMatrices.end(), _mappedTransform->bones);

	return true;
}

bool PMDActor::LoadTextures(const std::string& modelPath, const std::vector<PMDMaterial>& pmdMaterials)
{
	auto materialNum = pmdMaterials.size();

	_textureResources.resize(materialNum);
	_sphResources.resize(materialNum);
	_spaResources.resize(materialNum);
	_toonResources.resize(materialNum);

	for (size_t i = 0; i < materialNum; ++i)
	{
		// -- トゥーンリソースの読み込み --
		// トゥーン番号は通常テクスチャの有無とは無関係に指定されるため、
		// テクスチャ無しマテリアルの early-continue よりも先に読み込む
		if (pmdMaterials[i].toonIdx == 0xff)
		{
			// トゥーン未指定。デフォルトのグラデーションテクスチャに任せる
			_toonResources[i] = nullptr;
		}
		else
		{
			std::string toonFilePath = "toon/";

			char toonFileName[16];

			sprintf(
				toonFileName,
				"toon%02d.bmp",
				pmdMaterials[i].toonIdx + 1
			);

			toonFilePath += toonFileName;

			_toonResources[i] = _dx12.GetTextureByPath(toonFilePath);
		}

		if (strlen(pmdMaterials[i].texFilePath) == 0)
		{
			_textureResources[i] = nullptr;
			continue;
		}

		// テクスチャの指定は「通常テクスチャ*スフィアマップ」のようにスプリッターで
		// 2 つ並ぶことがあり、どちらが先に来るかは決まっていない
		// さらにスフィアマップだけが単体で指定されることもある
		// そのため候補をいったん集めてから、拡張子を見て行き先を振り分ける
		std::vector<std::string> texFileNames;
		std::string rawFileName = pmdMaterials[i].texFilePath;

		if (std::count(rawFileName.begin(), rawFileName.end(), '*') > 0)
		{	// スプリッターがある
			auto namepair = SplitFileName(rawFileName);
			texFileNames.push_back(namepair.first);
			texFileNames.push_back(namepair.second);
		}
		else
		{
			texFileNames.push_back(rawFileName);
		}

		for (auto& fileName : texFileNames)
		{
			if (fileName.empty())
			{
				continue;
			}

			// モデルとテクスチャパスからアプリケーションからのテクスチャパスを得る
			auto filePath = GetTexturePathFromModelAndTexPath(
				modelPath,
				fileName.c_str()
			);

			auto ext = GetExtension(fileName);
			if (ext == "sph")
			{
				// 乗算スフィアマップ
				_sphResources[i] = _dx12.GetTextureByPath(filePath);
			}
			else if (ext == "spa")
			{
				// 加算スフィアマップ
				_spaResources[i] = _dx12.GetTextureByPath(filePath);
			}
			else
			{
				// 通常テクスチャ
				_textureResources[i] = _dx12.GetTextureByPath(filePath);
			}
		}
	}

	return true;
}

bool PMDActor::CreateMaterialAndTextureView()
{
	auto device = _dx12.Device();
	auto materialNum = static_cast<UINT>(_materials.size());

	// ディスクリプタヒープの作成
	D3D12_DESCRIPTOR_HEAP_DESC materialDescHeapDesc = {};
	materialDescHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	materialDescHeapDesc.NodeMask = 0;
	// 先頭の 2 つはシーン用の定数バッファ(b0)、ボーン行列(b1)、残りがマテリアル(b1)
	// CBV_SRV_UAV のディスクリプタヒープは同時に 1 本しかバインドできないため、
	// b0 ～ b2 のディスクリプタは同じヒープにまとめる必要がある
	materialDescHeapDesc.NumDescriptors = materialNum * DescriptorsPerMaterial + 2;
	materialDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	auto result = device->CreateDescriptorHeap(&materialDescHeapDesc, IID_PPV_ARGS(&_materialDescHeap));
	if (!CheckResult(result, "CreateDescriptorHeap _materialDescHeap")) return false;

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
	auto matDescHeapH = _materialDescHeap->GetCPUDescriptorHandleForHeapStart();

	auto incSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 先頭(0 番)にシーン用の定数バッファビュー(b0)を作る
	_dx12.CreateSceneConstantBufferView(matDescHeapH);
	matDescHeapH.ptr += incSize;

	// ボーン行列用の定数バッファビュー(b1) ← 自分のバッファなので直接作る
	D3D12_CONSTANT_BUFFER_VIEW_DESC transformCBVDesc = {};
	transformCBVDesc.BufferLocation = _transformBuff->GetGPUVirtualAddress();
	transformCBVDesc.SizeInBytes = static_cast<UINT>(_transformBuff->GetDesc().Width);
	device->CreateConstantBufferView(&transformCBVDesc, matDescHeapH);
	matDescHeapH.ptr += incSize;

	// テクスチャが指定されなかったときに使う既定のテクスチャ
	auto whiteTex = _dx12.WhiteTexture();
	auto blackTex = _dx12.BlackTexture();
	auto gradTex = _dx12.GradTexture();

	// 2 番目以降にマテリアルの定数バッファビュー(b2)をマテリアル数ぶん並べる
	for (UINT i = 0; i < materialNum; ++i)
	{
		// マテリアル用CBV
		device->CreateConstantBufferView(&matCBVDesc, matDescHeapH);
		matDescHeapH.ptr += incSize;
		matCBVDesc.BufferLocation += _materialBuffSize;

		// テクスチャ用のSRV
		auto tex = _textureResources[i] != nullptr ? _textureResources[i].Get() : whiteTex;
		srvDesc.Format = tex->GetDesc().Format;
		device->CreateShaderResourceView(tex, &srvDesc, matDescHeapH);
		matDescHeapH.ptr += incSize;

		// 乗算スフィアマップ用のSRV
		auto sph = _sphResources[i] != nullptr ? _sphResources[i].Get() : whiteTex;
		srvDesc.Format = sph->GetDesc().Format;
		device->CreateShaderResourceView(sph, &srvDesc, matDescHeapH);
		matDescHeapH.ptr += incSize;

		// 加算スフィアマップ用のSRV
		auto spa = _spaResources[i] != nullptr ? _spaResources[i].Get() : blackTex;
		srvDesc.Format = spa->GetDesc().Format;
		device->CreateShaderResourceView(spa, &srvDesc, matDescHeapH);
		matDescHeapH.ptr += incSize;

		// トゥーンテクスチャ用のSRV
		auto toon = _toonResources[i] != nullptr ? _toonResources[i].Get() : gradTex;
		srvDesc.Format = toon->GetDesc().Format;
		device->CreateShaderResourceView(toon, &srvDesc, matDescHeapH);
		matDescHeapH.ptr += incSize;
	}

	return true;
}

bool PMDActor::CreateBoneNodeTable(const std::vector<PMDBone>& pmdBones)
{
	// インデックスと名前の対応関係構築のためにあとで使う
	std::vector<std::string> boneNames(pmdBones.size());

	// ボーンノードマップの作成
	for (int idx = 0; idx < pmdBones.size(); ++idx)
	{
		auto& pb = pmdBones[idx];
		boneNames[idx] = pb.boneName;
		auto& node = _boneNodeTable[pb.boneName];
		node.boneIdx = idx;
		node.startPos = pb.pos;
	}

	// 親子関係の構築
	for (size_t idx = 0; idx < pmdBones.size(); ++idx)
	{
		auto& pb = pmdBones[idx];
		// 親インデックスをチェック(あり得ない番号なら飛ばす)
		if (pb.parentNo >= pmdBones.size())
		{
			_boneRootNodes.emplace_back(&_boneNodeTable[boneNames[idx]]);
			continue;
		}

		auto& parentName = boneNames[pb.parentNo];
		auto& childName = boneNames[idx];
		_boneNodeTable[parentName].children.emplace_back(&_boneNodeTable[childName]);
	}

	_boneMatrices.resize(pmdBones.size());

	// 全てのボーン行列を初期化
	std::fill(_boneMatrices.begin(), _boneMatrices.end(), DirectX::XMMatrixIdentity());

	return true;
}

void PMDActor::RecursiveMatrixMultiply(BoneNode* node, const DirectX::XMMATRIX& mat)
{
	_boneMatrices[node->boneIdx] *= mat;

	for (auto& child : node->children)
	{
		RecursiveMatrixMultiply(child, _boneMatrices[node->boneIdx]);
	}
}

void PMDActor::Draw()
{
	auto cmdList = _dx12.CommandList();

	// ディスクリプタヒープの指定
	// CBV_SRV_UAV のヒープは同時に 1 本しかバインドできない
	// (切り替えると、それ以前に設定したディスクリプタテーブルは無効になる)
	// ComPtr の operator& は中身を Release してしまうため、必ず Get() を使うこと
	ID3D12DescriptorHeap* heaps[] = { _materialDescHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	// ヒープの先頭ハンドル(0 番 = シーン用の定数バッファ)
	auto matHeapH = _materialDescHeap->GetGPUDescriptorHandleForHeapStart();

	// ルートパラメーター 0 番に行列(b0)を関連付ける
	cmdList->SetGraphicsRootDescriptorTable(0, matHeapH);

	// 頂点バッファの設定
	cmdList->IASetVertexBuffers(0, 1, &_vbView);

	// インデックスバッファの設定
	cmdList->IASetIndexBuffer(&_ibView);

	// 描画命令
	// マテリアルごとにディスクリプタを付け替えながら、担当インデックス範囲だけを描く
	auto incSize = _dx12.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// ここから先はマテリアル用のディスクリプタ(2 番目以降)
	matHeapH.ptr += incSize * 2;

	// マテリアル 1 つぶんのサイズ（マテリアル、テクスチャ、.sph、.spa、トゥーン）
	auto materialStride = incSize * DescriptorsPerMaterial;

	unsigned int idxOffset = 0;	// 描画開始インデックス
	for (auto& m : _materials)
	{
		// ルートパラメーター 1 番にこのマテリアル(b1)を関連付ける
		cmdList->SetGraphicsRootDescriptorTable(1, matHeapH);

		cmdList->DrawIndexedInstanced(m.indicesNum, 1, idxOffset, 0, 0);

		matHeapH.ptr += materialStride;	// 次のマテリアルのディスクリプタへ
		idxOffset += m.indicesNum;		// 次のマテリアルのインデックス開始位置へ
	}
}

void PMDActor::moveBone()
{
	for (auto& bonemotion : _motionData)
	{
		auto itBoneNode = _boneNodeTable.find(bonemotion.first);
		if (itBoneNode == _boneNodeTable.end())
		{
			continue;       // モデルに無いボーンのモーションは無視する
		}
		auto& node = itBoneNode->second;
		auto& pos = node.startPos;
		// 回転の中心をボーンの基準点に移すため、原点へ運んでから回して戻す
		auto mat = DirectX::XMMatrixTranslation(-pos.x, -pos.y, -pos.z)
			* DirectX::XMMatrixRotationQuaternion(bonemotion.second[0].quaternion)
			* DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
		_boneMatrices[node.boneIdx] = mat;
	}
	for (auto* root : _boneRootNodes)
	{
		RecursiveMatrixMultiply(root, DirectX::XMMatrixIdentity());
	}
	copy(_boneMatrices.begin(), _boneMatrices.end(), _mappedTransform->bones);
}

void PMDActor::PlayAnimation()
{
	_startTime = std::chrono::steady_clock::now();
}

float PMDActor::GetYFromXOnBezier(float x, const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b, uint8_t n)
{
	if (a.x == a.y && b.x == b.y)
	{
		return x;	// 計算不要
	}

	float t = x;
	const float k0 = 1 + 3 * a.x - 3 * b.x;	// t^3 の係数
	const float k1 = 3 * b.x - 6 * a.x;	// t^2 の係数
	const float k2 = 3 * a.x;	// t の係数

	// 誤差の範囲内かどうかに使用する定数
	constexpr float epsilon = 0.0005f;

	// t を近似で求める
	for (int i = 0; i < n; ++i)
	{
		// f(t) を求める
		auto ft = k0 * t * t * t + k1 * t * t + k2 * t - x;
		auto dft = 3 * k0 * t * t + 2 * k1 * t + k2;      // f'(t)

		// もし結果が 0 に近い(誤差の範囲内)なら打ち切る
		if (ft <= epsilon && ft >= -epsilon)
		{
			break;
		}

		t -= ft / dft;	// 刻む
	}

	// 求めたい t はすでに求めているので y を計算する
	auto r = 1 - t;
	return t * t * t + 3 * t * t * r * b.y + 3 * t * r * r * a.y;
}
