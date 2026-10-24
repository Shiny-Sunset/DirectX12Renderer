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

cbuffer SceneBuffer : register(b0)
{
    matrix view;
    matrix viewProj;
    matrix lightCamera[3];
    float3 eye;
    float _pad0;
    float3 lightVec;
    float shadowMapTexel; // 1 / 2048（全カスケード共通）
    float4 cascadeRadius; // x, y = 分割距離（ビュー空間 Z）
    float4 cascadeNormalOffset; // x, y, z = 押し出し量（m）
    float4 shadowCenter;
    float lightRange;
    float3 _pad1;
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