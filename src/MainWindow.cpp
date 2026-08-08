#include "MainWindow.h"
#include "AppSettings.h"
#include "ExportImageDialog.h"
#include "FileAssociation.h"
#include "ModelLoader.h"
#include "resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <string>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")

namespace
{
    constexpr int ID_TAB_CONTROL = 1001;
    constexpr int ID_VIEW_TABS = 1002;
    constexpr int ID_VIEWPORT = 1003;
    constexpr int ID_SIDEBAR = 1004;

    constexpr int ID_BTN_MATERIAL = 1201;
    constexpr int ID_BTN_WIREFRAME = 1202;
    constexpr int ID_BTN_SHADOWS = 1203;
    constexpr int ID_BTN_REFRAME = 1204;
    constexpr int ID_BTN_UV_RESET = 1205;
    constexpr int ID_UV_MATERIAL_COMBO = 1206;
    constexpr int ID_BTN_GRID = 1207;

    constexpr int ID_LIGHT_SELECT = 1210;
    constexpr int ID_LIGHT_ENABLED = 1211;
    constexpr int ID_COLOR_MODEL = 1212;
    constexpr int ID_COLOR_SWATCH = 1213;
    constexpr int ID_HEX_EDIT = 1214;
    constexpr int ID_CHANNEL_EDIT_BASE = 1215; // 1215..1217
    constexpr int ID_INTENSITY_EDIT = 1218;

    constexpr int ID_THEME_BASE = 1301; // 1301..1306
    constexpr int ID_AUX_BASE = 1401;   // reservado (nao usado desde o editor de luz)

    constexpr int IDM_FILE_OPEN = 2001;
    constexpr int IDM_FILE_OPEN_NEW_TAB = 2002;
    constexpr int IDM_FILE_CLOSE_TAB = 2003;
    constexpr int IDM_FILE_EXIT = 2004;
    constexpr int IDM_FILE_NEW_WINDOW = 2005;
    constexpr int IDM_FILE_EXPORT_IMAGE = 2006;
    constexpr int IDM_VIEW_3D = 2101;
    constexpr int IDM_VIEW_UV = 2102;
    constexpr int IDM_VIEW_NEXT_TAB = 2103;
    constexpr int IDM_VIEW_PREV_TAB = 2104;
    constexpr int IDM_VIEW_GRID = 2105;
    constexpr int IDM_TOOLS_REGISTER = 2201;
    constexpr int IDM_TOOLS_DEFAULT_APPS = 2202;
    constexpr int IDM_RECENT_BASE = 2500;  // 2500 .. 2500+kMaxRecentFiles-1
    constexpr int IDM_RECENT_CLEAR = 2599;

    const wchar_t* kViewportWndClass = L"FbxViewerViewportWnd";
    const wchar_t* kSidebarWndClass = L"FbxViewerSidebarWnd";
    const wchar_t* kLightDialClass = L"FbxViewerLightDial";
    const wchar_t* kAppTitle = L"Visualizador 3D";

    constexpr int TOP_MARGIN = 30;   // altura da faixa de abas de arquivo
    constexpr int VIEW_TAB_H = 26;   // altura da faixa "Modelo 3D / Mapa UV"
    constexpr int SIDEBAR_W = 290; // inclui a largura da barra de rolagem
    constexpr int TAB_CLOSE_ZONE = 22;
    constexpr int THEME_COUNT = 6;

    // Tamanho da janela na primeira execucao (depois vale o que o usuario
    // deixou na sessao anterior).
    constexpr int DEFAULT_WINDOW_W = 1280;
    constexpr int DEFAULT_WINDOW_H = 860;

    // ------------------------------------------------------------------
    // Presets de iluminacao (inspirados nas miniaturas do viewer nativo).
    // Sao pontos de partida: tudo continua editavel depois de aplicado.
    // ------------------------------------------------------------------
    const LightingTheme kThemes[THEME_COUNT] = {
        { L"Neutro",
          XMFLOAT3(0.18f, 0.18f, 0.20f), XMFLOAT3(1, 1, 1), 0.22f,
          XMFLOAT3(1, 1, 1),
          { XMFLOAT3(0.75f, 0.82f, 1.0f), XMFLOAT3(1.0f, 0.88f, 0.75f), XMFLOAT3(0.85f, 1.0f, 0.88f) },
          XMFLOAT3(0.55f, 0.55f, 0.58f) },
        { L"Estúdio claro",
          XMFLOAT3(0.90f, 0.90f, 0.93f), XMFLOAT3(1, 1, 1), 0.38f,
          XMFLOAT3(1, 1, 1),
          { XMFLOAT3(0.85f, 0.9f, 1.0f), XMFLOAT3(1.0f, 0.95f, 0.85f), XMFLOAT3(0.9f, 0.9f, 0.9f) },
          XMFLOAT3(0.82f, 0.82f, 0.85f) },
        { L"Entardecer",
          XMFLOAT3(0.33f, 0.20f, 0.16f), XMFLOAT3(1.0f, 0.80f, 0.60f), 0.26f,
          XMFLOAT3(1.0f, 0.72f, 0.45f),
          { XMFLOAT3(1.0f, 0.6f, 0.4f), XMFLOAT3(0.6f, 0.5f, 0.8f), XMFLOAT3(1.0f, 0.85f, 0.6f) },
          XMFLOAT3(0.45f, 0.32f, 0.26f) },
        { L"Noite",
          XMFLOAT3(0.04f, 0.06f, 0.11f), XMFLOAT3(0.55f, 0.65f, 1.0f), 0.16f,
          XMFLOAT3(0.72f, 0.80f, 1.0f),
          { XMFLOAT3(0.4f, 0.55f, 1.0f), XMFLOAT3(0.7f, 0.75f, 1.0f), XMFLOAT3(0.5f, 0.9f, 1.0f) },
          XMFLOAT3(0.16f, 0.20f, 0.30f) },
        { L"Esverdeado",
          XMFLOAT3(0.14f, 0.22f, 0.20f), XMFLOAT3(0.75f, 1.0f, 0.90f), 0.26f,
          XMFLOAT3(0.88f, 1.0f, 0.94f),
          { XMFLOAT3(0.6f, 1.0f, 0.8f), XMFLOAT3(1.0f, 0.95f, 0.7f), XMFLOAT3(0.7f, 0.9f, 1.0f) },
          XMFLOAT3(0.30f, 0.42f, 0.36f) },
        { L"Dramático",
          XMFLOAT3(0.02f, 0.02f, 0.03f), XMFLOAT3(1, 1, 1), 0.07f,
          XMFLOAT3(1.15f, 1.12f, 1.05f),
          { XMFLOAT3(0.9f, 0.3f, 0.25f), XMFLOAT3(0.25f, 0.45f, 0.95f), XMFLOAT3(1.0f, 1.0f, 1.0f) },
          XMFLOAT3(0.10f, 0.10f, 0.12f) },
    };

    // Direcoes fixas das luzes auxiliares (direcao em que a luz viaja):
    // 1 = da esquerda, 2 = da direita, 3 = de tras/baixo (contorno)
    const XMFLOAT3 kAuxDirs[3] = {
        XMFLOAT3( 0.9f, -0.25f,  0.35f),
        XMFLOAT3(-0.9f, -0.25f,  0.35f),
        XMFLOAT3( 0.0f,  0.35f, -1.0f),
    };

    const wchar_t* kLightTargetNames[(int)LightTarget::Count] = {
        L"Luz principal",
        L"Luz 1 (esquerda)",
        L"Luz 2 (direita)",
        L"Luz 3 (contorno)",
        L"Luz ambiente",
        L"Plano de fundo",
        L"Chão",
    };

    COLORREF ToColorRef(const XMFLOAT3& c)
    {
        auto clamp255 = [](float v) { return (BYTE)std::clamp((int)(v * 255.0f + 0.5f), 0, 255); };
        return RGB(clamp255(c.x), clamp255(c.y), clamp255(c.z));
    }

    XMFLOAT3 FromColorRef(COLORREF color)
    {
        return XMFLOAT3(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f);
    }

    // ---- Conversoes HSV <-> RGB (H em graus, S e V em 0..1) ----
    void RgbToHsv(const XMFLOAT3& rgb, float& h, float& s, float& v)
    {
        const float r = std::clamp(rgb.x, 0.0f, 1.0f);
        const float g = std::clamp(rgb.y, 0.0f, 1.0f);
        const float b = std::clamp(rgb.z, 0.0f, 1.0f);
        const float maxC = std::max({ r, g, b });
        const float minC = std::min({ r, g, b });
        const float delta = maxC - minC;

        v = maxC;
        s = (maxC <= 0.0f) ? 0.0f : delta / maxC;

        if (delta <= 1e-6f)      h = 0.0f;
        else if (maxC == r)      h = 60.0f * fmodf((g - b) / delta + 6.0f, 6.0f);
        else if (maxC == g)      h = 60.0f * ((b - r) / delta + 2.0f);
        else                     h = 60.0f * ((r - g) / delta + 4.0f);
    }

