#include "Util.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <wrl.h>
#include <filesystem>

namespace
{
	std::vector<std::wstring> g_shaderDirs{ L"" };   // 既定は作業ディレクトリ直下

	// ワイド文字列を std::cout へ出せる形にする
	std::string Narrow(const std::wstring& ws)
	{
		if (ws.empty()) return {};
		const int n = WideCharToMultiByte(CP_ACP, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (n <= 0) return {};
		std::string s(static_cast<size_t>(n) - 1, '\0');   // 終端の分を除く
		WideCharToMultiByte(CP_ACP, 0, ws.c_str(), -1, s.data(), n, nullptr, nullptr);
		return s;
	}

	// パスのフォルダ部分（末尾の区切りを含む）。区切りが無ければ空
	std::wstring DirectoryOf(const std::wstring& path)
	{
		const size_t i = path.find_last_of(L"/\\");
		return i == std::wstring::npos ? std::wstring() : path.substr(0, i + 1);
	}

	bool ReadWholeFile(const std::wstring& path, std::vector<char>& out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return false;
		out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());

		// 空ファイルだと data() が nullptr を返すことがある。
		// そのポインタを Close の鍵に使うので、1 バイトだけ入れておく
		if (out.empty()) out.push_back('\n');
		return true;
	}

	// シェーダーの #include を解決する
	// D3D_COMPILE_STANDARD_FILE_INCLUDE は「その .hlsl があるフォルダ」しか見ないため、
	// レンダラーとゲームでフォルダが分かれていると相対パスを書く必要があった。
	// 自前で持つと SetShaderDirectories の探索パスが #include にも効く
	class ShaderInclude : public ID3DInclude
	{
	public:
		explicit ShaderInclude(std::wstring baseDir) : _baseDir(std::move(baseDir)) {}

		HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE type, LPCSTR fileName,
			LPCVOID parentData, LPCVOID* outData, UINT* outBytes) override;
		HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override;

	private:
		struct Entry
		{
			std::vector<char> bytes;
			std::wstring dir;    // このファイルがあるフォルダ。入れ子の include の基準になる
		};

		std::wstring _baseDir;                          // 大元の .hlsl があるフォルダ
		std::unordered_map<const void*, Entry> _open;   // 開いている最中のファイル
	};

	HRESULT STDMETHODCALLTYPE ShaderInclude::Open(D3D_INCLUDE_TYPE type, LPCSTR fileName,
		LPCVOID parentData, LPCVOID* outData, UINT* outBytes)
	{
		// 入れ子の include は「その .hlsli があるフォルダ」が基準になる。
		// parentData は前に自分が返したポインタなので、それで親をたどれる。
		// 大元の .hlsl は D3DCompileFromFile が自分で読んでいて控えが無いので、
		// 見つからなければ _baseDir を使う
		const std::wstring* parentDir = &_baseDir;
		if (auto it = _open.find(parentData); it != _open.end()) parentDir = &it->second.dir;

		const std::wstring name = GetWideStringFromString(fileName);

		// 探索順。"..." は親のフォルダを先に見る
		std::vector<std::wstring> candidates;
		if (type == D3D_INCLUDE_LOCAL) candidates.push_back(*parentDir + name);
		for (const auto& dir : g_shaderDirs) candidates.push_back(dir + name);

		for (const auto& path : candidates)
		{
			Entry e;
			if (!ReadWholeFile(path, e.bytes)) continue;
			e.dir = DirectoryOf(path);

			// vector を move してもヒープのバッファは動かないので、
			// 先に取ったポインタは move した後も有効。これを Close の鍵にする
			const void* key = e.bytes.data();
			*outData = key;
			*outBytes = static_cast<UINT>(e.bytes.size());
			_open.emplace(key, std::move(e));
			return S_OK;
		}

		// どこを探したかを出す
		std::cout << "include が見つかりません: " << fileName << std::endl;
		for (const auto& c : candidates) std::cout << "  探した: " << Narrow(c) << std::endl;
		return E_FAIL;
	}

	HRESULT STDMETHODCALLTYPE ShaderInclude::Close(LPCVOID data)
	{
		_open.erase(data);   // 無ければ何も起きない
		return S_OK;
	}
}

bool CheckResult(HRESULT result, const char* what)
{
	if (FAILED(result))
	{
		std::cout << what << " is Failed (hr = 0x" << std::hex << result << std::dec << ")" << std::endl;
		return false;
	}
#ifdef _DEBUG
	std::cout << what << " is OK" << std::endl;
#endif // _DEBUG
	return true;
}

