#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>

// DirectX12 の基盤部分をまとめたクラス
// デバイス、スワップチェーン、コマンド関連、レンダーターゲット、深度バッファ、
// シーン共通の定数バッファ(b0)、テクスチャの生成とキャッシュを受け持つ
class Dx12Wrapper
{
public:
	Dx12Wrapper() = default;
	~Dx12Wrapper();

	// COM オブジェクトを抱えるのでコピーは禁止する
	Dx12Wrapper(const Dx12Wrapper&) = delete;
	Dx12Wrapper& operator=(const Dx12Wrapper&) = delete;

	// 初期化
	// @param hwnd 描画先のウィンドウハンドル
	// @param windowWidth ウィンドウ幅
	// @param windowHeight ウィンドウ高さ
	// @return 成功したら true
	bool Init(HWND hwnd, int windowWidth, int windowHeight);

	// シーンの更新(モデルの回転)
	void Update();

	// 描画開始
	// リソースバリア、レンダーターゲットと深度バッファの設定、クリア、
	// ビューポートとシザー矩形の設定まで行う
	void BeginDraw();

	// 描画終了
	// リソースバリア、コマンドリストのクローズと実行、GPU の完了待ち、リセットまで行う
	void EndDraw();

	// 画面のスワップ(フリップ)
	void Flip();

	// シーン用定数バッファ(b0)のビューを、指定されたハンドルの位置に作る
	// CBV_SRV_UAV のディスクリプタヒープは同時に 1 本しかバインドできないため、
	// シーン用のビューも PMDActor が持つマテリアル用ヒープの先頭に同居させる
	// @param handle ビューを作成する位置の CPU ディスクリプタハンドル
	void CreateSceneConstantBufferView(D3D12_CPU_DESCRIPTOR_HANDLE handle);

	// -- 各種オブジェクトの取得 --
	ID3D12Device* Device() const { return _dev.Get(); }
	ID3D12GraphicsCommandList* CommandList() const { return _cmdList.Get(); }
	ID3D12CommandQueue* CommandQueue() const { return _cmdQueue.Get(); }
	IDXGISwapChain4* Swapchain() const { return _swapchain.Get(); }

	// -- テクスチャ --
	// テクスチャをファイルから読み込む
	// 同じパスに対する 2 回目以降の呼び出しはキャッシュを返す
	// @param texPath アプリケーションから見たテクスチャファイルパス
	// @return 生成したテクスチャバッファ。失敗した場合は nullptr
	ID3D12Resource* GetTextureByPath(const std::string& texPath);

	// テクスチャが指定されなかったときに使う既定のテクスチャ
	ID3D12Resource* WhiteTexture() const { return _whiteTex.Get(); }
	ID3D12Resource* BlackTexture() const { return _blackTex.Get(); }
	ID3D12Resource* GradTexture() const { return _gradTex.Get(); }

private:
	// ヘッダーのグローバルスコープに using 宣言を置くと、
	// インクルードした全ての翻訳単位に名前が漏れるため、クラススコープの別名にとどめる
	template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	// シェーダーに渡すシーン共通の行列
	// (BasicShaderHeader.hlsli の cbuff0 と並びを合わせること)
	struct SceneMatrix
	{
		DirectX::XMMATRIX world;	// ワールド行列
		DirectX::XMMATRIX view;		// ビュー行列(スフィアマップ用にビュー空間の法線を求めるのに使う)
		DirectX::XMMATRIX proj;		// プロジェクション行列
		DirectX::XMFLOAT3 eye;		// 視点座標
	};

	// -- 初期化のサブルーチン --
	bool InitializeDXGIDevice();
	bool InitializeCommand();
	bool CreateSwapChain(HWND hwnd);
	bool CreateFinalRenderTargets();
	bool CreateDepthBuffer();
	bool CreateSceneConstantBuffer();
	bool CreateDefaultTextures();

	// 既定テクスチャの生成
	ComPtr<ID3D12Resource> CreateWhiteTexture();
	ComPtr<ID3D12Resource> CreateBlackTexture();
	ComPtr<ID3D12Resource> CreateGrayGradationTexture();

	// GPU の処理完了を待つ
	void WaitForCommandQueue();

	// -- デバイスと DXGI --
	ComPtr<ID3D12Device> _dev;
	ComPtr<IDXGIFactory6> _dxgiFactory;
	ComPtr<IDXGISwapChain4> _swapchain;

	// -- コマンド関連 --
	ComPtr<ID3D12CommandAllocator> _cmdAllocator;
	ComPtr<ID3D12GraphicsCommandList> _cmdList;
	ComPtr<ID3D12CommandQueue> _cmdQueue;
	ComPtr<ID3D12Fence> _fence;
	UINT64 _fenceVal = 0;

	// -- レンダーターゲット --
	ComPtr<ID3D12DescriptorHeap> _rtvHeaps;
	std::vector<ComPtr<ID3D12Resource>> _backBuffers;

	// -- 深度バッファ --
	ComPtr<ID3D12Resource> _depthBuffer;
	ComPtr<ID3D12DescriptorHeap> _dsvHeap;

	// -- シーン用定数バッファ --
	ComPtr<ID3D12Resource> _sceneConstBuff;
	SceneMatrix* _mappedScene = nullptr;	// _sceneConstBuff のマップ先(Unmap はデストラクタで行う)
	float _angle = 0.0f;					// モデルの回転角

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
};