    XMFLOAT3 HsvToRgb(float h, float s, float v)
    {
        h = fmodf(fmodf(h, 360.0f) + 360.0f, 360.0f);
        s = std::clamp(s, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        const float c = v * s;
        const float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
        const float m = v - c;

        float r = 0, g = 0, b = 0;
        if (h < 60)       { r = c; g = x; }
        else if (h < 120) { r = x; g = c; }
        else if (h < 180) { g = c; b = x; }
        else if (h < 240) { g = x; b = c; }
        else if (h < 300) { r = x; b = c; }
        else              { r = c; b = x; }
        return XMFLOAT3(r + m, g + m, b + m);
    }

    // Formata numero com separador de milhar pt-BR (51731 -> "51.731")
    std::wstring FormatThousands(UINT value)
    {
        std::wstring raw = std::to_wstring(value);
        std::wstring out;
        int count = 0;
        for (int i = (int)raw.size() - 1; i >= 0; i--)
        {
            out.insert(out.begin(), raw[i]);
            if (++count % 3 == 0 && i > 0)
                out.insert(out.begin(), L'.');
        }
        return out;
    }

    std::wstring Utf8ToWideSimple(const std::string& s)
    {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (len <= 0) return L"";
        std::wstring out((size_t)len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
        return out;
    }

    int GetControlInt(HWND control, int fallback)
    {
        wchar_t buffer[32] = {};
        GetWindowTextW(control, buffer, ARRAYSIZE(buffer));
        if (buffer[0] == L'\0') return fallback;
        return _wtoi(buffer);
    }

    void SetControlInt(HWND control, int value)
    {
        wchar_t buffer[32];
        swprintf_s(buffer, L"%d", value);
        SetWindowTextW(control, buffer);
    }

    std::wstring FileNameOnly(const std::wstring& path)
    {
        size_t slash = path.find_last_of(L"\\/");
        return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    }
}

LightingState MainWindow::BuildLightingState() const
{
    LightingState ls;
    ls.background = m_backgroundColor;
    ls.ambientColor = m_ambient.color;
    ls.ambientIntensity = m_ambient.intensity;
    ls.mainLightColor = m_mainLight.color;
    ls.mainLightIntensity = m_mainLight.intensity;
    ls.rotationDeg = m_lightRotationDeg;
    ls.elevationDeg = 40.0f;
    for (int i = 0; i < 3; i++)
    {
        ls.aux[i].direction = kAuxDirs[i];
        ls.aux[i].color = m_auxLights[i].color;
        ls.aux[i].intensity = m_auxLights[i].intensity;
        ls.aux[i].enabled = m_auxLights[i].enabled;
    }
    ls.groundColor = m_ground.color;
    ls.groundOpacity = m_ground.enabled ? m_ground.intensity : 0.0f;
    ls.showGrid = m_showGrid;
    return ls;
}

TabDocument* MainWindow::ActiveDocument()
{
    if (m_activeTab < 0 || m_activeTab >= (int)m_documents.size()) return nullptr;
    return m_documents[m_activeTab].get();
}

bool MainWindow::Create(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icc = { sizeof(icc),
        ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    ApplyTheme(0); // valores iniciais das luzes

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_uiFont = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW boldLf = ncm.lfMessageFont;
    boldLf.lfWeight = FW_SEMIBOLD;
    boldLf.lfHeight = (LONG)(boldLf.lfHeight * 1.2f);
    m_uiFontBold = CreateFontIndirectW(&boldLf);
    m_whiteBrush = CreateSolidBrush(RGB(255, 255, 255));

    HICON appIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
        0, 0, LR_DEFAULTSIZE);
    HICON appIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    // CS_HREDRAW/CS_VREDRAW: sem isso, ao redimensionar a janela a faixa da
    // sidebar (que fica ancorada a direita) nao era repintada e os botoes
    // ficavam com restos do desenho anterior ate receberem o mouse.
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProcStatic;
    wc.hInstance = hInstance;
    wc.lpszClassName = kMainWindowClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = m_whiteBrush;
    wc.hIcon = appIcon ? appIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = appIconSmall ? appIconSmall : wc.hIcon;
    RegisterClassExW(&wc);

    WNDCLASSEXW vwc = {};
    vwc.cbSize = sizeof(vwc);
    vwc.style = CS_DBLCLKS; // p/ o duplo clique reenquadrar a aba de UV
    vwc.lpfnWndProc = ViewportProcStatic;
    vwc.hInstance = hInstance;
    vwc.lpszClassName = kViewportWndClass;
    vwc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    vwc.hbrBackground = nullptr;
    RegisterClassExW(&vwc);

    WNDCLASSEXW swc = {};
    swc.cbSize = sizeof(swc);
    swc.lpfnWndProc = SidebarProcStatic;
    swc.hInstance = hInstance;
    swc.lpszClassName = kSidebarWndClass;
    swc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    swc.hbrBackground = m_whiteBrush;
    RegisterClassExW(&swc);

    WNDCLASSEXW dwc = {};
    dwc.cbSize = sizeof(dwc);
    dwc.lpfnWndProc = LightDialProcStatic;
    dwc.hInstance = hInstance;
    dwc.lpszClassName = kLightDialClass;
    dwc.hCursor = LoadCursor(nullptr, IDC_HAND);
    dwc.hbrBackground = nullptr;
    RegisterClassExW(&dwc);

    HMENU menuBar = CreateMenu();

    m_recentMenu = CreatePopupMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN, L"&Abrir...\tCtrl+O");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN_NEW_TAB, L"Abrir em &nova aba...\tCtrl+T");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_NEW_WINDOW, L"Abrir em nova &janela...\tCtrl+N");
    AppendMenuW(fileMenu, MF_POPUP, (UINT_PTR)m_recentMenu, L"Abrir &recentes");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXPORT_IMAGE, L"&Exportar imagem...\tCtrl+E");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_CLOSE_TAB, L"&Fechar aba\tCtrl+W");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"Sai&r");
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)fileMenu, L"&Arquivo");

    HMENU viewMenu = CreatePopupMenu();
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_3D, L"&Modelo 3D\tCtrl+1");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_UV, L"Mapa &UV e textura\tCtrl+2");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_GRID, L"&Grade\tG");
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_NEXT_TAB, L"&Próxima aba\tCtrl+Tab");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_PREV_TAB, L"Aba &anterior\tCtrl+Shift+Tab");
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)viewMenu, L"E&xibir");

    HMENU toolsMenu = CreatePopupMenu();
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_REGISTER,
        L"&Registrar como visualizador de arquivos 3D");
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_DEFAULT_APPS,
        L"Abrir &Aplicativos padrão do Windows...");
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)toolsMenu, L"&Ferramentas");

    // Posicao/tamanho da sessao anterior; na primeira execucao usa o padrao.
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    int width = DEFAULT_WINDOW_W, height = DEFAULT_WINDOW_H;
    bool maximized = false;
    const bool restored = appsettings::LoadWindowPlacement(x, y, width, height, maximized);

    m_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        kMainWindowClassName, kAppTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, width, height,
        nullptr, menuBar, hInstance, this);

    if (!m_hwnd) return false;

    RebuildRecentMenu();

    ACCEL accels[] = {
        { FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN },
        { FVIRTKEY | FCONTROL, 'T', IDM_FILE_OPEN_NEW_TAB },
        { FVIRTKEY | FCONTROL, 'N', IDM_FILE_NEW_WINDOW },
        { FVIRTKEY | FCONTROL, 'E', IDM_FILE_EXPORT_IMAGE },
        { FVIRTKEY | FCONTROL, 'W', IDM_FILE_CLOSE_TAB },
        { FVIRTKEY | FCONTROL, '1', IDM_VIEW_3D },
        { FVIRTKEY | FCONTROL, '2', IDM_VIEW_UV },
        { FVIRTKEY | FCONTROL, VK_TAB, IDM_VIEW_NEXT_TAB },
        { FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, IDM_VIEW_PREV_TAB },
        { FVIRTKEY | FCONTROL, VK_NEXT, IDM_VIEW_NEXT_TAB },  // Ctrl+PageDown
        { FVIRTKEY | FCONTROL | FSHIFT, VK_PRIOR, IDM_VIEW_PREV_TAB },
        { FVIRTKEY | FCONTROL, VK_PRIOR, IDM_VIEW_PREV_TAB }, // Ctrl+PageUp
    };
    m_accelTable = CreateAcceleratorTableW(accels, ARRAYSIZE(accels));

    if (!m_renderer.InitDevice())
    {
        MessageBoxW(m_hwnd, L"Falha ao inicializar o DirectX 11. Verifique se sua GPU/driver suporta D3D11.",
            L"Erro", MB_ICONERROR);
        return false;
    }

    ShowWindow(m_hwnd, (restored && maximized) ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);
    return true;
}

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    else
    {
        self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self) return self->WndProc(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;
    case WM_COMMAND:
        OnCommand(wParam);
        return 0;
    case WM_DRAWITEM:
        OnDrawItem(lParam);
        return TRUE;
    case WM_HSCROLL:
        OnHScroll((HWND)lParam);
        return 0;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)m_whiteBrush;
    }
    case WM_APP_LIGHTDIAL:
    {
        m_lightRotationDeg = (float)(int)wParam;
        RenderActiveTab();
        return 0;
    }
    case WM_NOTIFY:
    {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr->idFrom == ID_TAB_CONTROL && hdr->code == TCN_SELCHANGE)
            OnTabChanged();
        else if (hdr->idFrom == ID_VIEW_TABS && hdr->code == TCN_SELCHANGE)
            OnViewTabChanged();
        return 0;
    }
    case WM_SIZE:
        OnSize();
        return 0;
    case WM_DROPFILES:
        OnDropFiles((HDROP)wParam);
        return 0;
    case WM_COPYDATA:
    {
        COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
        if (cds && cds->dwData == kCopyDataOpenFile && cds->lpData && cds->cbData >= sizeof(wchar_t))
        {
            size_t charCount = cds->cbData / sizeof(wchar_t);
            std::wstring path((const wchar_t*)cds->lpData, charCount);
            while (!path.empty() && path.back() == L'\0')
                path.pop_back();

            if (IsIconic(hwnd))
                ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);

            if (!path.empty())
                OpenFile(path);
            return TRUE;
        }
        return FALSE;
    }
    case WM_CLOSE:
        // Guarda o tamanho/posicao antes de destruir a janela, para a proxima
        // sessao reabrir do mesmo jeito.
        appsettings::SaveWindowPlacement(hwnd);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Painel rolavel que hospeda a barra lateral. Os controles sao filhos dele, e
