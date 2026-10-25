#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cassert>
#include "GameConfig.h"

// DirectX12 の基盤部分をまとめたクラス
// デバイス、スワップチェーン、コマンド関連、レンダーターゲット、深度バッファ、
// シーン共通の定数バッファ(b0)、テクスチャの生成とキャッシュを受け持つ
//
// 1 フレームの描画は 3 つのパスに分かれる
//   0 パス目 BeginShadowPass     → 光源から見た深度をシャドウマップへ
//   1 パス目 BeginOffscreenPass  → シーンをオフスクリーンのテクスチャへ
//   2 パス目 BeginBackBufferPass → そのテクスチャを画面へ転送し、UI を重ねる
class Dx12Wrapper
{
public:
	Dx12Wrapper() = default;
	~Dx12Wrapper();

	// COM オブジェクトを抱えるのでコピーは禁止する
	Dx12Wrapper(const Dx12Wrapper&) = delete;
	Dx12Wrapper& operator=(const Dx12Wrapper&) = delete;

	enum class GpuPass { Shadow, Scene, Pera, Total, Count };

	// 直前に完了したフレームの、パスごとの GPU 時間（ミリ秒）
	float GpuPassTime(GpuPass p) const { return _gpuPassMs[static_cast<size_t>(p)]; }

	// 初期化
	// @param hwnd 描画先のウィンドウハンドル
	// @param windowWidth ウィンドウ幅
	// @param windowHeight ウィンドウ高さ
	// @return 成功したら true
	bool Init(HWND hwnd, int windowWidth, int windowHeight);

	static constexpr UINT BufferCount = 3;

	int WindowWidth() const { return _windowWidth; }
	int WindowHeight() const { return _windowHeight; }

	// ------------------------------------------------------------
	// 描画パスの制御
	// ------------------------------------------------------------
	// 各 Begin〜 はリソースバリアの遷移、描画先の設定、クリア、
	// ビューポートとシザー矩形の設定までを行う

	void BeginCascade(UINT c);
	UINT CurrentCascade() const { return _currentCascade; }
	void BeginShadowPass();      // 0 パス目：シャドウマップへ描き始める
	void EndShadowPass();        // 深度テクスチャとして読める状態へ戻す

	void BeginOffscreenPass();   // 1 パス目：オフスクリーンのテクスチャへ描き始める
	void EndOffscreenPass();     // テクスチャとして読める状態へ戻す

	void BeginBackBufferPass();  // 2 パス目：バックバッファへ描き始める

	// フレームの開始
	void BeginFrame();

	// 描画終了
	// リソースバリア、コマンドリストのクローズと実行、GPU の完了待ち、リセットまで行う
	void EndDraw();

	// 画面のスワップ(フリップ)
	void Flip();

	// GPU の処理完了を待つ
	void WaitForGpuIdle();

	// ------------------------------------------------------------
	// カメラと光源（シーン用定数バッファ b0 の中身）
	// ------------------------------------------------------------

	// カメラを設定し、ビュー行列とプロジェクション行列を作り直す
	// @param eye 視点
	// @param target 注視点
	// @param nearZ 近クリップ面
	// @param farZ 遠クリップ面
	void SetCamera(const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT3& target, float nearZ, float farZ);

	// 光源から見たビュー×プロジェクション行列を作り直す
	// @param center 影を落とす範囲の中心（通常はプレイヤーの位置）
	void UpdateLightCamera(const DirectX::XMFLOAT3& center);

	void SetLightVec(const DirectX::XMFLOAT3& v) { _lightVec = v; }
	const DirectX::XMFLOAT3& LightVec() const { return _lightVec; }

	// 影パスの絞り込みに使う。
	// UPLOAD ヒープは読み出しが極端に遅いので、CPU 側で使う分は控えを持つ
	const DirectX::XMMATRIX& LightCamera(UINT cascade) const { return _lightCamera[cascade]; }

	void SetLightDistance(float d) { _lightDistance = d; }
	float LightDistance() const { return _lightDistance; }

	// 影を落とす範囲（m）。狭めるほど影が精細になる
	void SetShadowAreaScale(float areaScale) { _shadowAreaScale = areaScale; }
	float ShadowAreaScale() const { return _shadowAreaScale; }

	// ------------------------------------------------------------
	// シーン用定数バッファ(b0)の受け渡し
	// ------------------------------------------------------------

	// 指定されたハンドルの位置にビューを作る
	// CBV_SRV_UAV のディスクリプタヒープは同時に 1 本しかバインドできないため、
	// シーン用のビューも各アクターが持つヒープの先頭に同居させる
	// @param handle ビューを作成する位置の CPU ディスクリプタハンドル
	void CreateSceneConstantBufferView(D3D12_CPU_DESCRIPTOR_HANDLE handle);

