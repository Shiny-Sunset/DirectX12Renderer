#pragma once

#include <Windows.h>
#include <string>
#include <utility>

// HRESULT を検査し、失敗ならエラーメッセージを出して false を返す
// 成功時は _DEBUG のときだけ "is OK" を出力する
// @param result 検査する HRESULT
// @param what 処理名(メッセージに出力される)
// @return 成功したら true
bool CheckResult(HRESULT result, const char* what);

// アライメントにそろえたサイズを返す
// @param size 元のサイズ
// @param alignment アライメントサイズ
// @return アライメントをそろえたサイズ
size_t AlignmentedSize(size_t size, size_t alignment);

// ファイル名から拡張子を取得する
// @param path 対象のパス文字列
// @return 拡張子
std::string GetExtension(const std::string& path);

// テクスチャのパスをセパレーター文字で分離する
// @param path 対象のパス文字列
// @param splitter 区切り文字
// @return 分離前後の文字列ペア
std::pair<std::string, std::string> SplitFileName(const std::string& path, const char splitter = '*');

// モデルのパスとテクスチャのパスから合成パスを得る
// @param modelPath アプリケーションから見た pmd モデルのパス
// @param texPath PMD モデルから見たテクスチャのパス
// @return アプリケーションから見たテクスチャのパス
std::string GetTexturePathFromModelAndTexPath(const std::string& modelPath, const char* texPath);

// std::string(マルチバイト文字列) から std::wstring(ワイド文字列) を得る
// @param str マルチバイト文字列
// @return 変換されたワイド文字列
std::wstring GetWideStringFromString(const std::string& str);