// as notificacoes que eles disparam sobem para a janela principal.
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWindow::SidebarProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    else
    {
        self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self) return self->SidebarProc(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::SidebarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    // Tudo que os controles notificam vai para a janela principal, que
    // concentra a logica.
    case WM_COMMAND:
    case WM_DRAWITEM:
    case WM_HSCROLL:
    case WM_APP_LIGHTDIAL:
        return SendMessageW(m_hwnd, msg, wParam, lParam);

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)m_whiteBrush;
    }

    case WM_VSCROLL:
    {
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);

        int position = si.nPos;
        switch (LOWORD(wParam))
        {
        case SB_LINEUP:       position -= 28; break;
        case SB_LINEDOWN:     position += 28; break;
        case SB_PAGEUP:       position -= (int)si.nPage; break;
        case SB_PAGEDOWN:     position += (int)si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: position = si.nTrackPos; break;
        case SB_TOP:          position = 0; break;
        case SB_BOTTOM:       position = si.nMax; break;
        default: return 0;
        }
        ScrollSidebarTo(position);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        ScrollSidebarTo(m_sidebarScroll - (delta * 70) / WHEEL_DELTA);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::ScrollSidebarTo(int position)
{
    if (!m_sidebarPanel) return;

    RECT rc;
    GetClientRect(m_sidebarPanel, &rc);
    const int visible = rc.bottom - rc.top;
    const int maxScroll = std::max(0, m_sidebarContentHeight - visible);

    position = std::clamp(position, 0, maxScroll);
    if (position == m_sidebarScroll) return;
    m_sidebarScroll = position;

    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_POS;
    si.nPos = m_sidebarScroll;
    SetScrollInfo(m_sidebarPanel, SB_VERT, &si, TRUE);

    // Reposicionar os filhos (em vez de usar ScrollWindowEx) mantem os botoes
    // owner-draw sempre coerentes com a rolagem.
    LayoutSidebar(rc.right - rc.left);
    RedrawWindow(m_sidebarPanel, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// Subclass do tab control: intercepta clique no "X" de cada aba
LRESULT CALLBACK MainWindow::TabSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*subclassId*/, DWORD_PTR refData)
{
    MainWindow* self = (MainWindow*)refData;

    if (msg == WM_LBUTTONDOWN && self)
    {
        TCHITTESTINFO ht = {};
        ht.pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int idx = TabCtrl_HitTest(hwnd, &ht);
        if (idx >= 0)
        {
            RECT r;
            TabCtrl_GetItemRect(hwnd, idx, &r);
            if (ht.pt.x >= r.right - TAB_CLOSE_ZONE)
            {
                self->CloseTab(idx);
                return 0;
            }
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Controle de rotacao de luz: anel com "sol" arrastavel (estilo viewer nativo)
// Estado (angulo em graus) fica no GWLP_USERDATA do proprio controle.
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWindow::LightDialProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto notifyParent = [hwnd]()
    {
        int angle = (int)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        SendMessageW(GetParent(hwnd), WM_APP_LIGHTDIAL, (WPARAM)angle, 0);
    };

    auto angleFromPoint = [hwnd](int x, int y) -> int
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        float cx = (rc.right - rc.left) * 0.5f;
        float cy = (rc.bottom - rc.top) * 0.5f;
        // 0 grau = topo, sentido horario (combina com o azimute da luz)
        float ang = atan2f((float)x - cx, cy - (float)y) * 180.0f / 3.14159265f;
        int deg = (int)lroundf(ang);
        if (deg < 0) deg += 360;
        return deg % 360;
    };

    switch (msg)
    {
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, angleFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        InvalidateRect(hwnd, nullptr, FALSE);
        notifyParent();
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == hwnd)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, angleFromPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
            InvalidateRect(hwnd, nullptr, FALSE);
            notifyParent();
        }
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;

        // Double-buffer simples p/ nao piscar durante o arrasto
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

        RECT full = { 0, 0, w, h };
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(mem, &full, white);
        DeleteObject(white);

        int cx = w / 2, cy = h / 2;
        int ringR = (std::min(w, h) / 2) - 12;

        HPEN ringPen = CreatePen(PS_SOLID, 3, RGB(0, 103, 192));
        HGDIOBJ oldPen = SelectObject(mem, ringPen);
        SelectObject(mem, GetStockObject(NULL_BRUSH));
        Ellipse(mem, cx - ringR, cy - ringR, cx + ringR, cy + ringR);

        int innerR = ringR - 14;
        HBRUSH grayBrush = CreateSolidBrush(RGB(205, 205, 208));
        HPEN nullPen = CreatePen(PS_NULL, 0, 0);
        SelectObject(mem, grayBrush);
        SelectObject(mem, nullPen);
        Ellipse(mem, cx - innerR, cy - innerR, cx + innerR, cy + innerR);

        int angle = (int)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        float rad = (float)angle * 3.14159265f / 180.0f;
        int sx = cx + (int)lroundf(sinf(rad) * ringR);
        int sy = cy - (int)lroundf(cosf(rad) * ringR);
        int sunR = 8;
        HBRUSH sunBrush = CreateSolidBrush(RGB(255, 185, 0));
        HPEN sunPen = CreatePen(PS_SOLID, 2, RGB(0, 103, 192));
        SelectObject(mem, sunBrush);
        SelectObject(mem, sunPen);
        Ellipse(mem, sx - sunR, sy - sunR, sx + sunR, sy + sunR);

        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);

        SelectObject(mem, oldPen);
        SelectObject(mem, oldBmp);
        DeleteObject(ringPen);
        DeleteObject(nullPen);
        DeleteObject(sunPen);
        DeleteObject(grayBrush);
        DeleteObject(sunBrush);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::ApplyFont(HWND ctrl, HFONT font)
{
    if (ctrl && font)
        SendMessageW(ctrl, WM_SETFONT, (WPARAM)font, TRUE);
}

void MainWindow::OnCreate(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    m_tabControl = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | TCS_FOCUSNEVER,
        0, 0, rc.right - rc.left, TOP_MARGIN,
        hwnd, (HMENU)(INT_PTR)ID_TAB_CONTROL, m_hInstance, nullptr);
    ApplyFont(m_tabControl, m_uiFont);
    SetWindowSubclass(m_tabControl, TabSubclassProc, 1, (DWORD_PTR)this);

    // Segunda faixa de abas: alterna entre a cena 3D e o mapa de UV.
    m_viewTabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | TCS_FOCUSNEVER,
        0, TOP_MARGIN, rc.right - rc.left - SIDEBAR_W, VIEW_TAB_H,
        hwnd, (HMENU)(INT_PTR)ID_VIEW_TABS, m_hInstance, nullptr);
    ApplyFont(m_viewTabs, m_uiFont);
    {
        const wchar_t* names[2] = { L"Modelo 3D", L"Mapa UV e textura" };
        for (int i = 0; i < 2; i++)
        {
            TCITEMW item = {};
            item.mask = TCIF_TEXT;
            item.pszText = (LPWSTR)names[i];
            TabCtrl_InsertItem(m_viewTabs, i, &item);
        }
        TabCtrl_SetCurSel(m_viewTabs, 0);
    }

    m_viewportContainer = CreateWindowExW(0, kViewportWndClass, L"",
        WS_CHILD | WS_VISIBLE,
        0, TOP_MARGIN + VIEW_TAB_H,
        rc.right - rc.left - SIDEBAR_W,
        rc.bottom - rc.top - TOP_MARGIN - VIEW_TAB_H,
        hwnd, (HMENU)(INT_PTR)ID_VIEWPORT, m_hInstance, this);

    // Painel rolavel da barra lateral
    m_sidebarPanel = CreateWindowExW(0, kSidebarWndClass, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
        rc.right - rc.left - SIDEBAR_W, 0, SIDEBAR_W, rc.bottom - rc.top,
        hwnd, (HMENU)(INT_PTR)ID_SIDEBAR, m_hInstance, this);

    CreateSidebar(m_sidebarPanel);
    UpdateSidebar(); // ja cuida de visibilidade e layout
    DragAcceptFiles(hwnd, TRUE);
}

void MainWindow::CreateSidebar(HWND parent)
{
    const wchar_t* statNames[6] = {
        L"Triângulos", L"Vértices", L"Edges", L"Malhas", L"Materiais", L"Draw calls" };

    m_sbTitle = CreateWindowExW(0, L"STATIC", L"Estatísticas",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbTitle, m_uiFontBold);

    for (int i = 0; i < 6; i++)
    {
        m_sbStatLabels[i] = CreateWindowExW(0, L"STATIC", statNames[i],
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
        m_sbStatValues[i] = CreateWindowExW(0, L"STATIC", L"—",
            WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
        ApplyFont(m_sbStatLabels[i], m_uiFont);
        ApplyFont(m_sbStatValues[i], m_uiFont);
    }

    m_sbDisplayTitle = CreateWindowExW(0, L"STATIC", L"Exibição",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbDisplayTitle, m_uiFontBold);

    m_btnMaterial = CreateWindowExW(0, L"BUTTON", L"Material",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_MATERIAL, m_hInstance, nullptr);
    m_btnWireframe = CreateWindowExW(0, L"BUTTON", L"Wireframe",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_WIREFRAME, m_hInstance, nullptr);
    m_btnShadows = CreateWindowExW(0, L"BUTTON", L"Sombras",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_SHADOWS, m_hInstance, nullptr);
    m_btnGrid = CreateWindowExW(0, L"BUTTON", L"Grade",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_GRID, m_hInstance, nullptr);
    m_btnReframe = CreateWindowExW(0, L"BUTTON", L"Reenquadrar",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_REFRAME, m_hInstance, nullptr);
    ApplyFont(m_btnMaterial, m_uiFont);
    ApplyFont(m_btnWireframe, m_uiFont);
    ApplyFont(m_btnShadows, m_uiFont);
    ApplyFont(m_btnGrid, m_uiFont);
    ApplyFont(m_btnReframe, m_uiFont);

    // ---- Ambiente e iluminacao ----
    m_sbLightTitle = CreateWindowExW(0, L"STATIC", L"Ambiente e Iluminação",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbLightTitle, m_uiFontBold);

    for (int i = 0; i < THEME_COUNT; i++)
    {
        m_themeButtons[i] = CreateWindowExW(0, L"BUTTON", kThemes[i].name,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, parent, (HMENU)(INT_PTR)(ID_THEME_BASE + i), m_hInstance, nullptr);
    }

    m_lightSelect = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_LIGHT_SELECT, m_hInstance, nullptr);
    ApplyFont(m_lightSelect, m_uiFont);
    for (int i = 0; i < (int)LightTarget::Count; i++)
        SendMessageW(m_lightSelect, CB_ADDSTRING, 0, (LPARAM)kLightTargetNames[i]);
    SendMessageW(m_lightSelect, CB_SETCURSEL, 0, 0);

    m_lightEnabled = CreateWindowExW(0, L"BUTTON", L"Ativa",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_LIGHT_ENABLED, m_hInstance, nullptr);
    ApplyFont(m_lightEnabled, m_uiFont);

    m_colorModel = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_COLOR_MODEL, m_hInstance, nullptr);
    ApplyFont(m_colorModel, m_uiFont);
    SendMessageW(m_colorModel, CB_ADDSTRING, 0, (LPARAM)L"HSV");
    SendMessageW(m_colorModel, CB_ADDSTRING, 0, (LPARAM)L"RGB");
    SendMessageW(m_colorModel, CB_SETCURSEL, 0, 0);

    m_hexEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"#FFFFFF",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_HEX_EDIT, m_hInstance, nullptr);
    ApplyFont(m_hexEdit, m_uiFont);

    m_colorSwatch = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_COLOR_SWATCH, m_hInstance, nullptr);

    for (int i = 0; i < 3; i++)
    {
        m_channelLabels[i] = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
        ApplyFont(m_channelLabels[i], m_uiFont);

        m_channelSliders[i] = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);

        m_channelEdits[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
            0, 0, 0, 0, parent, (HMENU)(INT_PTR)(ID_CHANNEL_EDIT_BASE + i), m_hInstance, nullptr);
        ApplyFont(m_channelEdits[i], m_uiFont);
    }

    m_intensityLabel = CreateWindowExW(0, L"STATIC", L"Intensidade",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_intensityLabel, m_uiFont);

    m_intensitySlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);

    m_intensityEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"100",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_INTENSITY_EDIT, m_hInstance, nullptr);
    ApplyFont(m_intensityEdit, m_uiFont);

    m_sbRotLabel = CreateWindowExW(0, L"STATIC", L"Rotação da luz principal",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbRotLabel, m_uiFont);

    m_lightDial = CreateWindowExW(0, kLightDialClass, L"",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    SetWindowLongPtrW(m_lightDial, GWLP_USERDATA, (LONG_PTR)(int)m_lightRotationDeg);

    // ---- Aba de UV ----
    m_sbUvTitle = CreateWindowExW(0, L"STATIC", L"Mapa UV e textura",
        WS_CHILD | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbUvTitle, m_uiFontBold);

    m_sbUvMaterialLabel = CreateWindowExW(0, L"STATIC", L"Material",
        WS_CHILD | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbUvMaterialLabel, m_uiFont);

    m_uvMaterialCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_UV_MATERIAL_COMBO, m_hInstance, nullptr);
    ApplyFont(m_uvMaterialCombo, m_uiFont);

    m_sbUvTextureInfo = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbUvTextureInfo, m_uiFont);

    m_btnUvReset = CreateWindowExW(0, L"BUTTON", L"Reenquadrar UV",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_UV_RESET, m_hInstance, nullptr);
    ApplyFont(m_btnUvReset, m_uiFont);

    m_sbUvHint = CreateWindowExW(0, L"STATIC",
        L"Arraste para mover.\nRoda do mouse para o zoom.\n"
        L"Tecla F reenquadra.\nVerde = material selecionado.",
        WS_CHILD | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbUvHint, m_uiFont);

    m_sbHint = CreateWindowExW(0, L"STATIC",
        L"Abra um arquivo .fbx, .obj, .ply,\n.glb/.gltf, .dae, .3ds ou .dxf\n"
        L"(Arquivo > Abrir, ou arraste\ne solte na janela).",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbHint, m_uiFont);

    LoadLightEditor();
}

