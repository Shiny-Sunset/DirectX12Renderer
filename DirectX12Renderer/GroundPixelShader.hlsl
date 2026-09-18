struct GroundOutput
{
    float4 svpos : SV_POSITION;
    float3 worldPos : POSITION;
};

float4 GroundPS(GroundOutput input) : SV_TARGET
{
    // 1 メートルごとに線を引く
    // fwidth で「1 ピクセルあたり何メートル進むか」を取り、線の太さを画面基準で一定にする
    float2 uv = input.worldPos.xz;
    float2 width = fwidth(uv);
    float2 grid = abs(frac(uv - 0.5) - 0.5) / max(width, 1e-5);
    float lineMask = saturate(min(grid.x, grid.y));

    float3 lineColor = float3(0.30, 0.30, 0.35);
    float3 baseColor = float3(0.55, 0.55, 0.60);
    return float4(lerp(lineColor, baseColor, lineMask), 1.0);
}