	// GPU 側のアドレス。ルートディスクリプタとして直接渡すときに使う
	D3D12_GPU_VIRTUAL_ADDRESS SceneConstantBufferAddress() const
	{
		return _sceneConstBuff->GetGPUVirtualAddress() + _frameIndex * _sceneCBStride;
	}

	static constexpr UINT FrameCount = 2;
	UINT FrameIndex() const { return _frameIndex; }

	// ------------------------------------------------------------
	// テクスチャ
	// ------------------------------------------------------------

	// テクスチャをファイルから読み込む
	// 同じパスに対する 2 回目以降の呼び出しはキャッシュを返す
	// @param texPath アプリケーションから見たテクスチャファイルパス
	// @return 生成したテクスチャバッファ。失敗した場合は nullptr
	ID3D12Resource* GetTextureByPath(const std::string& texPath);

	// メモリ上の画像データ(PNG/JPEG のバイト列)からテクスチャを作る
	// GLB は画像をファイルではなくバイナリチャンク内に埋め込むため、こちらを使う
	// @param data 画像ファイルの先頭を指すポインタ
	// @param size バイト数
	// @return 生成したテクスチャバッファ。失敗した場合は nullptr
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureFromMemory(const uint8_t* data, size_t size);

	// テクスチャが指定されなかったときに使う既定のテクスチャ
	ID3D12Resource* WhiteTexture() const { return _whiteTex.Get(); }
	ID3D12Resource* BlackTexture() const { return _blackTex.Get(); }
	ID3D12Resource* GradTexture() const { return _gradTex.Get(); }

	// シャドウマップの SRV を、指定されたハンドルの位置に作る
	// 各アクターが自分のヒープの中に作るために使う
	void CreateShadowMapView(D3D12_CPU_DESCRIPTOR_HANDLE handle);

	// ------------------------------------------------------------
	// 設定
	// ------------------------------------------------------------

	// 垂直同期(VSync)の切り替え
	// 切ると画面の更新を待たなくなり、フレームレートの上限が外れる
	// (デルタタイムが効いているかの確認用。通常は有効のままにする)
	void SetVSyncEnabled(bool enabled) { _vsyncEnabled = enabled; }
	bool IsVSyncEnabled() const { return _vsyncEnabled; }

	void SetTime(float time);

	// ------------------------------------------------------------
	// 各種オブジェクトの取得
	// ------------------------------------------------------------
	ID3D12Device* Device() const { return _dev.Get(); }
	ID3D12GraphicsCommandList* CommandList() const { return _cmdList.Get(); }
	ID3D12CommandQueue* CommandQueue() const { return _cmdQueue.Get(); }

	// ペラポリゴン用テクスチャとシャドウマップの SRV ヒープ
	// 描画する側が SetDescriptorHeaps で指定する
	ID3D12DescriptorHeap* PeraSrvHeap() const { return _peraSRVHeap.Get(); }
	ID3D12DescriptorHeap* ShadowSrvHeap() const { return _shadowSRVHeap.Get(); }

private:
	// ヘッダーのグローバルスコープに using 宣言を置くと、
	// インクルードした全ての翻訳単位に名前が漏れるため、クラススコープの別名にとどめる
	template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	// シェーダーに渡すシーン共通の行列
	// (GltfShaderHeader.hlsli の SceneBuffer と並びを合わせること)
	// float3 の後ろにパディングが要るのは、HLSL のベクトルが
	// 16 バイト境界をまたげないため（C++ 側と配置がずれる）
	struct SceneMatrix
	{
		DirectX::XMMATRIX view;          // ビュー行列（PMD のスフィアマップ用）
		DirectX::XMMATRIX viewProj;      // ビュー × プロジェクション
		DirectX::XMMATRIX lightCamera[3];   // 光源から見たビュー×プロジェクション
		DirectX::XMFLOAT3 eye;           // 視点座標
		float _pad0;
		DirectX::XMFLOAT3 lightVec;      // 光が進む向き
		float shadowMapTexel;            // シャドウマップ 1 テクセルの UV 幅（1 / 解像度）
		DirectX::XMFLOAT4 cascadeRadius;        // x,y,z = 各カスケードの終端距離（ビュー空間 Z）
		DirectX::XMFLOAT4 cascadeNormalOffset;
		DirectX::XMFLOAT4 shadowCenter;
		float lightRange;                // 光源カメラの深度範囲（far - near）
		float time;
		float _pad1[2];
	};

	static_assert(sizeof(SceneMatrix) % 16 == 0, "HLSL の定数バッファは 16 バイト単位で詰まる");

	// 1 フレームあたりのタイムスタンプの本数（パス境界の数）
	static constexpr UINT MarkerCount = 4;

