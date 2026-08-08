#pragma once
#include "SceneData.h" // inclui windows.h
#include <vector>
#include <memory>
#include <string>
#include "Renderer.h"

// Nome da classe da janela principal — usado tambem pela logica de
// single-instance no main.cpp (FindWindow) para localizar a instancia original.
inline constexpr wchar_t kMainWindowClassName[] = L"FbxViewerMainWnd";

// Assinatura do WM_COPYDATA usado para enviar um caminho de arquivo de uma
// segunda instancia para a instancia original ("FBX1" em ASCII).
inline constexpr ULONG_PTR kCopyDataOpenFile = 0x46425831;

// Argumento de linha de comando que abre uma janela nova em vez de mandar o
// arquivo para a instancia que ja esta rodando.
inline constexpr wchar_t kNewWindowSwitch[] = L"--new-window";

// Mensagem enviada pelo controle de rotacao de luz ao pai:
// wParam = novo angulo em graus (0-359).
inline constexpr UINT WM_APP_LIGHTDIAL = WM_APP + 1;

// Modos de visualizacao do viewport (a segunda faixa de abas).
enum class ViewMode
{
    Model3D = 0,
    UvMap = 1,
};

// O que o editor de cor da barra lateral esta editando no momento.
enum class LightTarget
{
    MainLight = 0,
    Aux1,
    Aux2,
    Aux3,
    Ambient,
    Background,
    Ground,
    Count
};

// Uma fonte de luz (ou o chao / o fundo) do jeito que o usuario a edita.
// Para o chao, "intensity" e a opacidade.
struct EditableLight
{
    XMFLOAT3 color = XMFLOAT3(1, 1, 1);
    float intensity = 1.0f;
    bool enabled = true;
};

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

    // Estado da aba de UV deste documento
    UvViewState uvView;
    bool hasUvs = false; // false = modelo sem coordenadas de textura

    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11Texture2D> depthTex;

    // Estado de mouse para orbit/zoom
    bool dragging = false;
    bool panning = false;
    POINT lastMouse = { 0, 0 };
};

// Tema de iluminacao (preset): carrega de uma vez o plano de fundo, a luz
// ambiente, a luz principal, as cores das 3 auxiliares e a cor do chao. Depois
// de aplicado, tudo continua editavel na barra lateral.
struct LightingTheme
{
    const wchar_t* name;
    XMFLOAT3 background;
    XMFLOAT3 ambientColor;
    float ambientIntensity;
    XMFLOAT3 mainLightColor;
    XMFLOAT3 auxColors[3];
    XMFLOAT3 groundColor;
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
    static LRESULT CALLBACK SidebarProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LightDialProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TabSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR refData);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT ViewportProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT SidebarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate(HWND hwnd);
    void OnCommand(WPARAM wParam);
    void OnDrawItem(LPARAM lParam);
    void OnHScroll(HWND control);
    void OnSize();
    void OnTabChanged();
    void OnViewTabChanged();
    void OnDropFiles(HDROP hDrop);

    std::vector<std::wstring> AskForFiles();
    void OpenFileDialog();
    void OpenInNewWindow();
    void OpenFile(const std::wstring& path);
    void CloseTab(int index);
    void SelectTab(int index);
    void CycleTab(int delta); // Ctrl+Tab / Ctrl+Shift+Tab
    void SetViewMode(ViewMode mode);
    void LayoutChildren();
    int LayoutSidebar(int panelWidth); // devolve a altura total do conteudo
    void ScrollSidebarTo(int position);
    void RenderActiveTab();
    void FrameCameraToModel(TabDocument& doc);
    void CreateSidebar(HWND parent);
    void UpdateSidebar();          // valores das estatisticas e estado dos toggles
    void UpdateSidebarVisibility(); // mostra a secao 3D ou a secao de UV
    void UpdateMaterialCombo();
    void UpdateWindowTitle();
    void ApplyFont(HWND ctrl, HFONT font);
    LightingState BuildLightingState() const;
    TabDocument* ActiveDocument();

    // ---- Editor de luz / cor ----
    void ApplyTheme(int themeIndex);
    EditableLight* CurrentTarget();          // nullptr se o alvo nao tem luz
    XMFLOAT3* CurrentTargetColor();
    bool TargetHasIntensity(LightTarget target) const;
    bool TargetHasEnabled(LightTarget target) const;
    void LoadLightEditor();                  // estado -> controles
    void PushColorToState(const XMFLOAT3& color);
    void OnChannelChanged(int channel, bool fromSlider);
    void OnIntensityChanged(bool fromSlider);
    void OnHexChanged();
    void PickColorFromDialog();

    // ---- Arquivos recentes ----
    void RebuildRecentMenu();
    void OpenRecentFile(int index);

    // ---- Exportacao de imagem ----
    void ExportImage();

    HWND m_hwnd = nullptr;
    HWND m_tabControl = nullptr;
    HWND m_viewTabs = nullptr;          // "Modelo 3D" / "Mapa UV e textura"
    HWND m_viewportContainer = nullptr; // janela "canvas" onde o D3D desenha
    HWND m_sidebarPanel = nullptr;      // painel rolavel que hospeda a barra lateral
    HACCEL m_accelTable = nullptr;
    HMENU m_recentMenu = nullptr;

    // Sidebar — estatisticas
    HWND m_sbTitle = nullptr;
    HWND m_sbStatLabels[6] = {};  // Triangulos, Vertices, Edges, Malhas, Materiais, Draw calls
    HWND m_sbStatValues[6] = {};

    // Sidebar — exibicao
    HWND m_sbDisplayTitle = nullptr;
    HWND m_btnMaterial = nullptr;
    HWND m_btnWireframe = nullptr;
    HWND m_btnShadows = nullptr;
    HWND m_btnReframe = nullptr;
    HWND m_btnGrid = nullptr;

    // Sidebar — ambiente e iluminacao
    HWND m_sbLightTitle = nullptr;
    HWND m_themeButtons[6] = {};
    HWND m_lightSelect = nullptr;      // qual luz esta sendo editada
    HWND m_lightEnabled = nullptr;     // checkbox "Ativa" / "Mostrar chao"
    HWND m_colorModel = nullptr;       // HSV / RGB
    HWND m_colorSwatch = nullptr;      // amostra clicavel (abre o seletor do Windows)
    HWND m_hexEdit = nullptr;
    HWND m_channelLabels[3] = {};
    HWND m_channelSliders[3] = {};
    HWND m_channelEdits[3] = {};
    HWND m_intensityLabel = nullptr;
    HWND m_intensitySlider = nullptr;
    HWND m_intensityEdit = nullptr;
    HWND m_sbRotLabel = nullptr;
    HWND m_lightDial = nullptr;

    // Sidebar — aba de UV
    HWND m_sbUvTitle = nullptr;
    HWND m_sbUvMaterialLabel = nullptr;
    HWND m_uvMaterialCombo = nullptr;
    HWND m_sbUvTextureInfo = nullptr;
    HWND m_btnUvReset = nullptr;
    HWND m_sbUvHint = nullptr;

    HWND m_sbHint = nullptr;
    HFONT m_uiFont = nullptr;
    HFONT m_uiFontBold = nullptr;
    HBRUSH m_whiteBrush = nullptr;

    // Rolagem da barra lateral
    int m_sidebarScroll = 0;
    int m_sidebarContentHeight = 0;

    // Estado global de iluminacao (compartilhado por todas as abas,
    // como no visualizador 3D nativo)
    int m_themeIndex = 0;
    float m_lightRotationDeg = 45.0f;
    EditableLight m_mainLight;
    EditableLight m_ambient;
    // Auxiliares e chao comecam desligados: o visual padrao continua sendo o
    // de antes (uma luz principal e a sombra projetada sobre o fundo).
    EditableLight m_auxLights[3] = {
        { XMFLOAT3(1, 1, 1), 1.0f, false },
        { XMFLOAT3(1, 1, 1), 1.0f, false },
        { XMFLOAT3(1, 1, 1), 1.0f, false },
    };
    EditableLight m_ground = { XMFLOAT3(0.55f, 0.55f, 0.58f), 0.85f, false };
    XMFLOAT3 m_backgroundColor = XMFLOAT3(0.18f, 0.18f, 0.20f);
    bool m_showGrid = false;

    LightTarget m_lightTarget = LightTarget::MainLight;
    bool m_colorModeHsv = true;
    // Enquanto a UI se atualiza sozinha, ignoramos as notificacoes que ela
    // mesma dispara — senao um slider reescreve o campo que o acabou de mudar.
    bool m_suppressLightUi = false;

    ViewMode m_viewMode = ViewMode::Model3D;

    // Ultimo tamanho aplicado ao viewport: evita recriar a swap chain quando
    // o layout roda sem que as dimensoes tenham mudado.
    int m_lastViewportW = 0;
    int m_lastViewportH = 0;

    Renderer m_renderer;
    std::vector<std::unique_ptr<TabDocument>> m_documents;
    std::vector<std::wstring> m_recentFiles;
    int m_activeTab = -1;

    HINSTANCE m_hInstance = nullptr;
};
