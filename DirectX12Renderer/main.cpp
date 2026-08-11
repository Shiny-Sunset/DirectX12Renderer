#include "Application.h"

// エントリーポイント
// 実際の処理は Application が受け持つ
#ifdef _DEBUG
int main()
{
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
#endif // _DEBUG
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
