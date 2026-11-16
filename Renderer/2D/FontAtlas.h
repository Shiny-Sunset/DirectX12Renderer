#pragma once
#include <d3d12.h>
#include <DirectXMath.h>
#include <string>
#include <wrl.h>
#include <vector>
#include <unordered_map>

class Dx12Wrapper;

class FontAtlas
{
public:
    struct Glyph
    {
        float u0, v0, u1, v1;   // アトラス上の UV
        int   width, height;    // ピクセル
        int   bearingX;         // ペン位置から左端まで
        int   bearingY;         // ベースラインから上端まで
        int   advance;          // 次の文字までの送り
    };

    explicit FontAtlas(Dx12Wrapper& dx12);

    // charset に入れた文字しか描けない。起動時に 1 回だけ焼く
    bool Build(const wchar_t* fontName, int sizePx, const std::wstring& charset, UINT atlasSize = 1024);

    const Glyph* Find(wchar_t c) const;   // 無ければ nullptr
    int Ascent() const { return _ascent; }
    int LineHeight() const { return _lineHeight; }
    ID3D12Resource* Texture() const { return _texture.Get(); }
    int DigitAdvance() const { return _digitAdvance; }

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreateTexture(const std::vector<uint8_t>& atlas);

    Dx12Wrapper& _dx12;
    ComPtr<ID3D12Resource> _texture;
    std::unordered_map<wchar_t, Glyph> _glyphs;
    UINT _atlasSize = 1024;
    int _ascent = 0;
    int _lineHeight = 0;
    int _digitAdvance = 0;
};