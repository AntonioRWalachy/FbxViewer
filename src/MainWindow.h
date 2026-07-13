#pragma once
#include "SceneData.h" // inclui windows.h
#include <vector>
#include <memory>
#include "Renderer.h"

// Nome da classe da janela principal — usado tambem pela logica de
// single-instance no main.cpp (FindWindow) para localizar a instancia original.
inline constexpr wchar_t kMainWindowClassName[] = L"FbxViewerMainWnd";

// Assinatura do WM_COPYDATA usado para enviar um caminho de arquivo de uma
// segunda instancia para a instancia original ("FBX1" em ASCII).
inline constexpr ULONG_PTR kCopyDataOpenFile = 0x46425831;

// Representa um arquivo aberto numa aba: dados de cena + buffers de GPU +
// estado de visualizacao (camera, toggles) daquela aba especifica.
struct TabDocument
{
    SceneModel model;
    GpuModel gpuModel;
    OrbitCamera camera;

    bool showMaterial = true;
    bool showWireframe = false;
    bool showShadows = true;

    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11Texture2D> depthTex;

    // Estado de mouse para orbit/zoom
    bool dragging = false;
    bool panning = false;
    POINT lastMouse = { 0, 0 };
};

class MainWindow
{
public:
    bool Create(HINSTANCE hInstance);
    int RunMessageLoop();
    void OpenFiles(const std::vector<std::wstring>& paths); // p/ linha de comando

private:
    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ViewportProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TabSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR refData);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT ViewportProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate(HWND hwnd);
    void OnCommand(WPARAM wParam);
    void OnSize();
    void OnTabChanged();
    void OnDropFiles(HDROP hDrop);

    void OpenFileDialog();
    void OpenFile(const std::wstring& path);
    void CloseTab(int index);
    void LayoutChildren();
    void RenderActiveTab();
    void FrameCameraToModel(TabDocument& doc);
    void CreateSidebar(HWND parent);
    void UpdateSidebar(); // atualiza valores das estatisticas e estado dos toggles
    void ApplyFont(HWND ctrl, HFONT font);

    HWND m_hwnd = nullptr;
    HWND m_tabControl = nullptr;
    HWND m_viewportContainer = nullptr; // janela "canvas" onde o D3D desenha
    HACCEL m_accelTable = nullptr;

    // Sidebar (estatisticas + toggles de exibicao)
    HWND m_sbTitle = nullptr;
    HWND m_sbStatLabels[5] = {};  // Triangulos, Vertices, Edges, Malhas, Materiais
    HWND m_sbStatValues[5] = {};
    HWND m_sbDisplayTitle = nullptr;
    HWND m_btnMaterial = nullptr;
    HWND m_btnWireframe = nullptr;
    HWND m_btnShadows = nullptr;
    HWND m_btnReframe = nullptr;
    HWND m_sbHint = nullptr;
    HFONT m_uiFont = nullptr;
    HFONT m_uiFontBold = nullptr;

    Renderer m_renderer;
    std::vector<std::unique_ptr<TabDocument>> m_documents;
    int m_activeTab = -1;

    HINSTANCE m_hInstance = nullptr;
};