// Desenho customizado dos botoes de tema e da amostra de cor.
void MainWindow::OnDrawItem(LPARAM lParam)
{
    DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;

    if ((int)dis->CtlID == ID_COLOR_SWATCH)
    {
        const XMFLOAT3* color = CurrentTargetColor();
        RECT r = dis->rcItem;
        HBRUSH fill = CreateSolidBrush(color ? ToColorRef(*color) : RGB(200, 200, 200));
        FillRect(dis->hDC, &r, fill);
        DeleteObject(fill);

        HPEN border = CreatePen(PS_SOLID, 1, RGB(120, 120, 125));
        HGDIOBJ oldPen = SelectObject(dis->hDC, border);
        SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, r.left, r.top, r.right, r.bottom);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(border);
        return;
    }

    int themeIdx = (int)dis->CtlID - ID_THEME_BASE;
    if (themeIdx < 0 || themeIdx >= THEME_COUNT) return;

    const LightingTheme& t = kThemes[themeIdx];
    RECT r = dis->rcItem;

    // Fundo = cor de fundo do tema
    HBRUSH bg = CreateSolidBrush(ToColorRef(t.background));
    FillRect(dis->hDC, &r, bg);
    DeleteObject(bg);

    // Bolinha = cor da luz principal (canto superior direito)
    int cw = r.right - r.left;
    int sunR = std::max(5, cw / 8);
    int sx = r.right - sunR * 2 - 5;
    int sy = r.top + 5;
    HBRUSH sun = CreateSolidBrush(ToColorRef(t.mainLightColor));
    HGDIOBJ oldBrush = SelectObject(dis->hDC, sun);
    HGDIOBJ oldPen = SelectObject(dis->hDC, GetStockObject(NULL_PEN));
    Ellipse(dis->hDC, sx, sy, sx + sunR * 2, sy + sunR * 2);
    SelectObject(dis->hDC, oldBrush);
    SelectObject(dis->hDC, oldPen);
    DeleteObject(sun);

    // Borda: azul grossa se selecionado, cinza fina caso contrario
    HPEN border = (themeIdx == m_themeIndex)
        ? CreatePen(PS_SOLID, 3, RGB(0, 103, 192))
        : CreatePen(PS_SOLID, 1, RGB(180, 180, 185));
    HGDIOBJ ob = SelectObject(dis->hDC, border);
    SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(dis->hDC, r.left, r.top, r.right, r.bottom);
    SelectObject(dis->hDC, ob);
    DeleteObject(border);
}

void MainWindow::OnSize()
{
    LayoutChildren();
}

