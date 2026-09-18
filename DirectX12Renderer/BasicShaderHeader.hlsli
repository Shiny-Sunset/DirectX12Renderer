// 頂点シェーダーからピクセルシェーダーへのやり取りに使用する構造体
struct Output
{
    float4 svpos : SV_POSITION; // システム用頂点座標
    float4 pos : POSITION;  // 頂点座標
    float3 ray : VECTOR;    // 視線ベクトル
    float4 normal : NORMAL0; // 法線ベクトル(ワールド空間)
    float4 vnormal : NORMAL1; // 法線ベクトル(ビュー空間。スフィアマップの uv に使う)
    float2 uv : TEXCOORD;   // uv 値
};

Texture2D<float4> tex : register(t0); // 0 番スロットに設定されたテクスチャ
Texture2D<float4> sph : register(t1); // 1 番スロットに設定されたテクスチャ(乗算スフィアマップ)
Texture2D<float4> spa : register(t2); // 2 番スロットに設定されたテクスチャ(加算スフィアマップ)
Texture2D<float4> toon : register(t3); // 3 番スロットに設定されたテクスチャ(トゥーン)
SamplerState smp : register(s0); // 0 番スロット設定されたサンプラー
SamplerState smpToon : register(s1); // 1 番スロット設定されたサンプラー(トゥーン用)

cbuffer cbuff0 : register(b0)   // 定数バッファ(シーン共通)
{
    matrix view; // ビュー行列
    matrix proj; // プロジェクション行列
    float3 eye; // 視点
};

// モデル固有の行列
cbuffer Transform : register(b1)
{
    matrix world; // ワールド変換行列
    matrix bones[256];
};

// マテリアル用
cbuffer Material : register(b2) // 定数バッファ
{
    float4 diffuse; // ディフューズ色
    float4 specular;    // スペキュラ
    float3 ambient; // アンビエント
};