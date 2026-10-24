#include "FontAtlas.h"
#include "Dx12Wrapper.h"
#include "Util.h"
#include <iostream>

FontAtlas::FontAtlas(Dx12Wrapper& dx12): _dx12(dx12)
{ }

bool FontAtlas::Build(const wchar_t* fontName, int sizePx, const std::wstring& charset, UINT atlasSize)
{
    _atlasSize = atlasSize;
    _glyphs.clear();
    HDC hdc = CreateCompatibleDC(nullptr);
    if (hdc == nullptr) return false;

    HFONT font = CreateFontW(
        sizePx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
        PROOF_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontName);
    if (font == nullptr) { DeleteDC(hdc); return false; }

    HGDIOBJ oldFont = SelectObject(hdc, font);

    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    _ascent = tm.tmAscent;                              // 上端からベースラインまで
    _lineHeight = tm.tmHeight + tm.tmExternalLeading;   // 改行の送り

    // GetGlyphOutlineW に渡す変換行列。16.16 の固定小数なので、1.0 は { 0, 1 }
    const MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };

    std::vector<uint8_t> atlas(_atlasSize * _atlasSize, 0);
    UINT penX = 0, penY = 0, rowHeight = 0;

    for (wchar_t ch : charset)
    {
        GLYPHMETRICS gm = {};
        const DWORD size = GetGlyphOutlineW(hdc, ch, GGO_GRAY8_BITMAP, &gm, 0, nullptr, &mat);
        if (size == GDI_ERROR) continue;

        std::vector<uint8_t> bits(size);
        if (size > 0)
        {
            GetGlyphOutlineW(hdc, ch, GGO_GRAY8_BITMAP, &gm, size, bits.data(), &mat);
        }
        else
        {
            // size == 0 は空白。ビットマップが無いだけ
            // 送り幅(gmCellIncX)だけ使いたいので、箱は 0 にしておく
            gm.gmBlackBoxX = 0;
            gm.gmBlackBoxY = 0;
        }

        // 棚詰め。右端まで行ったら次の段へ。1px あけて隣の文字がにじまないようにする
        if (penX + gm.gmBlackBoxX + 1 > _atlasSize)
        {
            penX = 0;
            penY += rowHeight + 1;
            rowHeight = 0;
        }
        if (penY + gm.gmBlackBoxY > _atlasSize)
        {
            std::cout << "FontAtlas: アトラスが足りない。"
                << _glyphs.size() << " / " << charset.size() << " 文字で打ち切った\n";
            break;
        }

        // GGO_GRAY8_BITMAP は 0〜64 の 65 段階。行は 4 バイト境界に揃っている
        const UINT pitch = (gm.gmBlackBoxX + 3) & ~3u;
        for (UINT y = 0; y < gm.gmBlackBoxY; ++y)
        {
            for (UINT x = 0; x < gm.gmBlackBoxX; ++x)
            {
                const uint8_t v = bits[y * pitch + x];
                atlas[(penY + y) * _atlasSize + (penX + x)] =
                    static_cast<uint8_t>(v >= 64 ? 255 : v * 255 / 64);
            }
        }

        Glyph g = {};
        g.u0 = static_cast<float>(penX) / _atlasSize;
        g.v0 = static_cast<float>(penY) / _atlasSize;
        g.u1 = static_cast<float>(penX + gm.gmBlackBoxX) / _atlasSize;
        g.v1 = static_cast<float>(penY + gm.gmBlackBoxY) / _atlasSize;
        g.width = static_cast<int>(gm.gmBlackBoxX);
        g.height = static_cast<int>(gm.gmBlackBoxY);
        g.bearingX = gm.gmptGlyphOrigin.x;
        g.bearingY = gm.gmptGlyphOrigin.y;
        g.advance = gm.gmCellIncX;
        _glyphs[ch] = g;

        penX += gm.gmBlackBoxX + 1;
        if (gm.gmBlackBoxY > rowHeight) rowHeight = gm.gmBlackBoxY;
    }

    SelectObject(hdc, oldFont);
    DeleteObject(font);
    DeleteDC(hdc);
    // 数字の送り幅を '0' に揃えるために覚えておく
    if (const auto* zero = Find(L'0')) _digitAdvance = zero->advance;

    std::cout << "FontAtlas: " << _glyphs.size() << " 文字、"
        << (penY + rowHeight) << " / " << _atlasSize << " 行を使用\n";
    return CreateTexture(atlas);
}

const FontAtlas::Glyph* FontAtlas::Find(wchar_t c) const
{
    auto it = _glyphs.find(c);
    if (it == _glyphs.end()) return nullptr;   // charset に入れていない文字

    return &it->second;
}

bool FontAtlas::CreateTexture(const std::vector<uint8_t>& atlas)
{
    D3D12_HEAP_PROPERTIES heapProp = {};
    heapProp.Type = D3D12_HEAP_TYPE_CUSTOM;
    heapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Format = DXGI_FORMAT_R8_UNORM;   // カバレッジだけなので 1 チャンネル
    resDesc.Width = _atlasSize;
    resDesc.Height = _atlasSize;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    auto result = _dx12.Device()->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&_texture));
    if (!CheckResult(result, "CreateCommittedResource _fontAtlas")) return false;

    result = _texture->WriteToSubresource(
        0, nullptr, atlas.data(),
        _atlasSize,                                  // 1 行のバイト数。R8 なので幅と同じ
        static_cast<UINT>(atlas.size()));
    if (!CheckResult(result, "_fontAtlas->WriteToSubresource")) return false;

    return true;
}