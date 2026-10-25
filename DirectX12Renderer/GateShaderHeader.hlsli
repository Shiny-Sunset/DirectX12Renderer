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
    float time;
    float2 _pad1;
};

struct GateOutput
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD;
};

// 座標から 0〜1 の乱数を作る。テクスチャもサンプラーも使わない
float Hash(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

// 格子の 4 隅の乱数を補間する
float ValueNoise(float2 p)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f); // なめらかに。線形のままだと格子が目に見える

    return lerp(lerp(Hash(i + float2(0, 0)), Hash(i + float2(1, 0)), f.x),
                lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), f.x), f.y);
}

// 周波数を倍・振幅を半分にしながら重ねる。
float Fbm(float2 p)
{
    float v = 0.0;
    float a = 0.5;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        v += a * ValueNoise(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}