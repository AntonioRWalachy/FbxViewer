#include "MainWindow.h"
#include "AppSettings.h"
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

    constexpr int ID_BTN_MATERIAL = 1201;
    constexpr int ID_BTN_WIREFRAME = 1202;
    constexpr int ID_BTN_SHADOWS = 1203;
    constexpr int ID_BTN_REFRAME = 1204;
    constexpr int ID_BTN_UV_RESET = 1205;
    constexpr int ID_UV_MATERIAL_COMBO = 1206;

    constexpr int ID_THEME_BASE = 1301; // 1301..1306
    constexpr int ID_AUX_BASE = 1401;   // 1401..1403

    constexpr int IDM_FILE_OPEN = 2001;
    constexpr int IDM_FILE_OPEN_NEW_TAB = 2002;
    constexpr int IDM_FILE_CLOSE_TAB = 2003;
    constexpr int IDM_FILE_EXIT = 2004;
    constexpr int IDM_VIEW_3D = 2101;
    constexpr int IDM_VIEW_UV = 2102;
    constexpr int IDM_VIEW_NEXT_TAB = 2103;
    constexpr int IDM_VIEW_PREV_TAB = 2104;
    constexpr int IDM_TOOLS_REGISTER = 2201;
    constexpr int IDM_TOOLS_DEFAULT_APPS = 2202;

    const wchar_t* kViewportWndClass = L"FbxViewerViewportWnd";
    const wchar_t* kLightDialClass = L"FbxViewerLightDial";
    const wchar_t* kAppTitle = L"Visualizador 3D";

    constexpr int TOP_MARGIN = 30;   // altura da faixa de abas de arquivo
    constexpr int VIEW_TAB_H = 26;   // altura da faixa "Modelo 3D / Mapa UV"
    constexpr int SIDEBAR_W = 260;
    constexpr int TAB_CLOSE_ZONE = 22;
    constexpr int THEME_COUNT = 6;

    // Tamanho da janela na primeira execucao (depois vale o que o usuario
    // deixou na sessao anterior).
    constexpr int DEFAULT_WINDOW_W = 1280;
    constexpr int DEFAULT_WINDOW_H = 860;

    // ------------------------------------------------------------------
    // Temas de iluminacao (inspirados nas miniaturas do viewer nativo)
    // ------------------------------------------------------------------
    const LightingTheme kThemes[THEME_COUNT] = {
        { L"Neutro",
          XMFLOAT3(0.18f, 0.18f, 0.20f), XMFLOAT3(1, 1, 1), 0.22f,
          XMFLOAT3(1, 1, 1),
          { XMFLOAT3(0.75f, 0.82f, 1.0f), XMFLOAT3(1.0f, 0.88f, 0.75f), XMFLOAT3(0.85f, 1.0f, 0.88f) } },
        { L"Estúdio claro",
          XMFLOAT3(0.90f, 0.90f, 0.93f), XMFLOAT3(1, 1, 1), 0.38f,
          XMFLOAT3(1, 1, 1),
          { XMFLOAT3(0.85f, 0.9f, 1.0f), XMFLOAT3(1.0f, 0.95f, 0.85f), XMFLOAT3(0.9f, 0.9f, 0.9f) } },
        { L"Entardecer",
          XMFLOAT3(0.33f, 0.20f, 0.16f), XMFLOAT3(1.0f, 0.80f, 0.60f), 0.26f,
          XMFLOAT3(1.0f, 0.72f, 0.45f),
          { XMFLOAT3(1.0f, 0.6f, 0.4f), XMFLOAT3(0.6f, 0.5f, 0.8f), XMFLOAT3(1.0f, 0.85f, 0.6f) } },
        { L"Noite",
          XMFLOAT3(0.04f, 0.06f, 0.11f), XMFLOAT3(0.55f, 0.65f, 1.0f), 0.16f,
          XMFLOAT3(0.72f, 0.80f, 1.0f),
          { XMFLOAT3(0.4f, 0.55f, 1.0f), XMFLOAT3(0.7f, 0.75f, 1.0f), XMFLOAT3(0.5f, 0.9f, 1.0f) } },
        { L"Esverdeado",
          XMFLOAT3(0.14f, 0.22f, 0.20f), XMFLOAT3(0.75f, 1.0f, 0.90f), 0.26f,
          XMFLOAT3(0.88f, 1.0f, 0.94f),
          { XMFLOAT3(0.6f, 1.0f, 0.8f), XMFLOAT3(1.0f, 0.95f, 0.7f), XMFLOAT3(0.7f, 0.9f, 1.0f) } },
        { L"Dramático",
          XMFLOAT3(0.02f, 0.02f, 0.03f), XMFLOAT3(1, 1, 1), 0.07f,
          XMFLOAT3(1.15f, 1.12f, 1.05f),
          { XMFLOAT3(0.9f, 0.3f, 0.25f), XMFLOAT3(0.25f, 0.45f, 0.95f), XMFLOAT3(1.0f, 1.0f, 1.0f) } },
    };

    // Direcoes fixas das luzes auxiliares (direcao em que a luz viaja):
    // 1 = da esquerda, 2 = da direita, 3 = de tras/baixo (contorno)
    const XMFLOAT3 kAuxDirs[3] = {
        XMFLOAT3( 0.9f, -0.25f,  0.35f),
        XMFLOAT3(-0.9f, -0.25f,  0.35f),
        XMFLOAT3( 0.0f,  0.35f, -1.0f),
    };

    COLORREF ToColorRef(const XMFLOAT3& c)
    {
        auto clamp255 = [](float v) { return (BYTE)std::clamp((int)(v * 255.0f), 0, 255); };
        return RGB(clamp255(c.x), clamp255(c.y), clamp255(c.z));
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
}

