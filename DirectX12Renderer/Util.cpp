#include "Util.h"

#include <algorithm>
#include <cassert>
#include <iostream>

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

size_t AlignmentedSize(size_t size, size_t alignment)
{
	// alignment - 1 を足してから下位ビットを切り捨てる
	// size が既に alignment の倍数のときは、そのままのサイズが返る
	return (size + alignment - 1) & ~(alignment - 1);
}

std::string GetExtension(const std::string& path)
{
	int idx = path.rfind('.');
	return path.substr(idx + 1, path.length() - idx - 1);
}

std::pair<std::string, std::string> SplitFileName(const std::string& path, const char splitter)
{
	int idx = path.find(splitter);
	std::pair<std::string, std::string> ret;
	ret.first = path.substr(0, idx);
	ret.second = path.substr(idx + 1, path.length() - idx - 1);
	return ret;
}

std::string GetTexturePathFromModelAndTexPath(const std::string& modelPath, const char* texPath)
{
	int pathIndex1 = modelPath.rfind('/');
	int pathIndex2 = modelPath.rfind('\\');
	auto pathIndex = std::max(pathIndex1, pathIndex2);
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
