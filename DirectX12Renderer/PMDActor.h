#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <chrono>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>

class Dx12Wrapper;

// PMD モデル 1 体分のデータと描画を受け持つクラス
// 頂点、インデックス、マテリアル、テクスチャ、それらのディスクリプタヒープを持つ
class PMDActor
{
public:
	explicit PMDActor(Dx12Wrapper& dx12);
	~PMDActor();

	PMDActor(const PMDActor&) = delete;
	PMDActor& operator=(const PMDActor&) = delete;

	// PMD ファイルを読み込んで描画に必要なリソースを作る
	// @param modelPath アプリケーションから見た pmd モデルのパス
	// @return 成功したら true
	bool Init(const std::string& modelPath);

	// 経過時間からアニメーションを更新する
	void Update();

	// 描画
	// PMDRenderer::BeforeDraw() でパイプラインを設定してから呼ぶこと
	void Draw();

	// VMD ファイルからモーションを読み込む
	// @param motionPath アプリケーションから見た vmd ファイルのパス
	// @return 成功したら true
	bool LoadVMDFile(const std::string& motionPath);

	void PlayAnimation();

private:
	// ヘッダーのグローバルスコープに using 宣言を置かないためのクラススコープの別名
	template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	// 頂点 1 つあたりのサイズ
	static constexpr size_t PmdVertexSize = 38;

	// マテリアル 1 つが使うディスクリプタの数
	// CBV(マテリアル) + SRV(テクスチャ) + SRV(乗算スフィアマップ)
	// + SRV(加算スフィアマップ) + SRV(トゥーンテクスチャ)
	static constexpr UINT DescriptorsPerMaterial = 5;

	// PMD ヘッダー構造体
	struct PMDHeader
	{
		float version;
		char model_name[20];	// モデル名
		char comment[256];	// モデルコメント
	};

#pragma pack(1)	// ここから 1 バイトパッキングとなり、アライメントは発生しない
	// PMD マテリアル構造体(ファイル上の並びそのまま)
	struct PMDMaterial
	{
		DirectX::XMFLOAT3 diffuse;	// ディフューズ色
		float alpha;	// ディフューズα
		float specularity;	// スぺキュラの強さ(乗算値)
		DirectX::XMFLOAT3 specular;	// スペキュラ色
		DirectX::XMFLOAT3 ambient;	// アンビエント色
		unsigned char toonIdx;	// トゥーン番号
		unsigned char edgeFlg;	// マテリアルごとの輪郭線フラグ

		// 2 バイトのパディングが発生しない

		unsigned int indicesNum;	// マテリアルが割り当てられるインデックス数

		char texFilePath[20];	// テクスチャファイルパス + α
	};	// 合計 70バイト

	// ボーンの構造体
	struct PMDBone
	{
		char boneName[20];	// ボーン名
		unsigned short parentNo;	// 親ボーン番号
		unsigned short nextNo;	// 先端のボーン番号
		unsigned char type;	// ボーン種別
		unsigned short ikBoneNo;	// IK ボーン番号
		DirectX::XMFLOAT3 pos;	// ボーンの基準点座標
	};	// 合計 39バイト

	// VMD のモーションデータ(ファイル上の並びそのまま)
	struct VMDMotion
	{
		char boneName[15];      // ボーン名
		unsigned int frameNo;   // フレーム番号
		DirectX::XMFLOAT3 location;     // 位置
		DirectX::XMFLOAT4 quaternion;   // 回転クォータニオン
		unsigned char bezier[64];       // 補間ベジェパラメーター
	};      // 合計 111バイト
#pragma pack()	// パッキング指定を解除

	// これらの構造体はファイル上の並びをそのまま fread するため、
	// サイズが 1 バイトでもずれると以降の読み込みが全部壊れる
	// (#pragma pack(1) の範囲から外れた場合などをコンパイル時に検出する)
	static_assert(sizeof(PMDMaterial) == 70, "PMDMaterial のサイズが 70 バイトではありません");
	static_assert(sizeof(PMDBone) == 39, "PMDBone のサイズが 39 バイトではありません");
	static_assert(sizeof(VMDMotion) == 111, "VMDMotion のサイズが 111 バイトではありません");

	// シェーダー側に投げられるマテリアルデータ
	// (BasicShaderHeader.hlsli の cbuffer Material と並びを合わせること)
	struct MaterialForHlsl
	{
		DirectX::XMFLOAT3 diffuse;	// ディフューズ色
		float alpha;	// ディフューズα
		DirectX::XMFLOAT3 specular;	// スペキュラ色
		float specularity;	// スぺキュラの強さ(乗算値)
		DirectX::XMFLOAT3 ambient;	// アンビエント色
	};

	// それ以外のマテリアルデータ
	struct AdditionalMaterial
	{
		std::string texPath;	// テクスチャファイルパス
		int toonIdx;	// トゥーン番号
		bool edgeFlg;	// マテリアルごとの輪郭線フラグ
	};

	// 全体をまとめるデータ
	struct Material
	{
		unsigned int indicesNum;	// インデックス数
		MaterialForHlsl material;
		AdditionalMaterial additional;
	};

