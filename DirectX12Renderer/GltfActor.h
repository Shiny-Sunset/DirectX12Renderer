#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <iostream>
#include <string>
#include <vector>
#include "GltfModel.h"

class Dx12Wrapper;
class GltfRenderer;

// glTF(.glb) モデル 1 体分のデータと描画を受け持つクラス
// 変換は GltfActor.cpp の無名名前空間にある ToLH / QuatToLH / MatrixToLH に集約している。
// 新しい種類のデータ(タンジェント、モーフターゲット、カメラ、ライト等)を
// 読み込むときも、必ずこれらを通すこと。
class GltfActor
{
public:
    explicit GltfActor(Dx12Wrapper& dx12, GltfRenderer& _renderer, GltfModel& model);
    ~GltfActor();

    GltfActor(const GltfActor&) = delete;
    GltfActor& operator=(const GltfActor&) = delete;

    // .glb を読み込んで描画に必要なリソースを作る
    bool Init();    // 定数バッファとディスクリプタヒープを作る（読み込みはしない）

    void Update(float deltaTime);
    // 描画（GltfRenderer::BeforeDraw() の後に呼ぶ）
    void Draw();

    void DrawShadow();   // 影を落とすためだけの描画

    // 名前でアニメーションを選んで再生を開始する
    // @param blendSeconds 前のアニメーションから混ぜながら移行する秒数（0 で即座に切り替え）
    // @return 見つかったら true
    bool PlayAnimation(const std::string& name, float blendSeconds = 0.2f);

    // -- 配置 --
    void SetPosition(const DirectX::XMFLOAT3& pos) { _position = pos; }
    const DirectX::XMFLOAT3& Position() const { return _position; }

    // Y 軸まわりの向き（ラジアン）
    void SetRotationY(float radian) { _rotationY = radian; }
    float RotationY() const { return _rotationY; }

    void SetScale(float scale) { _scale = scale; }
    float Scale() const { return _scale; }

    const char* CurrentAnimationName() { return _currentAnimation < 0 ? "(none)" : _model.Animations()[_currentAnimation].name.c_str(); }
    float BlendWeight() { return _prevAnimation < 0 ? 1.0f : 1.0f - _blendRemain / _blendDuration; }

    // 輪郭線の描画を切り替える
    void SetOutlineEnabled(bool enabled) { _outlineEnabled = enabled; }
    bool IsOutlineEnabled() const { return _outlineEnabled; }

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // シェーダーに渡すモデル固有の行列
    // (GltfShaderHeader.hlsli の cbuffer Transform と並びを合わせること)
    struct TransformBufferData
    {
        DirectX::XMMATRIX world;
        DirectX::XMMATRIX bones[GltfModel::MaxBoneCount];
    };

    // -- 初期化のサブルーチン --
    bool CreateMaterialAndTextureView();                // CreateDescriptorHeap を置き換え
    bool CreateTransformBuffer();                     // ボーン行列用の定数バッファ(b1)
    void UpdateBoneMatrices();                        // ノードから _boneMatrices を作り直す

    Dx12Wrapper& _dx12;
    GltfRenderer& _renderer;
    GltfModel& _model;

    std::vector<GltfModel::Node> _animNodes;
    std::vector<GltfModel::Node> _blendNodes;
    std::vector<DirectX::XMMATRIX> _boneMatrices;

    DirectX::XMFLOAT3 _position = { 0.0f, 0.0f, 0.0f };
    float _rotationY = 0.0f;
    float _scale = 1.0f;

    ComPtr<ID3D12Resource> _transformBuff;
    TransformBufferData* _mappedTransform = nullptr;    // マップしたまま保持

    ComPtr<ID3D12DescriptorHeap> _descHeap;

    int _currentAnimation = -1;                 // 再生中のアニメーション(-1 = 停止)
    float _animTime = 0.0f;

    // -- ブレンド（移行元として残っている、1 つ前のアニメーション） --
    int _prevAnimation = -1;                    // -1 ならブレンド中ではない
    float _prevAnimTime = 0.0f;
    float _blendRemain = 0.0f;                  // 残りの移行時間（秒）
    float _blendDuration = 0.0f;                // 移行にかける時間（割合の計算に使う）

    bool _outlineEnabled = true;   // 輪郭線を描くか

    void UpdateWorldMatrix();   // _position などから world を作って GPU へ書く

    // アニメーション 1 本を指定時刻でサンプリングして、ノードの配列に書き出す
    // @param animIndex アニメーション番号
    // @param timeSec 再生位置（秒）
    // @param out 書き出し先（バインドポーズで初期化してから上書きする）
    void SampleAnimation(int animIndex, float timeSec, std::vector<GltfModel::Node>& out) const;

    // 2 つのポーズを混ぜる
    // @param weight 0 で a、1 で b
    void BlendNodes(const std::vector<GltfModel::Node>& a, const std::vector<GltfModel::Node>& b,
        float weight, std::vector<GltfModel::Node>& out) const;

    void ApplyAnimation(float timeSec);         // _animNodes を書き換える
};