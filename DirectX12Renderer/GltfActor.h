#pragma once

#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <string>
#include <vector>

#include "GltfModel.h"

class Dx12Wrapper;
class GltfRenderer;

// 画面に出るキャラクター 1 体分（見た目）を受け持つクラス
// 頂点・テクスチャ・アニメーションのデータは GltfModel が持ち、複数のアクターで共有する。
// このクラスは「その 1 体だけの情報」＝ 位置・向き・現在のポーズ・定数バッファを持つ
class GltfActor
{
public:
    GltfActor(Dx12Wrapper& dx12, GltfRenderer& renderer, GltfModel& model);
    ~GltfActor();

    GltfActor(const GltfActor&) = delete;
    GltfActor& operator=(const GltfActor&) = delete;

    // @param ownBuffer 自分用のインスタンスバッファを持つか。
    //                  false のアクターは、外（EnemyPool）がまとめて詰めて描く
    bool Init(bool ownBuffer = true);

    // -- 毎フレームの更新と描画 --
    // アニメーションを進め、ボーン行列とワールド行列を GPU へ送る
    void Update(float deltaTime);

    const GltfModel& Model() const { return _model; }

    // インスタンスバッファへ詰めるために外から読む
    DirectX::XMMATRIX WorldMatrix() const;
    const std::vector<DirectX::XMMATRIX>& BoneMatrices() const { return _boneMatrices; }

    // 描画（GltfRenderer::BeforeDraw() の後に呼ぶ）
    void Draw();

    // 影を落とすためだけの描画（深度だけを書く）
    void DrawShadow();

    // -- アニメーション --
    // 名前でアニメーションを選んで再生を開始する
    // @param blendSeconds 前のアニメーションから混ぜながら移行する秒数（0 で即座に切り替え）
    // @return 見つかったら true
    bool PlayAnimation(const std::string& name, float blendSeconds = 0.2f, bool loop = true);

    // アニメーションを止めて、バインドポーズに戻す
    void StopAnimation();

    const char* CurrentAnimationName() const
    {
        return _currentAnimation < 0 ? "(none)" : _model.Animations()[_currentAnimation].name.c_str();
    }

    // クロスフェードの進み具合（0 = 移行元、1 = 移行先）
    float BlendWeight() const
    {
        return _prevAnimation < 0 ? 1.0f : 1.0f - _blendRemain / _blendDuration;
    }

    // -- 配置 --
    void SetPosition(const DirectX::XMFLOAT3& pos) { _position = pos; }
    const DirectX::XMFLOAT3& Position() const { return _position; }

    // Y 軸まわりの向き（ラジアン）。0 で +Z を向く
    void SetRotationY(float radian) { _rotationY = radian; }
    float RotationY() const { return _rotationY; }

    void SetScale(float scale) { _scale = scale; }
    float Scale() const { return _scale; }

    // -- 表示の切り替え --
    void SetOutlineEnabled(bool enabled) { _outlineEnabled = enabled; }
    bool IsOutlineEnabled() const { return _outlineEnabled; }

    void SetVisible(bool v) { _visible = v; }
    bool IsVisible() const { return _visible; }

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // シェーダーに渡すモデル固有の行列
    struct TransformBufferData
    {
        GltfInstanceData instance;
        DirectX::XMMATRIX bones[GltfModel::MaxBoneCount];
    };

    // SampleAnimation がフレームをまたいで持つ状態
    struct SampleCache
    {
        int animIndex = -1;           // out にバインドポーズを書いたときのアニメ番号
        std::vector<size_t> cursor;   // チャンネルごとの、前回見つけたキーの位置
    };

    // -- 初期化のサブルーチン --
    bool CreateTransformBuffer();          // ワールド行列とボーン行列の定数バッファ(b1)

    // -- 毎フレームの内部処理 --
    void UpdateBoneMatrices();   // 現在のポーズから _boneMatrices を作り直して GPU へ送る
    void UpdateWorldMatrix();    // _position などから world を作って GPU へ送る

    // アニメーション 1 本を指定時刻でサンプリングして、ノードの配列に書き出す
    // @param animIndex アニメーション番号
    // @param timeSec 再生位置（秒）
    // @param out 書き出し先（バインドポーズで初期化してから上書きする）
    void SampleAnimation(int animIndex, float timeSec, std::vector<GltfModel::Node>& out, SampleCache& cache) const;

    // 2 つのポーズを混ぜる
    // @param weight 0 で a、1 で b
    void BlendNodes(const std::vector<GltfModel::Node>& a, const std::vector<GltfModel::Node>& b,
        float weight, std::vector<GltfModel::Node>& out) const;

    TransformBufferData* Transform();
    D3D12_GPU_VIRTUAL_ADDRESS InstanceAddress() const;   // t2 用
    D3D12_GPU_VIRTUAL_ADDRESS BoneAddress() const;       // t3 用

    // -- 参照するもの（所有しない） --
    Dx12Wrapper& _dx12;
    GltfRenderer& _renderer;
    GltfModel& _model;

    // -- 配置 --
    DirectX::XMFLOAT3 _position = { 0.0f, 0.0f, 0.0f };
    float _rotationY = 0.0f;
    float _scale = 1.0f;

    // -- 現在のポーズ（このアクターだけのもの） --
    std::vector<GltfModel::Node> _animNodes;    // 最終的なポーズ
    std::vector<GltfModel::Node> _blendNodes;   // 移行元のポーズ（作業用）
    std::vector<DirectX::XMMATRIX> _boneMatrices;
    std::vector<DirectX::XMMATRIX> _globals;
    mutable SampleCache _animCache;    // _animNodes 用
    mutable SampleCache _blendCache;   // _blendNodes 用

    // -- アニメーションの再生状態 --
    int _currentAnimation = -1;   // 再生中のアニメーション(-1 = 停止)
    float _animTime = 0.0f;
    bool _loop = true;

    // ブレンド（移行元として残っている、1 つ前のアニメーション）
    int _prevAnimation = -1;      // -1 ならブレンド中ではない
    float _prevAnimTime = 0.0f;
    float _blendRemain = 0.0f;    // 残りの移行時間（秒）
    float _blendDuration = 0.0f;  // 移行にかける時間（割合の計算に使う）

    // -- GPU リソース --
    ComPtr<ID3D12Resource> _transformBuff;
    uint8_t* _mappedTransform = nullptr;   // マップしたまま保持
    UINT _transformStride = 0;

    // -- 表示 --
    bool _outlineEnabled = true;   // 輪郭線を描くか
    bool _visible = true;
};
