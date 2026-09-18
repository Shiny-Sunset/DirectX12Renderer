#include "DebugUI.h"

#include "Dx12Wrapper.h"
#include "Util.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

namespace
{
    // ImGui 用ヒープのスロット数
    // フォントテクスチャ 1 枚あれば足りるが、バージョンによって追加で確保することがあるので余裕を持たせる
    constexpr UINT SrvHeapSize = 64;
}

DebugUI::DebugUI(Dx12Wrapper& dx12)
    : _dx12(dx12)
{
}

DebugUI::~DebugUI()
{
    // バックエンドは COM オブジェクトを持つので、Dx12Wrapper より先に後始末する必要がある
    if (_initialized)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

bool DebugUI::Init(HWND hwnd)
{
    auto device = _dx12.Device();

    // -- ImGui 専用のディスクリプタヒープ --
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = SrvHeapSize;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;   // シェーダーから読むので必須

    auto result = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_srvHeap));
    if (!CheckResult(result, "CreateDescriptorHeap (ImGui)")) return false;

    _srvIncSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 空きスロットを全部積んでおく（後ろから取り出す）
    _freeSrvIndices.resize(SrvHeapSize);
    for (UINT i = 0; i < SrvHeapSize; ++i)
    {
        _freeSrvIndices[i] = SrvHeapSize - 1 - i;
    }

    // -- ImGui 本体 --
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FontScaleMain = 1.5f;

    // -- Win32 バックエンド（入力とウィンドウサイズ） --
    if (!ImGui_ImplWin32_Init(hwnd)) return false;

    // -- DX12 バックエンド（描画） --
    ImGui_ImplDX12_InitInfo info;
    info.Device = device;
    info.CommandQueue = _dx12.CommandQueue();
    info.NumFramesInFlight = 2;                               // スワップチェーンのバッファ数と合わせる
    info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;         // Dx12Wrapper の RTV と一致させる
    info.DSVFormat = DXGI_FORMAT_D32_FLOAT;                   // BeginDraw でバインドしている DSV と一致させる
    info.UserData = this;
    info.SrvDescriptorHeap = _srvHeap.Get();
    info.SrvDescriptorAllocFn = &DebugUI::AllocSrv;
    info.SrvDescriptorFreeFn = &DebugUI::FreeSrv;

    if (!ImGui_ImplDX12_Init(&info)) return false;

    _initialized = true;
    return true;
}

void DebugUI::AllocSrv(ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
{
    auto* self = static_cast<DebugUI*>(info->UserData);
    // 空きスロットを 1 つ取り出す
    const UINT idx = self->_freeSrvIndices.back();
    self->_freeSrvIndices.pop_back();

    outCpu->ptr = self->_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + idx * self->_srvIncSize;
    outGpu->ptr = self->_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + idx * self->_srvIncSize;
}

void DebugUI::FreeSrv(ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
{
    auto* self = static_cast<DebugUI*>(info->UserData);

    // ハンドルのアドレスからスロット番号を逆算して、空きに戻す
    const auto start = self->_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    const UINT idx = static_cast<UINT>((cpu.ptr - start) / self->_srvIncSize);
    self->_freeSrvIndices.push_back(idx);
}

void DebugUI::BeginFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::EndFrame()
{
    // ImGui::Begin 〜 End で積んだ内容から、描画データを確定する（まだ描かない）
    ImGui::Render();
}

void DebugUI::Draw()
{
    auto cmdList = _dx12.CommandList();

    // ImGui 専用のヒープに切り替える
    ID3D12DescriptorHeap* heaps[] = { _srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    // ルートシグネチャ・パイプラインステートは ImGui が自分で設定する
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}

bool DebugUI::WantCaptureMouse() const { return ImGui::GetIO().WantCaptureMouse; }
bool DebugUI::WantCaptureKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }