// 影と陰影の計算（地面・箱で共通）
// ※ このファイルは SceneBuffer(b0) を宣言したヘッダの「後に」include すること

// 影に入っているかだけを返す（0 = 完全に影、1 = 光が当たっている）
float ComputeShadowLit(float4 tpos, float ndl, Texture2D<float> shadowMap, SamplerComparisonState shadowSmp)
{
    float lit = 0.0;
    if (ndl <= 0.0)
        return lit;
    

    // ずらす量をメートルで指定する
    static const float BiasMeters = 0.02; // 基本 2cm
    static const float SlopeMeters = 0.05; // 斜めに当たる面はさらに最大 5cm
    
    // 光源から見た位置を、テクスチャ座標へ
    float3 posFromLight = tpos.xyz / tpos.w;
    float2 shadowUV = (posFromLight.xy + float2(1, -1)) * float2(0.5, -0.5);

        // 斜めに当たるほどアクネが出やすいので、傾きに応じてバイアスを増やす
        // メートル → 深度(0〜1) へ換算する
    float bias = (BiasMeters + SlopeMeters * (1.0 - ndl)) / lightRange;

    float depth = posFromLight.z - bias;
    
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            lit += shadowMap.SampleCmpLevelZero(
                shadowSmp, shadowUV + float2(x, y) * shadowMapTexel, depth);
        }
    }
    lit /= 9.0;

    lit *= saturate(ndl * 2.0);
    
    return lit;
}

// @param n 面の法線（正規化済み）
// @param lightVec 光が進む向き
// @param tpos 光源から見た位置（同次座標）
// @param shadowMap シャドウマップ
// @param shadowSmp 比較用サンプラー
// @return 明るさの倍率（影なら暗い値）
float ComputeBrightness(float3 n, float3 lightVec, float4 tpos,
                        Texture2D<float> shadowMap, SamplerComparisonState shadowSmp)
{
    static const float ShadowLevel = 0.4; // 影の暗さ
    
    float ndl = dot(-normalize(lightVec), n);
    float lit = ComputeShadowLit(tpos, ndl, shadowMap, shadowSmp) * saturate(ndl * 2.0);
    return lerp(ShadowLevel, 1.0, lit);
}