LightingState MainWindow::BuildLightingState() const
{
    const LightingTheme& t = kThemes[std::clamp(m_themeIndex, 0, THEME_COUNT - 1)];
    LightingState ls;
    ls.background = t.background;
    ls.ambientColor = t.ambientColor;
    ls.ambientIntensity = t.ambientIntensity;
    ls.mainLightColor = t.mainLightColor;
    ls.rotationDeg = m_lightRotationDeg;
    ls.elevationDeg = 40.0f;
    for (int i = 0; i < 3; i++)
    {
        ls.aux[i].direction = kAuxDirs[i];
        ls.aux[i].color = t.auxColors[i];
        ls.aux[i].enabled = m_auxEnabled[i];
    }
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

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

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
    wc.hbrBackground = m_whiteBrush; // sidebar branca, estilo moderno
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

    WNDCLASSEXW dwc = {};
    dwc.cbSize = sizeof(dwc);
    dwc.lpfnWndProc = LightDialProcStatic;
    dwc.hInstance = hInstance;
    dwc.lpszClassName = kLightDialClass;
    dwc.hCursor = LoadCursor(nullptr, IDC_HAND);
    dwc.hbrBackground = nullptr;
    RegisterClassExW(&dwc);

    HMENU menuBar = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN, L"&Abrir...\tCtrl+O");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN_NEW_TAB, L"Abrir em &nova aba...\tCtrl+T");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_CLOSE_TAB, L"&Fechar aba\tCtrl+W");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"Sai&r");
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)fileMenu, L"&Arquivo");

    HMENU viewMenu = CreatePopupMenu();
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_3D, L"&Modelo 3D\tCtrl+1");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_UV, L"Mapa &UV e textura\tCtrl+2");
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

    ACCEL accels[] = {
        { FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN },
        { FVIRTKEY | FCONTROL, 'T', IDM_FILE_OPEN_NEW_TAB },
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
    case WM_CTLCOLORSTATIC:
    {
        // Fundo branco + texto transparente p/ os labels da sidebar
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

        // Fundo branco
        RECT full = { 0, 0, w, h };
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(mem, &full, white);
        DeleteObject(white);

        int cx = w / 2, cy = h / 2;
        int ringR = (std::min(w, h) / 2) - 12;

        // Anel azul (trilho)
        HPEN ringPen = CreatePen(PS_SOLID, 3, RGB(0, 103, 192));
        HGDIOBJ oldPen = SelectObject(mem, ringPen);
        SelectObject(mem, GetStockObject(NULL_BRUSH));
        Ellipse(mem, cx - ringR, cy - ringR, cx + ringR, cy + ringR);

        // Disco central cinza (o "modelo")
        int innerR = ringR - 14;
        HBRUSH grayBrush = CreateSolidBrush(RGB(205, 205, 208));
        HPEN nullPen = CreatePen(PS_NULL, 0, 0);
        SelectObject(mem, grayBrush);
        SelectObject(mem, nullPen);
        Ellipse(mem, cx - innerR, cy - innerR, cx + innerR, cy + innerR);

        // "Sol" na posicao do angulo atual (0 = topo, horario)
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

    CreateSidebar(hwnd);
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
    m_btnReframe = CreateWindowExW(0, L"BUTTON", L"Reenquadrar",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_REFRAME, m_hInstance, nullptr);
    ApplyFont(m_btnMaterial, m_uiFont);
    ApplyFont(m_btnWireframe, m_uiFont);
    ApplyFont(m_btnShadows, m_uiFont);
    ApplyFont(m_btnReframe, m_uiFont);

    // ---- Iluminacao ----
    m_sbLightTitle = CreateWindowExW(0, L"STATIC", L"Iluminação",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbLightTitle, m_uiFontBold);

    for (int i = 0; i < THEME_COUNT; i++)
    {
        m_themeButtons[i] = CreateWindowExW(0, L"BUTTON", kThemes[i].name,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 0, 0, parent, (HMENU)(INT_PTR)(ID_THEME_BASE + i), m_hInstance, nullptr);
    }

    m_sbRotLabel = CreateWindowExW(0, L"STATIC", L"Rotação de luz",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbRotLabel, m_uiFont);

    m_lightDial = CreateWindowExW(0, kLightDialClass, L"",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    SetWindowLongPtrW(m_lightDial, GWLP_USERDATA, (LONG_PTR)(int)m_lightRotationDeg);

    const wchar_t* auxNames[3] = { L"Luz 1 (esquerda)", L"Luz 2 (direita)", L"Luz 3 (contorno)" };
    for (int i = 0; i < 3; i++)
    {
        m_auxChecks[i] = CreateWindowExW(0, L"BUTTON", auxNames[i],
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 0, 0, parent, (HMENU)(INT_PTR)(ID_AUX_BASE + i), m_hInstance, nullptr);
        ApplyFont(m_auxChecks[i], m_uiFont);
    }

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
}

// Desenho customizado dos botoes de tema: quadrado com o fundo do tema,
// bolinha da cor da luz principal e borda azul quando selecionado.
void MainWindow::OnDrawItem(LPARAM lParam)
{
    DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
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

    // Reposiciona tudo num unico lote. SWP_NOCOPYBITS impede que o Windows
    // reaproveite o desenho antigo do controle ao move-lo — era isso que
    // deixava os botoes da sidebar com artefatos ao redimensionar.
    HDWP dwp = BeginDeferWindowPos(48);
    auto place = [&dwp](HWND ctrl, int x, int y, int w, int h)
    {
        if (!ctrl || !dwp) return;
        dwp = DeferWindowPos(dwp, ctrl, nullptr, x, y, w, h,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    };

    place(m_tabControl, 0, 0, width, TOP_MARGIN);
    place(m_viewTabs, 0, TOP_MARGIN, viewportW, VIEW_TAB_H);
    place(m_viewportContainer, 0, TOP_MARGIN + VIEW_TAB_H, viewportW, viewportH);

    // ---- Layout da sidebar ----
    const int sbX = width - SIDEBAR_W + 16;
    const int sbW = SIDEBAR_W - 32;
    int y = TOP_MARGIN + 12;
    const int rowH = 21;

    place(m_sbTitle, sbX, y, sbW, 24); y += 30;
    for (int i = 0; i < 6; i++)
    {
        place(m_sbStatLabels[i], sbX, y, sbW / 2 + 20, rowH);
        place(m_sbStatValues[i], sbX + sbW / 2 + 20, y, sbW / 2 - 20, rowH);
        y += rowH + 2;
    }
    y += 10;

    // Sem arquivo aberto so aparece a dica; as secoes de controle ficam
    // escondidas (ver UpdateSidebarVisibility), entao nao ocupam espaco.
    const bool hasDoc = (ActiveDocument() != nullptr);
    if (!hasDoc)
    {
        place(m_sbHint, sbX, y, sbW, 76);
    }
    else if (m_viewMode == ViewMode::Model3D)
    {
        place(m_sbDisplayTitle, sbX, y, sbW, 24); y += 30;
        int halfW = (sbW - 6) / 2;
        place(m_btnMaterial, sbX, y, halfW, 28);
        place(m_btnWireframe, sbX + halfW + 6, y, halfW, 28); y += 32;
        place(m_btnShadows, sbX, y, halfW, 28);
        place(m_btnReframe, sbX + halfW + 6, y, halfW, 28); y += 40;

        place(m_sbLightTitle, sbX, y, sbW, 24); y += 30;

        // Grade 3x2 de temas
        int cell = (sbW - 12) / 3;
        for (int i = 0; i < THEME_COUNT; i++)
        {
            int col = i % 3, row = i / 3;
            place(m_themeButtons[i], sbX + col * (cell + 6), y + row * (cell + 6), cell, cell);
        }
        y += 2 * (cell + 6) + 8;

        place(m_sbRotLabel, sbX, y, sbW, 20); y += 24;
        const int dialSize = 100;
        place(m_lightDial, sbX + (sbW - dialSize) / 2, y, dialSize, dialSize);
        y += dialSize + 10;

        for (int i = 0; i < 3; i++)
        {
            place(m_auxChecks[i], sbX, y, sbW, 24);
            y += 26;
        }
        y += 8;
    }
    else
    {
        place(m_sbUvTitle, sbX, y, sbW, 24); y += 30;
        place(m_sbUvMaterialLabel, sbX, y, sbW, 20); y += 22;
        // A altura de um combo inclui a lista suspensa; o campo em si mostra
        // so a primeira linha.
        place(m_uvMaterialCombo, sbX, y, sbW, 240); y += 32;
        place(m_sbUvTextureInfo, sbX, y, sbW, 56); y += 62;
        place(m_btnUvReset, sbX, y, sbW, 28); y += 40;
        place(m_sbUvHint, sbX, y, sbW, 76);
    }

    if (dwp) EndDeferWindowPos(dwp);

    // Repinta a faixa da sidebar (e os filhos dentro dela) depois do
    // reposicionamento.
    RECT sidebar = { width - SIDEBAR_W, 0, width, height };
    RedrawWindow(m_hwnd, &sidebar, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);

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

void MainWindow::SetViewMode(ViewMode mode)
{
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    if (m_viewTabs) TabCtrl_SetCurSel(m_viewTabs, (int)mode);
    UpdateSidebarVisibility();
    LayoutChildren();
    RenderActiveTab();
}

void MainWindow::OnViewTabChanged()
{
    int sel = TabCtrl_GetCurSel(m_viewTabs);
    SetViewMode(sel == 1 ? ViewMode::UvMap : ViewMode::Model3D);
}

void MainWindow::OnCommand(WPARAM wParam)
{
    int id = LOWORD(wParam);
    int notification = HIWORD(wParam);
    TabDocument* doc = ActiveDocument();

    // Botoes de tema
    if (id >= ID_THEME_BASE && id < ID_THEME_BASE + THEME_COUNT)
    {
        m_themeIndex = id - ID_THEME_BASE;
        for (int i = 0; i < THEME_COUNT; i++)
            InvalidateRect(m_themeButtons[i], nullptr, TRUE);
        RenderActiveTab();
        return;
    }
    // Checkboxes das luzes auxiliares
    if (id >= ID_AUX_BASE && id < ID_AUX_BASE + 3)
    {
        int i = id - ID_AUX_BASE;
        m_auxEnabled[i] = (SendMessageW(m_auxChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
        RenderActiveTab();
        return;
    }

    switch (id)
    {
    case IDM_FILE_OPEN:
    case IDM_FILE_OPEN_NEW_TAB:
        OpenFileDialog();
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

void MainWindow::OpenFileDialog()
{
    wchar_t fileBuffer[4096] = {};
    std::wstring filter = BuildOpenDialogFilter();

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = ARRAYSIZE(fileBuffer);
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring dirOrSingle = fileBuffer;
    wchar_t* p = fileBuffer + dirOrSingle.size() + 1;
    if (*p == L'\0')
    {
        OpenFile(dirOrSingle);
    }
    else
    {
        while (*p)
        {
            std::wstring fileName = p;
            std::wstring fullPath = dirOrSingle + L"\\" + fileName;
            OpenFile(fullPath);
            p += fileName.size() + 1;
        }
    }
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
    ShowWindow(m_btnReframe, show3D);
    ShowWindow(m_sbLightTitle, show3D);
    for (int i = 0; i < THEME_COUNT; i++) ShowWindow(m_themeButtons[i], show3D);
    ShowWindow(m_sbRotLabel, show3D);
    ShowWindow(m_lightDial, show3D);
    for (int i = 0; i < 3; i++) ShowWindow(m_auxChecks[i], show3D);

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
            XMFLOAT3 bg = BuildLightingState().background;
            HBRUSH brush = CreateSolidBrush(ToColorRef(bg));
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
            XMFLOAT3 bg = BuildLightingState().background;
            HBRUSH brush = CreateSolidBrush(ToColorRef(bg));
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
            POINT screen = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            POINT local = screen;
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
