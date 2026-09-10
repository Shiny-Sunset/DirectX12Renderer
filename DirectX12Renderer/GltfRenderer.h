#pragma once

#include <d3d12.h>
#include <wrl.h>

class Dx12Wrapper;

// GLB モデルを描画するためのパイプライン(ルートシグネチャ、シェーダー、
// パイプラインステート)を受け持つクラス
// モデルのデータそのものは GltfActor 側が持つ
class GltfRenderer
{
public:
	explicit GltfRenderer(Dx12Wrapper& dx12);
	~GltfRenderer() = default;

	GltfRenderer(const GltfRenderer&) = delete;
	GltfRenderer& operator=(const GltfRenderer&) = delete;

	// 初期化(ルートシグネチャとパイプラインステートの作成)
	// @return 成功したら true
	bool Init();

	// 描画前に、モデル間で共通の状態を設定する
	void BeforeDraw();

	ID3D12PipelineState* PipelineState() const { return _pipelineState.Get(); }
	ID3D12PipelineState* BlendPipelineState() const { return _blendPipelineState.Get(); }
	ID3D12RootSignature* RootSignature() const { return _rootSignature.Get(); }
	ID3D12PipelineState* OutlinePipelineState() const { return _outlinePipelineState.Get(); }

private:
	// ヘッダーのグローバルスコープに using 宣言を置かないためのクラススコープの別名
	template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	// シェーダーのコンパイル
	bool CompileShaders();

	// ルートシグネチャの作成
	bool CreateRootSignature();

	// グラフィックスパイプラインステートの作成
	bool CreateGraphicsPipeline();

	Dx12Wrapper& _dx12;

	ComPtr<ID3DBlob> _vsBlob;
	ComPtr<ID3DBlob> _psBlob;
	ComPtr<ID3DBlob> _outlineVsBlob;
	ComPtr<ID3DBlob> _outlinePsBlob;

	ComPtr<ID3D12RootSignature> _rootSignature;
	ComPtr<ID3D12PipelineState> _pipelineState;
	ComPtr<ID3D12PipelineState> _blendPipelineState;
	ComPtr<ID3D12PipelineState> _outlinePipelineState;
};
