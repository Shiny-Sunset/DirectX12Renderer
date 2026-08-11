#include "BasicShaderHeader.hlsli"

Output BasicVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
    min16uint2 boneno : BONE_NO,
    min16uint weight : WEIGHT
)
{
    float w = weight / 100.0f;
    matrix bm = bones[boneno[0]] * w + bones[boneno[1]] * (1 - w);
    Output output;  // ピクセルシェーダーに渡す値
    pos = mul(bm, pos);
    pos = mul(world, pos);
    output.svpos = mul(mul(mul(proj, view), world), pos);  // シェーダーでは列優先なのでDirectXと行列の計算する順番が逆になるので注意
    normal.w = 0;   // ここが重要（法線の平行移動成分は無効にする）
    output.pos = pos;   // ワールド空間の頂点座標
    output.ray = normalize(pos.xyz - eye);
    output.normal = mul(world, normal); // 法線にもワールド変換を行う
    output.vnormal = mul(view, output.normal);  // スフィアマップはカメラから見た向きで引くのでビュー空間に変換する
    output.uv = uv;
	return output;
}