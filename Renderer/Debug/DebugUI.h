#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <vector>

class Dx12Wrapper;
struct ImGui_ImplDX12_InitInfo;

// Dear ImGui の初期化・フレーム管理・描画を受け持つクラス
// 何を表示するかは持たず、ImGui::Begin 〜 End は呼び出し側で書く
class DebugUI
{
public:
    explicit DebugUI(Dx12Wrapper& dx12);
    ~DebugUI();

    DebugUI(const DebugUI&) = delete;
    DebugUI& operator=(const DebugUI&) = delete;

    // 初期化
      // @param hwnd 描画先のウィンドウ
      // @return 成功したら true
    bool Init(HWND hwnd);

    // フレームの開始（ImGui::Begin より前に呼ぶ）
    void BeginFrame();

    // フレームの終了（ImGui::End より後に呼ぶ。描画データを確定する）
    void EndFrame();

    // 描画（Dx12Wrapper::BeginDraw と EndDraw の間、全ての描画の最後に呼ぶ）
    void Draw();

    // ImGui が入力を使っているか（ゲーム側の操作を止める判断に使う）
    bool WantCaptureMouse() const;
    bool WantCaptureKeyboard() const;

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    // ImGui のバックエンドから呼ばれる、SRV ディスクリプタの確保・解放
    static void AllocSrv(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
    static void FreeSrv(ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);

    Dx12Wrapper& _dx12;

    // ImGui 専用のディスクリプタヒープ（フォントテクスチャの SRV 置き場）
    ComPtr<ID3D12DescriptorHeap> _srvHeap;
    std::vector<UINT> _freeSrvIndices;   // 空いているスロット番号
    UINT _srvIncSize = 0;

    bool _initialized = false;
};