	void StampGpuTimer(UINT marker);
	void ResolveGpuTimer();     // EndDraw の中
	void ReadGpuTimer();

	// 1 フレーム分の、CPU と GPU で取り合いになるリソース
	struct FrameResource
	{
		ComPtr<ID3D12CommandAllocator> allocator;
		UINT64 fenceValue = 0;   // このフレームの描画完了を表すフェンスの値
	};

	// -- 初期化のサブルーチン（Init から順に呼ばれる） --
	bool InitializeDXGIDevice();
	bool InitializeCommand();
	bool CreateSwapChain(HWND hwnd);
	bool CreateFinalRenderTargets();
	bool CreateDepthBuffer();
	bool CreateSceneConstantBuffer();
	bool CreateDefaultTextures();
	bool CreatePeraResources();
	bool CreateShadowMap();
	bool CreateGpuTimer();

	// 既定テクスチャの生成
	ComPtr<ID3D12Resource> CreateWhiteTexture();
	ComPtr<ID3D12Resource> CreateBlackTexture();
	ComPtr<ID3D12Resource> CreateGrayGradationTexture();

	// -- デバイスと DXGI --
	ComPtr<ID3D12Device> _dev;
	ComPtr<IDXGIFactory6> _dxgiFactory;
	ComPtr<IDXGISwapChain4> _swapchain;

	// -- コマンド関連 --
	ComPtr<ID3D12GraphicsCommandList> _cmdList;
	ComPtr<ID3D12CommandQueue> _cmdQueue;
	ComPtr<ID3D12Fence> _fence;
	UINT64 _fenceVal = 0;

	// -- レンダーターゲット（バックバッファ） --
	ComPtr<ID3D12DescriptorHeap> _rtvHeaps;
	std::vector<ComPtr<ID3D12Resource>> _backBuffers;

	// -- 深度バッファ --
	ComPtr<ID3D12Resource> _depthBuffer;
	ComPtr<ID3D12DescriptorHeap> _dsvHeap;

	// -- マルチパスレンダリング用のテクスチャ --
	ComPtr<ID3D12Resource> _peraResource;
	ComPtr<ID3D12DescriptorHeap> _peraRTVHeap;
	ComPtr<ID3D12DescriptorHeap> _peraSRVHeap;

	// -- シャドウマップ --
	// 1 枚のリソースを深度バッファ(DSV)とテクスチャ(SRV)の両方として使う
	ComPtr<ID3D12Resource> _shadowMap;
	ComPtr<ID3D12DescriptorHeap> _shadowDSVHeap;
	ComPtr<ID3D12DescriptorHeap> _shadowSRVHeap;

	// -- シーン用定数バッファ --
	ComPtr<ID3D12Resource> _sceneConstBuff;
	uint8_t* _mappedScene = nullptr;   // マップ先の先頭(バイト単位で進めるので uint8_t*)
	UINT _sceneCBStride = 0;

	// 今のフレームが書き込む領域
	SceneMatrix* Scene()
	{
		return reinterpret_cast<SceneMatrix*>(_mappedScene + _frameIndex * _sceneCBStride);
	}

	// -- 平行光源 --
	DirectX::XMFLOAT3 _lightVec = { 1.0f, -1.0f, 1.0f };   // 光が進む向き（斜め上から）
	float _lightDistance = 150.0f;   // 注視点から光源を引く距離
	DirectX::XMMATRIX _lightCamera[GameConfig::CascadeCount];
	float _shadowAreaScale = 1.0f;      // 影を落とす範囲の倍率

	// -- テクスチャ --
	ComPtr<ID3D12Resource> _whiteTex;
	ComPtr<ID3D12Resource> _blackTex;
	ComPtr<ID3D12Resource> _gradTex;

	// ファイル名パスとリソースのマップテーブル
	std::unordered_map<std::string, ComPtr<ID3D12Resource>> _resourceTable;

	// -- 描画時に使い回す設定 --
	D3D12_VIEWPORT _viewport = {};
	D3D12_RECT _scissorrect = {};
	D3D12_RESOURCE_BARRIER _barrierDesc = {};
	UINT _currentBackBufferIdx = 0;

	int _windowWidth = 0;
	int _windowHeight = 0;
	bool _vsyncEnabled = true;   // 垂直同期を待つか

	FrameResource _frames[FrameCount];
	UINT _frameIndex = 0;
	HANDLE _fenceEvent = nullptr;

	ComPtr<ID3D12QueryHeap> _queryHeap;
	ComPtr<ID3D12Resource> _queryReadback;
	UINT64 _timestampFreq = 0;        // 1 秒あたりのカウント数
	UINT _gpuTimerWarmup = 0;
	float _gpuPassMs[static_cast<size_t>(GpuPass::Count)] = {};
	UINT _currentCascade = 0;
};