	// シェーダー側の bones[] の要素数(BasicShaderHeader.hlsli と合わせること)
	static constexpr size_t MaxBoneCount = 256;

	// ボーンのデータ
	struct BoneNode
	{
		int boneIdx;	// ボーンインデックス
		DirectX::XMFLOAT3 startPos;	// ボーン基準点(回転の中心)
		DirectX::XMFLOAT3 endPos = {};	// ボーン先端点(実際のスキニングでは利用しない)
		std::vector<BoneNode*> children;	// 子ノード
	};

	// ボーンの座標変換用行列
	std::vector<DirectX::XMMATRIX> _boneMatrices;

	// ボーンノードテーブル
	std::map<std::string, BoneNode> _boneNodeTable;

	// ボーン行列用の定数バッファ(b1)を作る
	bool CreateTransformBuffer();

	// -- ボーン --
	ComPtr<ID3D12Resource> _transformBuff;
	DirectX::XMMATRIX* _mappedTransform = nullptr;   // 毎フレーム書くのでマップしたままにする

	// 親の変換を子へ伝播させる
	// @param node 起点のノード
	// @param mat 親までで積み上がった行列
	void RecursiveMatrixMultiply(BoneNode* node, const DirectX::XMMATRIX& mat);

	// -- 初期化のサブルーチン --
	// PMD ファイルを読み込む
	bool LoadPMDFile(
		const std::string& modelPath,
		std::vector<unsigned char>& vertices,
		std::vector<unsigned short>& indices,
		std::vector<PMDMaterial>& pmdMaterials,
		std::vector<PMDBone>& pmdBones
	);

	// 頂点バッファとインデックスバッファを作る
	bool CreateVertexAndIndexBuffer(
		const std::vector<unsigned char>& vertices,
		const std::vector<unsigned short>& indices
	);

	// マテリアルの定数バッファを作る
	bool CreateMaterialBuffer(const std::vector<PMDMaterial>& pmdMaterials);

	// テクスチャ、スフィアマップ、トゥーンテクスチャを読み込む
	bool LoadTextures(const std::string& modelPath, const std::vector<PMDMaterial>& pmdMaterials);

	// マテリアル用のディスクリプタヒープとビューを作る
	bool CreateMaterialAndTextureView();

	// ボーンの情報からボーンノードテーブルを用意する
	bool CreateBoneNodeTable(const std::vector<PMDBone>& pmdBones);

	// キーフレーム 1 つ分
	struct KeyFrame
	{
		unsigned int frameNo;   // アニメーション開始からのフレーム番号
		DirectX::XMVECTOR quaternion;   // 回転クォータニオン
		DirectX::XMFLOAT2 p1, p2;	// ベジェ曲線の中間コントロールポイント

		KeyFrame(unsigned int fno, const DirectX::XMVECTOR& q, const DirectX::XMFLOAT2& ip1, const DirectX::XMFLOAT2& ip2)
			: frameNo(fno), quaternion(q), p1(ip1), p2(ip2) {
		}
	};

	// ボーン名 -> そのボーンのキーフレーム列
	std::unordered_map<std::string, std::vector<KeyFrame>> _motionData;

	unsigned int _duration = 0;     // モーションの総フレーム数
	std::vector<BoneNode*> _boneRootNodes;   // 親を持たないボーン

	// アニメーション開始時刻
	// timeGetTime() は分解能が既定で約 15.6ms しかなく、60fps だと経過時間が
	// 不規則に飛んでカクつきの原因になるため、単調増加が保証された steady_clock を使う
	std::chrono::steady_clock::time_point _startTime;

	// ベジェ曲線補間メソッドの実装
	float GetYFromXOnBezier(float x, const DirectX::XMFLOAT2& a, const DirectX::XMFLOAT2& b, uint8_t n);

	Dx12Wrapper& _dx12;

	// -- 頂点とインデックス --
	ComPtr<ID3D12Resource> _vertBuff;
	ComPtr<ID3D12Resource> _idxBuff;
	D3D12_VERTEX_BUFFER_VIEW _vbView = {};
	D3D12_INDEX_BUFFER_VIEW _ibView = {};

	// -- マテリアル --
	std::vector<Material> _materials;
	ComPtr<ID3D12Resource> _materialBuff;
	size_t _materialBuffSize = 0;	// 256 バイト境界にそろえた 1 マテリアル分のサイズ

	// シーン用の定数バッファ(b0)とマテリアル用のディスクリプタをまとめて持つヒープ
	// CBV_SRV_UAV のヒープは同時に 1 本しかバインドできないので、
	// b0 のビューもこのヒープの先頭(0 番)に同居させている
	ComPtr<ID3D12DescriptorHeap> _materialDescHeap;

	// -- テクスチャ(実体は Dx12Wrapper のキャッシュと共有) --
	std::vector<ComPtr<ID3D12Resource>> _textureResources;
	std::vector<ComPtr<ID3D12Resource>> _sphResources;
	std::vector<ComPtr<ID3D12Resource>> _spaResources;
	std::vector<ComPtr<ID3D12Resource>> _toonResources;

	void moveBone();
};