bool CheckShaderResult(HRESULT result, ID3DBlob* errorBlob, const char* what)
{
	if (SUCCEEDED(result))
	{
#ifdef _DEBUG
		std::cout << what << " is OK" << std::endl;
#endif // _DEBUG
		return true;
	}

	if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
	{
		std::cout << what << ": ファイルが見つかりません" << std::endl;
		::OutputDebugStringA("ファイルが見つかりません");
		return false;
	}

	std::cout << what << " is Failed" << std::endl;

	// エラー内容が取れないこともあるので、必ず null チェックしてから読む
	if (errorBlob != nullptr)
	{
		std::string errstr;
		errstr.resize(errorBlob->GetBufferSize());

		std::copy_n(
			(char*)errorBlob->GetBufferPointer(),
			errorBlob->GetBufferSize(),
			errstr.begin()
		);
		errstr += "\n";

		std::cout << errstr;
		::OutputDebugStringA(errstr.c_str());
	}
	return false;
}

size_t AlignmentedSize(size_t size, size_t alignment)
{
	// alignment - 1 を足してから下位ビットを切り捨てる
	// size が既に alignment の倍数のときは、そのままのサイズが返る
	return (size + alignment - 1) & ~(alignment - 1);
}

std::string GetExtension(const std::string& path)
{
	const size_t idx = path.rfind('.');
	if (idx == std::string::npos) return {};
	return path.substr(idx + 1);
}

std::pair<std::string, std::string> SplitFileName(const std::string& path, const char splitter)
{
	const size_t idx = path.find(splitter);
	if (idx == std::string::npos) return {};
	std::pair<std::string, std::string> ret;
	ret.first = path.substr(0, idx);
	ret.second = path.substr(idx + 1);
	return ret;
}

std::string GetTexturePathFromModelAndTexPath(const std::string& modelPath, const char* texPath)
{
	const size_t pathIndex1 = modelPath.rfind('/');
	const size_t pathIndex2 = modelPath.rfind('\\');
	auto pathIndex = std::max(pathIndex1, pathIndex2);
	if (pathIndex == std::string::npos) return {};
	auto folderPath = modelPath.substr(0, pathIndex + 1);
	return folderPath + texPath;
}

std::wstring GetWideStringFromString(const std::string& str)
{
	// 呼び出し 1 回目(文字列数を得る)
	auto num1 = MultiByteToWideChar(
		CP_ACP,
		MB_PRECOMPOSED | MB_ERR_INVALID_CHARS,
		str.c_str(),
		-1,
		nullptr,
		0
	);

	std::wstring wstr;	// string の wchar_t 版
	wstr.resize(num1);	// 得られた文字列数でリサイズ

	// 呼び出し 2 回目(確保済みの wstr に変換文字列をコピー)
	auto num2 = MultiByteToWideChar(
		CP_ACP,
		MB_PRECOMPOSED | MB_ERR_INVALID_CHARS,
		str.c_str(),
		-1,
		&wstr[0],
		num1
	);

	assert(num1 == num2);	// 一応チェック
	return wstr;
}

void SetShaderDirectories(std::vector<std::wstring> dirs)
{
	g_shaderDirs = std::move(dirs);
	if (g_shaderDirs.empty()) g_shaderDirs.push_back(L"");
}

bool CompileShader(const wchar_t* fileName, const char* entry, const char* target, ID3DBlob** blob)
{
	std::wstring path;

	for (const auto& dir : g_shaderDirs)
	{
		std::wstring candidate = dir + fileName;
		if (std::filesystem::exists(candidate))
		{
			path = std::move(candidate);
			break;
		}
	}
	if (path.empty())
	{
		std::cout << entry << " : シェーダーのファイルが見つかりません" << std::endl;
		return false;
	}

	ShaderInclude include(DirectoryOf(path));   // コンパイル中だけ使うのでローカルでよい

	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	auto result = D3DCompileFromFile(
		path.c_str(),
		nullptr,    // define は無し
		&include,   // #include も g_shaderDirs から探す（ShaderInclude）
		entry, target,
		ShaderCompileFlags,
		0,
		blob, &errorBlob);

	// エントリポイント名はシェーダーごとに一意なので、そのまま処理名に使える
	return CheckShaderResult(result, errorBlob.Get(), entry);
}