void MainWindow::LayoutChildren()
{
    if (!m_hwnd) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    int viewportW = std::max(1, width - SIDEBAR_W);
    int viewportH = std::max(1, height - TOP_MARGIN - VIEW_TAB_H);

    HDWP dwp = BeginDeferWindowPos(4);
    auto place = [&dwp](HWND ctrl, int x, int y, int w, int h)
    {
        if (!ctrl || !dwp) return;
        dwp = DeferWindowPos(dwp, ctrl, nullptr, x, y, w, h,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    };
    place(m_tabControl, 0, 0, width, TOP_MARGIN);
    place(m_viewTabs, 0, TOP_MARGIN, viewportW, VIEW_TAB_H);
    place(m_viewportContainer, 0, TOP_MARGIN + VIEW_TAB_H, viewportW, viewportH);
    place(m_sidebarPanel, width - SIDEBAR_W, 0, SIDEBAR_W, height);
    if (dwp) EndDeferWindowPos(dwp);

    if (m_sidebarPanel)
    {
        RECT panelRc;
        GetClientRect(m_sidebarPanel, &panelRc);
        const int panelWidth = panelRc.right - panelRc.left;
        const int panelHeight = panelRc.bottom - panelRc.top;

        m_sidebarContentHeight = LayoutSidebar(panelWidth);

        // Se o conteudo encolheu, a rolagem pode ter ficado alem do fim.
        const int maxScroll = std::max(0, m_sidebarContentHeight - panelHeight);
        if (m_sidebarScroll > maxScroll)
        {
            m_sidebarScroll = maxScroll;
            LayoutSidebar(panelWidth);
        }

        SCROLLINFO si = { sizeof(si) };
        // SIF_DISABLENOSCROLL mantem a barra sempre visivel (desabilitada
        // quando nao ha o que rolar). Sem isso o Windows a esconde, a largura
        // util do painel muda, e o layout — que ja foi calculado — fica
        // cortado na borda direita.
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = std::max(0, m_sidebarContentHeight - 1);
        si.nPage = (UINT)std::max(1, panelHeight);
        si.nPos = m_sidebarScroll;
        SetScrollInfo(m_sidebarPanel, SB_VERT, &si, TRUE);

        RedrawWindow(m_sidebarPanel, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

    const bool viewportChanged = (viewportW != m_lastViewportW || viewportH != m_lastViewportH);
    m_lastViewportW = viewportW;
    m_lastViewportH = viewportH;

    if (TabDocument* doc = ActiveDocument())
    {
        if (doc->swapChain && viewportChanged)
        {
            m_renderer.ResizeSwapChain(doc->swapChain, (UINT)viewportW, (UINT)viewportH,
                doc->rtv, doc->dsv, doc->depthTex);
            RenderActiveTab();
        }
    }
}

// Posiciona os controles da barra lateral e devolve a altura total ocupada,
// que alimenta a barra de rolagem.
int MainWindow::LayoutSidebar(int panelWidth)
{
    if (!m_sidebarPanel || panelWidth <= 0) return 0;

    const int sbX = 14;
    const int sbW = std::max(80, panelWidth - 28);
    int y = 12 - m_sidebarScroll;
    const int rowH = 21;

    HDWP dwp = BeginDeferWindowPos(56);
    auto place = [&dwp](HWND ctrl, int x, int yy, int w, int h)
    {
        if (!ctrl || !dwp) return;
        dwp = DeferWindowPos(dwp, ctrl, nullptr, x, yy, w, h,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    };

    place(m_sbTitle, sbX, y, sbW, 24); y += 30;
    for (int i = 0; i < 6; i++)
    {
        place(m_sbStatLabels[i], sbX, y, sbW / 2 + 20, rowH);
        place(m_sbStatValues[i], sbX + sbW / 2 + 20, y, sbW / 2 - 20, rowH);
        y += rowH + 2;
    }
    y += 10;

    const bool hasDoc = (ActiveDocument() != nullptr);
    if (!hasDoc)
    {
        place(m_sbHint, sbX, y, sbW, 76);
        y += 84;
    }
    else if (m_viewMode == ViewMode::Model3D)
    {
        place(m_sbDisplayTitle, sbX, y, sbW, 24); y += 30;
        const int halfW = (sbW - 6) / 2;
        place(m_btnMaterial, sbX, y, halfW, 28);
        place(m_btnWireframe, sbX + halfW + 6, y, halfW, 28); y += 32;
        place(m_btnShadows, sbX, y, halfW, 28);
        place(m_btnGrid, sbX + halfW + 6, y, halfW, 28); y += 32;
        place(m_btnReframe, sbX, y, sbW, 28); y += 40;

        place(m_sbLightTitle, sbX, y, sbW, 24); y += 30;

        // Presets: grade 3x2
        const int cell = (sbW - 12) / 3;
        for (int i = 0; i < THEME_COUNT; i++)
        {
            const int col = i % 3, row = i / 3;
            place(m_themeButtons[i], sbX + col * (cell + 6), y + row * (cell + 6), cell, cell);
        }
        y += 2 * (cell + 6) + 10;

        // Editor da luz selecionada
        place(m_lightSelect, sbX, y, sbW, 220); y += 30;
        place(m_lightEnabled, sbX, y, sbW, 22); y += 26;

        const int swatchW = 46;
        place(m_colorModel, sbX, y, sbW - swatchW - 6, 200);
        place(m_colorSwatch, sbX + sbW - swatchW, y, swatchW, 22); y += 28;
        place(m_hexEdit, sbX, y, sbW, 22); y += 30;

        const int editW = 50;
        const int sliderW = sbW - editW - 6;
        for (int i = 0; i < 3; i++)
        {
            place(m_channelLabels[i], sbX, y, sbW, 16); y += 17;
            place(m_channelSliders[i], sbX, y, sliderW, 24);
            place(m_channelEdits[i], sbX + sliderW + 6, y + 1, editW, 22); y += 28;
        }

        place(m_intensityLabel, sbX, y, sbW, 16); y += 17;
        place(m_intensitySlider, sbX, y, sliderW, 24);
        place(m_intensityEdit, sbX + sliderW + 6, y + 1, editW, 22); y += 32;

        place(m_sbRotLabel, sbX, y, sbW, 20); y += 22;
        const int dialSize = 100;
        place(m_lightDial, sbX + (sbW - dialSize) / 2, y, dialSize, dialSize);
        y += dialSize + 16;
    }
    else
    {
        place(m_sbUvTitle, sbX, y, sbW, 24); y += 30;
        place(m_sbUvMaterialLabel, sbX, y, sbW, 20); y += 22;
        place(m_uvMaterialCombo, sbX, y, sbW, 240); y += 32;
        place(m_sbUvTextureInfo, sbX, y, sbW, 56); y += 62;
        place(m_btnUvReset, sbX, y, sbW, 28); y += 40;
        place(m_sbUvHint, sbX, y, sbW, 76); y += 84;
    }

    if (dwp) EndDeferWindowPos(dwp);

    // Altura total = ultima posicao (desfazendo a rolagem) + margem inferior
    return y + m_sidebarScroll + 12;
}

void MainWindow::SetViewMode(ViewMode mode)
{
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    if (m_viewTabs) TabCtrl_SetCurSel(m_viewTabs, (int)mode);
    m_sidebarScroll = 0;
    UpdateSidebarVisibility();
    LayoutChildren();
    RenderActiveTab();
}

void MainWindow::OnViewTabChanged()
{
    int sel = TabCtrl_GetCurSel(m_viewTabs);
    SetViewMode(sel == 1 ? ViewMode::UvMap : ViewMode::Model3D);
}

// ---------------------------------------------------------------------------
// Editor de cor / intensidade
// ---------------------------------------------------------------------------
void MainWindow::ApplyTheme(int themeIndex)
{
    themeIndex = std::clamp(themeIndex, 0, THEME_COUNT - 1);
    m_themeIndex = themeIndex;
    const LightingTheme& theme = kThemes[themeIndex];

    m_backgroundColor = theme.background;
    m_ambient.color = theme.ambientColor;
    m_ambient.intensity = theme.ambientIntensity;
    m_mainLight.color = theme.mainLightColor;
    m_mainLight.intensity = 1.0f;
    for (int i = 0; i < 3; i++)
    {
        m_auxLights[i].color = theme.auxColors[i];
        m_auxLights[i].intensity = 1.0f;
        // O liga/desliga de cada auxiliar e escolha do usuario: o preset troca
        // so as cores, para nao apagar o que ele acabou de montar.
    }
    m_ground.color = theme.groundColor;
}

EditableLight* MainWindow::CurrentTarget()
{
    switch (m_lightTarget)
    {
    case LightTarget::MainLight: return &m_mainLight;
    case LightTarget::Aux1:      return &m_auxLights[0];
    case LightTarget::Aux2:      return &m_auxLights[1];
    case LightTarget::Aux3:      return &m_auxLights[2];
    case LightTarget::Ambient:   return &m_ambient;
    case LightTarget::Ground:    return &m_ground;
    default:                     return nullptr; // plano de fundo
    }
}

XMFLOAT3* MainWindow::CurrentTargetColor()
{
    if (m_lightTarget == LightTarget::Background) return &m_backgroundColor;
    EditableLight* light = CurrentTarget();
    return light ? &light->color : nullptr;
}

bool MainWindow::TargetHasIntensity(LightTarget target) const
{
    return target != LightTarget::Background;
}

bool MainWindow::TargetHasEnabled(LightTarget target) const
{
    return target == LightTarget::Aux1 || target == LightTarget::Aux2
        || target == LightTarget::Aux3 || target == LightTarget::Ground;
}

void MainWindow::LoadLightEditor()
{
    if (!m_lightSelect) return;

    m_suppressLightUi = true;

    const XMFLOAT3* color = CurrentTargetColor();
    const XMFLOAT3 rgb = color ? *color : XMFLOAT3(1, 1, 1);

    // Campo hexadecimal
    wchar_t hex[16];
    const COLORREF ref = ToColorRef(rgb);
    swprintf_s(hex, L"#%02X%02X%02X", GetRValue(ref), GetGValue(ref), GetBValue(ref));
    SetWindowTextW(m_hexEdit, hex);

    // Canais, conforme o modelo de cor escolhido
    const wchar_t* hsvLabels[3] = { L"Matiz", L"Saturação", L"Valor" };
    const wchar_t* rgbLabels[3] = { L"Vermelho", L"Verde", L"Azul" };
    int values[3] = {};
    int maxima[3] = {};

    if (m_colorModeHsv)
    {
        float h = 0, s = 0, v = 0;
        RgbToHsv(rgb, h, s, v);
        values[0] = (int)lroundf(h);
        values[1] = (int)lroundf(s * 100.0f);
        values[2] = (int)lroundf(v * 100.0f);
        maxima[0] = 359; maxima[1] = 100; maxima[2] = 100;
    }
    else
    {
        values[0] = GetRValue(ref);
        values[1] = GetGValue(ref);
        values[2] = GetBValue(ref);
        maxima[0] = maxima[1] = maxima[2] = 255;
    }

    for (int i = 0; i < 3; i++)
    {
        SetWindowTextW(m_channelLabels[i], m_colorModeHsv ? hsvLabels[i] : rgbLabels[i]);
        SendMessageW(m_channelSliders[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, maxima[i]));
        SendMessageW(m_channelSliders[i], TBM_SETPOS, TRUE, values[i]);
        SetControlInt(m_channelEdits[i], values[i]);
    }

    // Intensidade (ou opacidade, no caso do chao)
    const bool hasIntensity = TargetHasIntensity(m_lightTarget);
    const bool isGround = (m_lightTarget == LightTarget::Ground);
    EditableLight* light = CurrentTarget();

    EnableWindow(m_intensitySlider, hasIntensity);
    EnableWindow(m_intensityEdit, hasIntensity);
    EnableWindow(m_intensityLabel, hasIntensity);
    SetWindowTextW(m_intensityLabel, isGround ? L"Opacidade" : L"Intensidade");

    if (hasIntensity && light)
    {
        const int maxValue = isGround ? 100 : 200;
        const int position = std::clamp((int)lroundf(light->intensity * 100.0f), 0, maxValue);
        SendMessageW(m_intensitySlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, maxValue));
        SendMessageW(m_intensitySlider, TBM_SETPOS, TRUE, position);
        SetControlInt(m_intensityEdit, position);
    }

    // Liga/desliga
    const bool hasEnabled = TargetHasEnabled(m_lightTarget);
    ShowWindow(m_lightEnabled, hasEnabled ? SW_SHOW : SW_HIDE);
    SetWindowTextW(m_lightEnabled, isGround ? L"Mostrar chão" : L"Ativa");
    if (hasEnabled && light)
        SendMessageW(m_lightEnabled, BM_SETCHECK, light->enabled ? BST_CHECKED : BST_UNCHECKED, 0);

    InvalidateRect(m_colorSwatch, nullptr, TRUE);
    m_suppressLightUi = false;
}

void MainWindow::PushColorToState(const XMFLOAT3& color)
{
    if (XMFLOAT3* target = CurrentTargetColor())
        *target = color;
    InvalidateRect(m_colorSwatch, nullptr, TRUE);
    RenderActiveTab();
}

void MainWindow::OnChannelChanged(int channel, bool fromSlider)
{
    if (m_suppressLightUi || channel < 0 || channel > 2) return;

    const int maxValue = m_colorModeHsv ? (channel == 0 ? 359 : 100) : 255;
    int value;
    if (fromSlider)
        value = (int)SendMessageW(m_channelSliders[channel], TBM_GETPOS, 0, 0);
    else
        value = std::clamp(GetControlInt(m_channelEdits[channel], 0), 0, maxValue);

    m_suppressLightUi = true;
    if (fromSlider)
        SetControlInt(m_channelEdits[channel], value);
    else
        SendMessageW(m_channelSliders[channel], TBM_SETPOS, TRUE, value);
    m_suppressLightUi = false;

    // Le os tres canais e remonta a cor
    int channels[3];
    for (int i = 0; i < 3; i++)
        channels[i] = (int)SendMessageW(m_channelSliders[i], TBM_GETPOS, 0, 0);

    XMFLOAT3 color = m_colorModeHsv
        ? HsvToRgb((float)channels[0], channels[1] / 100.0f, channels[2] / 100.0f)
        : XMFLOAT3(channels[0] / 255.0f, channels[1] / 255.0f, channels[2] / 255.0f);

    m_suppressLightUi = true;
    wchar_t hex[16];
    const COLORREF ref = ToColorRef(color);
    swprintf_s(hex, L"#%02X%02X%02X", GetRValue(ref), GetGValue(ref), GetBValue(ref));
    SetWindowTextW(m_hexEdit, hex);
    m_suppressLightUi = false;

    PushColorToState(color);
}

void MainWindow::OnIntensityChanged(bool fromSlider)
{
    if (m_suppressLightUi) return;
    EditableLight* light = CurrentTarget();
    if (!light || !TargetHasIntensity(m_lightTarget)) return;

    const int maxValue = (m_lightTarget == LightTarget::Ground) ? 100 : 200;
    int value;
    if (fromSlider)
        value = (int)SendMessageW(m_intensitySlider, TBM_GETPOS, 0, 0);
    else
        value = std::clamp(GetControlInt(m_intensityEdit, 100), 0, maxValue);

    m_suppressLightUi = true;
    if (fromSlider)
        SetControlInt(m_intensityEdit, value);
    else
        SendMessageW(m_intensitySlider, TBM_SETPOS, TRUE, value);
    m_suppressLightUi = false;

    light->intensity = value / 100.0f;
    RenderActiveTab();
}

void MainWindow::OnHexChanged()
{
    if (m_suppressLightUi) return;

    wchar_t buffer[32] = {};
    GetWindowTextW(m_hexEdit, buffer, ARRAYSIZE(buffer));

    // Aceita "#RRGGBB" e "RRGGBB"; ignora enquanto o usuario ainda digita.
    const wchar_t* text = buffer;
    if (*text == L'#') text++;
    if (wcslen(text) != 6) return;

    unsigned int value = 0;
    for (int i = 0; i < 6; i++)
    {
        const wchar_t c = text[i];
        unsigned int digit;
        if (c >= L'0' && c <= L'9') digit = (unsigned int)(c - L'0');
        else if (c >= L'a' && c <= L'f') digit = (unsigned int)(c - L'a' + 10);
        else if (c >= L'A' && c <= L'F') digit = (unsigned int)(c - L'A' + 10);
        else return;
        value = (value << 4) | digit;
    }

    const XMFLOAT3 color(
        ((value >> 16) & 0xFF) / 255.0f,
        ((value >> 8) & 0xFF) / 255.0f,
        (value & 0xFF) / 255.0f);

    if (XMFLOAT3* target = CurrentTargetColor())
        *target = color;

    // Reflete nos sliders sem disparar a volta.
    m_suppressLightUi = true;
    if (m_colorModeHsv)
    {
        float h = 0, s = 0, v = 0;
        RgbToHsv(color, h, s, v);
        SendMessageW(m_channelSliders[0], TBM_SETPOS, TRUE, (int)lroundf(h));
        SendMessageW(m_channelSliders[1], TBM_SETPOS, TRUE, (int)lroundf(s * 100.0f));
        SendMessageW(m_channelSliders[2], TBM_SETPOS, TRUE, (int)lroundf(v * 100.0f));
        SetControlInt(m_channelEdits[0], (int)lroundf(h));
        SetControlInt(m_channelEdits[1], (int)lroundf(s * 100.0f));
        SetControlInt(m_channelEdits[2], (int)lroundf(v * 100.0f));
    }
    else
    {
        const int channels[3] = { (int)((value >> 16) & 0xFF), (int)((value >> 8) & 0xFF), (int)(value & 0xFF) };
        for (int i = 0; i < 3; i++)
        {
            SendMessageW(m_channelSliders[i], TBM_SETPOS, TRUE, channels[i]);
            SetControlInt(m_channelEdits[i], channels[i]);
        }
    }
    m_suppressLightUi = false;

    InvalidateRect(m_colorSwatch, nullptr, TRUE);
    RenderActiveTab();
}

void MainWindow::PickColorFromDialog()
{
    XMFLOAT3* target = CurrentTargetColor();
    if (!target) return;

    static COLORREF customColors[16] = {};
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = m_hwnd;
    cc.rgbResult = ToColorRef(*target);
    cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (!ChooseColorW(&cc)) return;

    *target = FromColorRef(cc.rgbResult);
    LoadLightEditor();
    RenderActiveTab();
}

// ---------------------------------------------------------------------------
// Arquivos recentes
// ---------------------------------------------------------------------------
void MainWindow::RebuildRecentMenu()
{
    if (!m_recentMenu) return;

    while (GetMenuItemCount(m_recentMenu) > 0)
        DeleteMenu(m_recentMenu, 0, MF_BYPOSITION);

    m_recentFiles = appsettings::LoadRecentFiles();

    if (m_recentFiles.empty())
    {
        AppendMenuW(m_recentMenu, MF_STRING | MF_GRAYED, 0, L"(nenhum arquivo recente)");
    }
    else
    {
        for (size_t i = 0; i < m_recentFiles.size(); i++)
        {
            // "&" no nome do arquivo viraria sublinhado de atalho.
            std::wstring label = std::to_wstring(i + 1) + L"  " + FileNameOnly(m_recentFiles[i]);
            std::wstring escaped;
            for (wchar_t c : label)
            {
                escaped.push_back(c);
                if (c == L'&') escaped.push_back(L'&');
            }
            AppendMenuW(m_recentMenu, MF_STRING, IDM_RECENT_BASE + i, escaped.c_str());
        }
        AppendMenuW(m_recentMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m_recentMenu, MF_STRING, IDM_RECENT_CLEAR, L"&Limpar lista");
    }

    if (m_hwnd) DrawMenuBar(m_hwnd);
}

void MainWindow::OpenRecentFile(int index)
{
    if (index < 0 || index >= (int)m_recentFiles.size()) return;
    OpenFile(m_recentFiles[index]);
}

// ---------------------------------------------------------------------------
// Exportacao de imagem
// ---------------------------------------------------------------------------
void MainWindow::ExportImage()
{
    TabDocument* doc = ActiveDocument();
    if (!doc)
    {
        MessageBoxW(m_hwnd, L"Abra um modelo antes de exportar a imagem.",
            L"Exportar imagem", MB_ICONINFORMATION);
        return;
    }
    if (m_viewMode != ViewMode::Model3D)
    {
        MessageBoxW(m_hwnd, L"A exportação usa a cena 3D. Volte para a aba \"Modelo 3D\".",
            L"Exportar imagem", MB_ICONINFORMATION);
        return;
    }

    RECT rc;
    GetClientRect(m_viewportContainer, &rc);
    const int viewportW = std::max<LONG>(1, rc.right - rc.left);
    const int viewportH = std::max<LONG>(1, rc.bottom - rc.top);

    ExportImageOptions options;
    options.renderShadows = doc->showShadows;
    options.showGrid = m_showGrid;
    if (!ShowExportImageDialog(m_hInstance, m_hwnd, viewportW, viewportH, options))
        return;

    LightingState lighting = BuildLightingState();
    lighting.showGrid = options.showGrid;

    std::vector<uint8_t> pixels;
    const ShadingMode mode = doc->showMaterial ? ShadingMode::Material : ShadingMode::NoMaterial;
    if (!m_renderer.RenderToImage((UINT)options.width, (UINT)options.height,
        doc->gpuModel, doc->model, doc->camera, mode, doc->showWireframe,
        options.renderShadows, lighting, options.transparent, pixels))
    {
        MessageBoxW(m_hwnd,
            L"Não foi possível renderizar a imagem nesse tamanho.\n"
            L"Tente uma resolução menor.",
            L"Exportar imagem", MB_ICONERROR);
        RenderActiveTab();
        return;
    }

    if (options.toClipboard)
    {
        const bool ok = CopyImageToClipboard(m_hwnd, pixels, options.width, options.height,
            options.transparent);
        if (!ok)
        {
            MessageBoxW(m_hwnd, L"Não foi possível copiar a imagem para a área de transferência.",
                L"Exportar imagem", MB_ICONWARNING);
        }
        RenderActiveTab();
        return;
    }

    // Nome sugerido: o do modelo, com a extensao do formato escolhido.
    std::wstring suggested = doc->model.displayName;
    size_t dot = suggested.find_last_of(L'.');
    if (dot != std::wstring::npos) suggested = suggested.substr(0, dot);
    suggested += L"." + std::wstring(ExportFormatExtension(options.format));

    wchar_t fileBuffer[MAX_PATH] = {};
    wcsncpy_s(fileBuffer, suggested.c_str(), _TRUNCATE);

    const std::wstring filter = ExportFormatFilter(options.format);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = ARRAYSIZE(fileBuffer);
    ofn.lpstrDefExt = ExportFormatExtension(options.format);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn))
    {
        RenderActiveTab();
        return;
    }

    std::wstring error;
    if (!SaveImageToFile(fileBuffer, options, pixels, options.width, options.height, error))
    {
        MessageBoxW(m_hwnd, (L"Falha ao salvar a imagem.\n\n" + error).c_str(),
            L"Exportar imagem", MB_ICONERROR);
    }

    // O render offscreen trocou o alvo do pipeline: redesenha a janela.
    RenderActiveTab();
}

void MainWindow::OpenInNewWindow()
{
    std::vector<std::wstring> paths = AskForFiles();
    if (paths.empty()) return;

    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return;

    // O switch faz a nova instancia ignorar o single-instance e abrir a
    // propria janela em vez de mandar os arquivos para esta.
    std::wstring arguments = kNewWindowSwitch;
    for (const std::wstring& path : paths)
        arguments += L" \"" + path + L"\"";

    ShellExecuteW(nullptr, L"open", exePath, arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

void MainWindow::OnHScroll(HWND control)
{
    if (!control) return;
    for (int i = 0; i < 3; i++)
    {
        if (control == m_channelSliders[i])
        {
            OnChannelChanged(i, true);
            return;
        }
    }
    if (control == m_intensitySlider)
        OnIntensityChanged(true);
}

void MainWindow::OnCommand(WPARAM wParam)
{
    int id = LOWORD(wParam);
    int notification = HIWORD(wParam);
    TabDocument* doc = ActiveDocument();

    // Presets de iluminacao
    if (id >= ID_THEME_BASE && id < ID_THEME_BASE + THEME_COUNT)
    {
        ApplyTheme(id - ID_THEME_BASE);
        for (int i = 0; i < THEME_COUNT; i++)
            InvalidateRect(m_themeButtons[i], nullptr, TRUE);
        LoadLightEditor();
        RenderActiveTab();
        return;
    }
    // Arquivos recentes
    if (id >= IDM_RECENT_BASE && id < IDM_RECENT_BASE + appsettings::kMaxRecentFiles)
    {
        OpenRecentFile(id - IDM_RECENT_BASE);
        return;
    }
    // Sliders de canal de cor (edicao numerica)
    if (id >= ID_CHANNEL_EDIT_BASE && id < ID_CHANNEL_EDIT_BASE + 3)
    {
        if (notification == EN_CHANGE)
            OnChannelChanged(id - ID_CHANNEL_EDIT_BASE, false);
        return;
    }

    switch (id)
    {
    case IDM_FILE_OPEN:
    case IDM_FILE_OPEN_NEW_TAB:
        OpenFileDialog();
        break;
    case IDM_FILE_NEW_WINDOW:
        OpenInNewWindow();
        break;
    case IDM_FILE_EXPORT_IMAGE:
        ExportImage();
        break;
    case IDM_RECENT_CLEAR:
        appsettings::ClearRecentFiles();
        RebuildRecentMenu();
        break;
    case IDM_FILE_CLOSE_TAB:
        CloseTab(m_activeTab);
        break;
    case IDM_FILE_EXIT:
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        break;
    case IDM_VIEW_3D:
        SetViewMode(ViewMode::Model3D);
        break;
    case IDM_VIEW_UV:
        SetViewMode(ViewMode::UvMap);
        break;
    case IDM_VIEW_GRID:
    case ID_BTN_GRID:
        if (id == ID_BTN_GRID)
            m_showGrid = (SendMessageW(m_btnGrid, BM_GETCHECK, 0, 0) == BST_CHECKED);
        else
            m_showGrid = !m_showGrid;
        SendMessageW(m_btnGrid, BM_SETCHECK, m_showGrid ? BST_CHECKED : BST_UNCHECKED, 0);
        CheckMenuItem(GetMenu(m_hwnd), IDM_VIEW_GRID,
            MF_BYCOMMAND | (m_showGrid ? MF_CHECKED : MF_UNCHECKED));
        RenderActiveTab();
        break;
    case IDM_VIEW_NEXT_TAB:
        CycleTab(1);
        break;
    case IDM_VIEW_PREV_TAB:
        CycleTab(-1);
        break;
    case IDM_TOOLS_REGISTER:
        if (fileassoc::RegisterNow())
        {
            MessageBoxW(m_hwnd,
                L"Pronto. O Visualizador 3D agora aparece em \"Abrir com\" para\n"
                L"arquivos .fbx, .obj, .ply, .glb, .gltf, .dae, .3ds e .dxf.\n\n"
                L"Para deixá-lo como programa padrão de um tipo, use\n"
                L"Ferramentas > Abrir Aplicativos padrão do Windows.",
                L"Associação de arquivos", MB_ICONINFORMATION);
        }
        else
        {
            MessageBoxW(m_hwnd, L"Não foi possível gravar as associações no registro.",
                L"Associação de arquivos", MB_ICONWARNING);
        }
        break;
    case IDM_TOOLS_DEFAULT_APPS:
        fileassoc::OpenDefaultAppsSettings();
        break;
    case ID_BTN_MATERIAL:
        if (doc)
        {
            doc->showMaterial = (SendMessageW(m_btnMaterial, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RenderActiveTab();
        }
        break;
    case ID_BTN_WIREFRAME:
        if (doc)
        {
            doc->showWireframe = (SendMessageW(m_btnWireframe, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RenderActiveTab();
        }
        break;
    case ID_BTN_SHADOWS:
        if (doc)
        {
            doc->showShadows = (SendMessageW(m_btnShadows, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RenderActiveTab();
        }
        break;
    case ID_BTN_REFRAME:
        if (doc)
        {
            FrameCameraToModel(*doc);
            RenderActiveTab();
        }
        break;
    case ID_BTN_UV_RESET:
        if (doc)
        {
            doc->uvView.zoom = 1.0f;
            doc->uvView.panX = 0.0f;
            doc->uvView.panY = 0.0f;
            RenderActiveTab();
        }
        break;
    case ID_UV_MATERIAL_COMBO:
        if (doc && notification == CBN_SELCHANGE)
        {
            int sel = (int)SendMessageW(m_uvMaterialCombo, CB_GETCURSEL, 0, 0);
            if (sel >= 0)
            {
                doc->uvView.materialIndex = sel;
                UpdateSidebar();
                RenderActiveTab();
            }
        }
        break;
    case ID_LIGHT_SELECT:
        if (notification == CBN_SELCHANGE)
        {
            int sel = (int)SendMessageW(m_lightSelect, CB_GETCURSEL, 0, 0);
            m_lightTarget = (LightTarget)std::clamp(sel, 0, (int)LightTarget::Count - 1);
            LoadLightEditor();
        }
        break;
    case ID_COLOR_MODEL:
        if (notification == CBN_SELCHANGE)
        {
            m_colorModeHsv = (SendMessageW(m_colorModel, CB_GETCURSEL, 0, 0) == 0);
            LoadLightEditor();
        }
        break;
    case ID_LIGHT_ENABLED:
        if (EditableLight* light = CurrentTarget())
        {
            light->enabled = (SendMessageW(m_lightEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED);
            RenderActiveTab();
        }
        break;
    case ID_COLOR_SWATCH:
        PickColorFromDialog();
        break;
    case ID_HEX_EDIT:
        if (notification == EN_CHANGE) OnHexChanged();
        break;
    case ID_INTENSITY_EDIT:
        if (notification == EN_CHANGE) OnIntensityChanged(false);
        break;
    }
}

void MainWindow::OnDropFiles(HDROP hDrop)
{
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < fileCount; i++)
    {
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(hDrop, i, path, MAX_PATH))
            OpenFile(path);
    }
    DragFinish(hDrop);
}

std::vector<std::wstring> MainWindow::AskForFiles()
{
    std::vector<std::wstring> result;

    wchar_t fileBuffer[4096] = {};
    std::wstring filter = BuildOpenDialogFilter();

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = ARRAYSIZE(fileBuffer);
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return result;

    // Com multiselecao o buffer traz a pasta, '\0', e cada nome de arquivo.
    std::wstring dirOrSingle = fileBuffer;
    wchar_t* p = fileBuffer + dirOrSingle.size() + 1;
    if (*p == L'\0')
    {
        result.push_back(dirOrSingle);
    }
    else
    {
        while (*p)
        {
            std::wstring fileName = p;
            result.push_back(dirOrSingle + L"\\" + fileName);
            p += fileName.size() + 1;
        }
    }
    return result;
}

void MainWindow::OpenFileDialog()
{
    for (const std::wstring& path : AskForFiles())
        OpenFile(path);
}

void MainWindow::OpenFile(const std::wstring& path)
{
    auto doc = std::make_unique<TabDocument>();

    std::wstring error;
    if (!LoadModelFile(path, doc->model, error))
    {
        std::wstring msg = L"Não foi possível abrir o arquivo:\n" + path + L"\n\n" + error;
        MessageBoxW(m_hwnd, msg.c_str(), L"Erro ao abrir arquivo", MB_ICONERROR);
        return;
    }

    if (!m_renderer.UploadModel(doc->model, doc->gpuModel))
    {
        MessageBoxW(m_hwnd, L"Falha ao enviar a malha para a GPU.", L"Erro", MB_ICONERROR);
        return;
    }
    doc->model.gpuReady = true;

    // Um modelo sem UVs mostraria a aba de UV vazia; o loader informa se o
    // arquivo trazia coordenadas, para avisarmos na sidebar.
    doc->hasUvs = doc->model.hasTexCoords;

    // Comeca mostrando o primeiro material que tem textura.
    for (size_t i = 0; i < doc->gpuModel.materialTextures.size(); i++)
    {
        if (doc->gpuModel.materialTextures[i].Valid())
        {
            doc->uvView.materialIndex = (int)i;
            break;
        }
    }

    size_t slashPos = path.find_last_of(L"\\/");
    doc->model.displayName = (slashPos == std::wstring::npos) ? path : path.substr(slashPos + 1);
    doc->model.filePath = path;

    FrameCameraToModel(*doc);

    RECT rc;
    GetClientRect(m_viewportContainer, &rc);
    UINT width = std::max<LONG>(1, rc.right - rc.left);
    UINT height = std::max<LONG>(1, rc.bottom - rc.top);

    if (!m_renderer.CreateSwapChainForWindow(m_viewportContainer, width, height,
        doc->swapChain, doc->rtv, doc->dsv, doc->depthTex))
    {
        MessageBoxW(m_hwnd, L"Falha ao criar a swap chain do DirectX para esta aba.", L"Erro", MB_ICONERROR);
        return;
    }

    m_documents.push_back(std::move(doc));
    int newIndex = (int)m_documents.size() - 1;

    std::wstring tabText = m_documents[newIndex]->model.displayName + L"   ✕";
    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;
    tie.pszText = tabText.data();
    TabCtrl_InsertItem(m_tabControl, newIndex, &tie);
    TabCtrl_SetCurSel(m_tabControl, newIndex);

    m_activeTab = newIndex;

    appsettings::AddRecentFile(path);
    RebuildRecentMenu();

    UpdateMaterialCombo();
    UpdateSidebar();
    UpdateWindowTitle();
    RenderActiveTab();
}

void MainWindow::CloseTab(int index)
{
    if (index < 0 || index >= (int)m_documents.size()) return;

    TabCtrl_DeleteItem(m_tabControl, index);
    m_documents.erase(m_documents.begin() + index);

    int count = (int)m_documents.size();
    if (count == 0)
    {
        m_activeTab = -1;
        InvalidateRect(m_viewportContainer, nullptr, TRUE);
    }
    else
    {
        if (m_activeTab >= index) m_activeTab = std::max(0, m_activeTab - 1);
        m_activeTab = std::min(m_activeTab, count - 1);
        TabCtrl_SetCurSel(m_tabControl, m_activeTab);
    }
    UpdateMaterialCombo();
    UpdateSidebar();
    UpdateWindowTitle();
    RenderActiveTab();
}

void MainWindow::SelectTab(int index)
{
    if (index < 0 || index >= (int)m_documents.size()) return;
    TabCtrl_SetCurSel(m_tabControl, index);
    // TabCtrl_SetCurSel nao dispara TCN_SELCHANGE — chamamos na mao.
    OnTabChanged();
}

void MainWindow::CycleTab(int delta)
{
    int count = (int)m_documents.size();
    if (count <= 1) return;
    int next = ((m_activeTab + delta) % count + count) % count;
    SelectTab(next);
}

void MainWindow::OnTabChanged()
{
    m_activeTab = TabCtrl_GetCurSel(m_tabControl);
    UpdateMaterialCombo();
    UpdateSidebar();
    UpdateWindowTitle();

    if (TabDocument* doc = ActiveDocument())
    {
        RECT rc;
        GetClientRect(m_viewportContainer, &rc);
        UINT width = std::max<LONG>(1, rc.right - rc.left);
        UINT height = std::max<LONG>(1, rc.bottom - rc.top);
        if (doc->swapChain)
            m_renderer.ResizeSwapChain(doc->swapChain, width, height, doc->rtv, doc->dsv, doc->depthTex);
    }

    RenderActiveTab();
}

void MainWindow::FrameCameraToModel(TabDocument& doc)
{
    XMFLOAT3 mn = doc.model.boundsMin;
    XMFLOAT3 mx = doc.model.boundsMax;
    XMFLOAT3 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
    float sizeX = mx.x - mn.x, sizeY = mx.y - mn.y, sizeZ = mx.z - mn.z;
    float radius = 0.5f * sqrtf(sizeX * sizeX + sizeY * sizeY + sizeZ * sizeZ);
    if (radius < 0.001f) radius = 1.0f;

    doc.camera.target = center;
    doc.camera.distance = radius * 2.2f;
    doc.camera.yaw = 0.6f;
    doc.camera.pitch = 0.35f;
}

void MainWindow::UpdateWindowTitle()
{
    if (!m_hwnd) return;
    if (TabDocument* doc = ActiveDocument())
        SetWindowTextW(m_hwnd, (doc->model.displayName + L" - " + kAppTitle).c_str());
    else
        SetWindowTextW(m_hwnd, kAppTitle);
}

void MainWindow::UpdateMaterialCombo()
{
    if (!m_uvMaterialCombo) return;
    SendMessageW(m_uvMaterialCombo, CB_RESETCONTENT, 0, 0);

    TabDocument* doc = ActiveDocument();
    if (!doc)
    {
        EnableWindow(m_uvMaterialCombo, FALSE);
        return;
    }

    for (size_t i = 0; i < doc->model.materials.size(); i++)
    {
        std::wstring name = Utf8ToWideSimple(doc->model.materials[i].name);
        if (name.empty()) name = L"Material " + std::to_wstring(i + 1);
        bool hasTexture = (i < doc->gpuModel.materialTextures.size())
            && doc->gpuModel.materialTextures[i].Valid();
        if (hasTexture) name += L"  • textura";
        SendMessageW(m_uvMaterialCombo, CB_ADDSTRING, 0, (LPARAM)name.c_str());
    }

    int count = (int)doc->model.materials.size();
    doc->uvView.materialIndex = std::clamp(doc->uvView.materialIndex, 0, std::max(0, count - 1));
    SendMessageW(m_uvMaterialCombo, CB_SETCURSEL, (WPARAM)doc->uvView.materialIndex, 0);
    EnableWindow(m_uvMaterialCombo, count > 0);
}

void MainWindow::UpdateSidebarVisibility()
{
    // Sem arquivo aberto nenhuma das duas secoes faz sentido: fica so a dica.
    const bool hasDoc = (ActiveDocument() != nullptr);
    const bool is3D = hasDoc && (m_viewMode == ViewMode::Model3D);
    const bool isUv = hasDoc && (m_viewMode == ViewMode::UvMap);
    const int show3D = is3D ? SW_SHOW : SW_HIDE;
    const int showUv = isUv ? SW_SHOW : SW_HIDE;
    ShowWindow(m_sbHint, hasDoc ? SW_HIDE : SW_SHOW);

    ShowWindow(m_sbDisplayTitle, show3D);
    ShowWindow(m_btnMaterial, show3D);
    ShowWindow(m_btnWireframe, show3D);
    ShowWindow(m_btnShadows, show3D);
    ShowWindow(m_btnGrid, show3D);
    ShowWindow(m_btnReframe, show3D);
    ShowWindow(m_sbLightTitle, show3D);
    for (int i = 0; i < THEME_COUNT; i++) ShowWindow(m_themeButtons[i], show3D);
    ShowWindow(m_lightSelect, show3D);
    ShowWindow(m_colorModel, show3D);
    ShowWindow(m_colorSwatch, show3D);
    ShowWindow(m_hexEdit, show3D);
    for (int i = 0; i < 3; i++)
    {
        ShowWindow(m_channelLabels[i], show3D);
        ShowWindow(m_channelSliders[i], show3D);
        ShowWindow(m_channelEdits[i], show3D);
    }
    ShowWindow(m_intensityLabel, show3D);
    ShowWindow(m_intensitySlider, show3D);
    ShowWindow(m_intensityEdit, show3D);
    ShowWindow(m_sbRotLabel, show3D);
    ShowWindow(m_lightDial, show3D);
    // O "Ativa" so aparece para as luzes que podem ser desligadas.
    ShowWindow(m_lightEnabled, (is3D && TargetHasEnabled(m_lightTarget)) ? SW_SHOW : SW_HIDE);

    ShowWindow(m_sbUvTitle, showUv);
    ShowWindow(m_sbUvMaterialLabel, showUv);
    ShowWindow(m_uvMaterialCombo, showUv);
    ShowWindow(m_sbUvTextureInfo, showUv);
    ShowWindow(m_btnUvReset, showUv);
    ShowWindow(m_sbUvHint, showUv);
}

void MainWindow::UpdateSidebar()
{
    TabDocument* doc = ActiveDocument();
    const bool hasDoc = (doc != nullptr);

    if (hasDoc)
    {
        const MeshStats& s = doc->model.stats;
        SetWindowTextW(m_sbStatValues[0], FormatThousands(s.triangleCount).c_str());
        SetWindowTextW(m_sbStatValues[1], FormatThousands(s.vertexCount).c_str());
        SetWindowTextW(m_sbStatValues[2], FormatThousands(s.edgeCount).c_str());
        SetWindowTextW(m_sbStatValues[3], FormatThousands(s.meshCount).c_str());
        SetWindowTextW(m_sbStatValues[4], FormatThousands(s.materialCount).c_str());
        SetWindowTextW(m_sbStatValues[5], FormatThousands(s.drawCallCount).c_str());

        SendMessageW(m_btnMaterial, BM_SETCHECK, doc->showMaterial ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_btnWireframe, BM_SETCHECK, doc->showWireframe ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_btnShadows, BM_SETCHECK, doc->showShadows ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_btnGrid, BM_SETCHECK, m_showGrid ? BST_CHECKED : BST_UNCHECKED, 0);

        // ---- Informacoes da textura do material selecionado ----
        std::wstring info;
        int mi = doc->uvView.materialIndex;
        if (mi >= 0 && mi < (int)doc->gpuModel.materialTextures.size() &&
            doc->gpuModel.materialTextures[mi].Valid())
        {
            const GpuTexture& tex = doc->gpuModel.materialTextures[mi];
            std::wstring name = doc->model.materials[mi].textureName;
            if (name.empty()) name = L"(sem nome)";
            info = name + L"\n" + std::to_wstring(tex.width) + L" × "
                + std::to_wstring(tex.height) + L" px";
        }
        else
        {
            info = L"Este material não tem textura.";
        }
        if (!doc->hasUvs)
            info += L"\nO modelo não tem coordenadas de UV.";
        SetWindowTextW(m_sbUvTextureInfo, info.c_str());
    }
    else
    {
        for (int i = 0; i < 6; i++)
            SetWindowTextW(m_sbStatValues[i], L"—");
        SendMessageW(m_btnMaterial, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(m_btnWireframe, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(m_btnShadows, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(m_sbUvTextureInfo, L"Nenhum arquivo aberto.");
    }

    EnableWindow(m_btnMaterial, hasDoc);
    EnableWindow(m_btnWireframe, hasDoc);
    EnableWindow(m_btnShadows, hasDoc);
    EnableWindow(m_btnReframe, hasDoc);
    EnableWindow(m_btnUvReset, hasDoc);

    // A sidebar muda de conteudo conforme o modo e conforme haver ou nao um
    // arquivo aberto — refaz visibilidade e posicoes de uma vez so.
    UpdateSidebarVisibility();
    LayoutChildren();
}

void MainWindow::RenderActiveTab()
{
    TabDocument* doc = ActiveDocument();
    if (!doc || !doc->swapChain || !doc->rtv || !doc->dsv) return;

    RECT rc;
    GetClientRect(m_viewportContainer, &rc);
    UINT width = std::max<LONG>(1, rc.right - rc.left);
    UINT height = std::max<LONG>(1, rc.bottom - rc.top);

    if (m_viewMode == ViewMode::UvMap)
    {
        m_renderer.RenderUvView(doc->rtv.Get(), width, height,
            doc->gpuModel, doc->model, doc->uvView);
    }
    else
    {
        ShadingMode mode = doc->showMaterial ? ShadingMode::Material : ShadingMode::NoMaterial;
        m_renderer.RenderScene(doc->rtv.Get(), doc->dsv.Get(), width, height,
            doc->gpuModel, doc->model, doc->camera, mode, doc->showWireframe, doc->showShadows,
            BuildLightingState());
    }

    m_renderer.EndFrame(doc->swapChain.Get());
}

LRESULT CALLBACK MainWindow::ViewportProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        self = (MainWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    else
    {
        self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self) return self->ViewportProc(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::ViewportProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Pintura de fundo: tratada ANTES do guard de aba ativa, para que a
    // janela seja limpa quando todas as abas forem fechadas.
    if (msg == WM_ERASEBKGND)
    {
        if (m_activeTab < 0)
        {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(ToColorRef(m_backgroundColor));
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
        }
        return 1;
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (m_activeTab < 0)
        {
            HBRUSH brush = CreateSolidBrush(ToColorRef(m_backgroundColor));
            FillRect(hdc, &ps.rcPaint, brush);
            DeleteObject(brush);
        }
        else
        {
            // Redesenha o frame: o conteudo da swap chain se perde quando a
            // janela e descoberta por outra.
            RenderActiveTab();
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    TabDocument* docPtr = ActiveDocument();
    if (!docPtr)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    TabDocument& doc = *docPtr;
    const bool uvMode = (m_viewMode == ViewMode::UvMap);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    const UINT vpWidth = std::max<LONG>(1, clientRect.right - clientRect.left);
    const UINT vpHeight = std::max<LONG>(1, clientRect.bottom - clientRect.top);

    switch (msg)
    {
    case WM_SIZE:
    {
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);
        if (doc.swapChain && width > 0 && height > 0)
        {
            m_renderer.ResizeSwapChain(doc.swapChain, width, height, doc.rtv, doc.dsv, doc.depthTex);
            RenderActiveTab();
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);
        POINT cursor = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        // Clique numa esfera do gizmo alinha a camera aquele eixo, como no
        // Blender. So no modo 3D e antes de comecar a orbitar.
        if (!uvMode)
        {
            XMFLOAT3 direction;
            if (Renderer::HitTestGizmo(doc.camera, vpWidth, vpHeight, cursor.x, cursor.y, direction))
            {
                doc.camera.LookFromDirection(direction);
                RenderActiveTab();
                return 0;
            }
        }

        if (uvMode)
            doc.panning = true;
        else
            doc.dragging = true;
        doc.lastMouse = cursor;
        SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (uvMode)
        {
            doc.uvView.zoom = 1.0f;
            doc.uvView.panX = 0.0f;
            doc.uvView.panY = 0.0f;
            RenderActiveTab();
        }
        return 0;
    case WM_LBUTTONUP:
        doc.dragging = false;
        if (uvMode) doc.panning = false;
        if (!doc.panning && !doc.dragging) ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        SetFocus(hwnd);
        doc.panning = true;
        doc.lastMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        doc.panning = false;
        if (!doc.dragging) ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
    {
        POINT cur = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int dx = cur.x - doc.lastMouse.x;
        int dy = cur.y - doc.lastMouse.y;

        if (uvMode)
        {
            if (doc.panning)
            {
                // Converte o deslocamento em pixels para NDC (o espaco em que
                // o pan da aba de UV e aplicado).
                doc.uvView.panX += (float)dx * 2.0f / (float)vpWidth;
                doc.uvView.panY -= (float)dy * 2.0f / (float)vpHeight;
                doc.lastMouse = cur;
                RenderActiveTab();
            }
            return 0;
        }

        if (doc.dragging)
        {
            doc.camera.yaw += dx * 0.008f;
            doc.camera.pitch += dy * 0.008f;
            doc.camera.pitch = std::clamp(doc.camera.pitch, -1.5f, 1.5f);
            doc.lastMouse = cur;
            RenderActiveTab();
        }
        else if (doc.panning)
        {
            float panScale = doc.camera.distance * 0.0015f;
            XMFLOAT3 eye = doc.camera.GetEyePosition();
            XMVECTOR eyeVec = XMLoadFloat3(&eye);
            XMVECTOR targetVec = XMLoadFloat3(&doc.camera.target);
            XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(targetVec, eyeVec));
            XMVECTOR up = XMVectorSet(0, 1, 0, 0);
            XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
            XMVECTOR camUp = XMVector3Cross(forward, right);

            XMVECTOR offsetVec = XMVectorAdd(
                XMVectorScale(right, -dx * panScale),
                XMVectorScale(camUp, dy * panScale));
            XMVECTOR newTarget = XMVectorAdd(targetVec, offsetVec);
            XMStoreFloat3(&doc.camera.target, newTarget);

            doc.lastMouse = cur;
            RenderActiveTab();
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);

        if (uvMode)
        {
            // Zoom em torno do cursor: o ponto sob o mouse fica parado.
            POINT local = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &local);
            float cursorX = ((float)local.x / (float)vpWidth) * 2.0f - 1.0f;
            float cursorY = 1.0f - ((float)local.y / (float)vpHeight) * 2.0f;

            float oldZoom = doc.uvView.zoom;
            float newZoom = std::clamp(oldZoom * ((delta > 0) ? 1.15f : 1.0f / 1.15f), 0.08f, 60.0f);
            float ratio = newZoom / oldZoom;

            doc.uvView.panX = cursorX + (doc.uvView.panX - cursorX) * ratio;
            doc.uvView.panY = cursorY + (doc.uvView.panY - cursorY) * ratio;
            doc.uvView.zoom = newZoom;
            RenderActiveTab();
            return 0;
        }

        float factor = (delta > 0) ? 0.85f : 1.18f;
        doc.camera.distance *= factor;
        doc.camera.distance = std::clamp(doc.camera.distance, 0.01f, 100000.0f);
        RenderActiveTab();
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == 'W' && !uvMode)
        {
            doc.showWireframe = !doc.showWireframe;
            UpdateSidebar();
            RenderActiveTab();
        }
        else if (wParam == 'G' && !uvMode)
        {
            SendMessageW(m_hwnd, WM_COMMAND, MAKEWPARAM(IDM_VIEW_GRID, 0), 0);
        }
        else if (wParam == 'F')
        {
            if (uvMode)
            {
                doc.uvView.zoom = 1.0f;
                doc.uvView.panX = 0.0f;
                doc.uvView.panY = 0.0f;
            }
            else
            {
                FrameCameraToModel(doc);
            }
            RenderActiveTab();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int MainWindow::RunMessageLoop()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!m_accelTable || !TranslateAcceleratorW(m_hwnd, m_accelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}

void MainWindow::OpenFiles(const std::vector<std::wstring>& paths)
{
    for (const std::wstring& p : paths)
        if (!p.empty())
            OpenFile(p);
}
