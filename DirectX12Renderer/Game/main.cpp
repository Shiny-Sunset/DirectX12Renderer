#include "Application.h"
#include <cstdlib>
#include <crtdbg.h>

// エントリーポイント
// 実際の処理は Application が受け持つ
#ifdef _DEBUG
int main()
{
	// アサーションをダイアログではなく stderr へ
	for (int type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
	// 中断時の Windows エラー報告ダイアログも止める
	// 理由は知りたいので、_WRITE_ABORT_MSG のほうは残す
	_set_abort_behavior(0, _CALL_REPORTFAULT);
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif // _DEBUG
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	auto& app = Application::Instance();

	if (!app.Init())
	{
		app.Terminate();
		return -1;
	}

	app.Run();

	app.Terminate();

	return 0;
}
