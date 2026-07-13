#include "MainWindow.h"
#include "FbxLoader.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <string>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace
{
    constexpr int ID_TAB_CONTROL = 1001;
    constexpr int ID_VIEWPORT = 1003;

    constexpr int ID_BTN_MATERIAL = 1201;
    constexpr int ID_BTN_WIREFRAME = 1202;
    constexpr int ID_BTN_SHADOWS = 1203;
    constexpr int ID_BTN_REFRAME = 1204;

    constexpr int IDM_FILE_OPEN = 2001;
    constexpr int IDM_FILE_OPEN_NEW_TAB = 2002;
    constexpr int IDM_FILE_CLOSE_TAB = 2003;
    constexpr int IDM_FILE_EXIT = 2004;

    const wchar_t* kViewportWndClass = L"FbxViewerViewportWnd";

    constexpr int TOP_MARGIN = 28;    // altura do tab control
    constexpr int SIDEBAR_W = 240;    // largura da sidebar de estatisticas
    constexpr int TAB_CLOSE_ZONE = 22; // area clicavel do "X" na borda direita da aba

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
}

bool MainWindow::Create(HINSTANCE hInstance)
{
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Fontes de UI modernas (Segoe UI) em vez da fonte System antiga
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    m_uiFont = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW boldLf = ncm.lfMessageFont;
    boldLf.lfWeight = FW_SEMIBOLD;
    boldLf.lfHeight = (LONG)(boldLf.lfHeight * 1.25f);
    m_uiFontBold = CreateFontIndirectW(&boldLf);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProcStatic;
    wc.hInstance = hInstance;
    wc.lpszClassName = kMainWindowClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    WNDCLASSEXW vwc = {};
    vwc.cbSize = sizeof(vwc);
    vwc.lpfnWndProc = ViewportProcStatic;
    vwc.hInstance = hInstance;
    vwc.lpszClassName = kViewportWndClass;
    vwc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    vwc.hbrBackground = nullptr; // evita flicker, D3D limpa o fundo
    RegisterClassExW(&vwc);

    HMENU menuBar = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN, L"&Abrir...\tCtrl+O");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN_NEW_TAB, L"Abrir em &nova aba...\tCtrl+T");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_CLOSE_TAB, L"&Fechar aba\tCtrl+W");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"Sai&r");
    AppendMenuW(menuBar, MF_POPUP, (UINT_PTR)fileMenu, L"&Arquivo");

    m_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        kMainWindowClassName, L"Visualizador FBX",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1250, 780,
        nullptr, menuBar, hInstance, this);

    if (!m_hwnd) return false;

    ACCEL accels[] = {
        { FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN },
        { FVIRTKEY | FCONTROL, 'T', IDM_FILE_OPEN_NEW_TAB },
        { FVIRTKEY | FCONTROL, 'W', IDM_FILE_CLOSE_TAB },
    };
    m_accelTable = CreateAcceleratorTableW(accels, ARRAYSIZE(accels));

    if (!m_renderer.InitDevice())
    {
        MessageBoxW(m_hwnd, L"Falha ao inicializar o DirectX 11. Verifique se sua GPU/driver suporta D3D11.",
            L"Erro", MB_ICONERROR);
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
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
    case WM_NOTIFY:
    {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr->idFrom == ID_TAB_CONTROL && hdr->code == TCN_SELCHANGE)
            OnTabChanged();
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
        // Caminho de arquivo enviado por uma segunda instancia do app
        // (duplo clique num .fbx com o app ja aberto).
        COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
        if (cds && cds->dwData == kCopyDataOpenFile && cds->lpData && cds->cbData >= sizeof(wchar_t))
        {
            // Garante terminacao nula mesmo que o emissor nao a inclua
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
                return 0; // consome o clique (nao muda a selecao)
            }
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
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

    m_viewportContainer = CreateWindowExW(0, kViewportWndClass, L"",
        WS_CHILD | WS_VISIBLE,
        0, TOP_MARGIN, rc.right - rc.left - SIDEBAR_W, rc.bottom - rc.top - TOP_MARGIN,
        hwnd, (HMENU)(INT_PTR)ID_VIEWPORT, m_hInstance, this);

    CreateSidebar(hwnd);
    UpdateSidebar();
    DragAcceptFiles(hwnd, TRUE);
}

void MainWindow::CreateSidebar(HWND parent)
{
    const wchar_t* statNames[5] = { L"Tri\u00e2ngulos", L"V\u00e9rtices", L"Edges", L"Malhas", L"Materiais" };

    m_sbTitle = CreateWindowExW(0, L"STATIC", L"Estat\u00edsticas",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbTitle, m_uiFontBold);

    for (int i = 0; i < 5; i++)
    {
        m_sbStatLabels[i] = CreateWindowExW(0, L"STATIC", statNames[i],
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
        m_sbStatValues[i] = CreateWindowExW(0, L"STATIC", L"\u2014",
            WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
        ApplyFont(m_sbStatLabels[i], m_uiFont);
        ApplyFont(m_sbStatValues[i], m_uiFont);
    }

    m_sbDisplayTitle = CreateWindowExW(0, L"STATIC", L"Exibi\u00e7\u00e3o",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbDisplayTitle, m_uiFontBold);

    // Toggles no estilo push-button (afundado = ativo)
    m_btnMaterial = CreateWindowExW(0, L"BUTTON", L"Material",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_MATERIAL, m_hInstance, nullptr);
    m_btnWireframe = CreateWindowExW(0, L"BUTTON", L"Wireframe",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_WIREFRAME, m_hInstance, nullptr);
    m_btnShadows = CreateWindowExW(0, L"BUTTON", L"Sombras",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_SHADOWS, m_hInstance, nullptr);
    m_btnReframe = CreateWindowExW(0, L"BUTTON", L"Reenquadrar c\u00e2mera",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)ID_BTN_REFRAME, m_hInstance, nullptr);
    ApplyFont(m_btnMaterial, m_uiFont);
    ApplyFont(m_btnWireframe, m_uiFont);
    ApplyFont(m_btnShadows, m_uiFont);
    ApplyFont(m_btnReframe, m_uiFont);

    m_sbHint = CreateWindowExW(0, L"STATIC",
        L"Abra um arquivo .fbx ou .obj\n(Arquivo > Abrir, ou arraste\ne solte na janela).",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, parent, nullptr, m_hInstance, nullptr);
    ApplyFont(m_sbHint, m_uiFont);
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

    if (m_tabControl)
        SetWindowPos(m_tabControl, nullptr, 0, 0, width, TOP_MARGIN, SWP_NOZORDER);

    int viewportW = std::max(1, width - SIDEBAR_W);
    int viewportH = std::max(1, height - TOP_MARGIN);
    if (m_viewportContainer)
        SetWindowPos(m_viewportContainer, nullptr, 0, TOP_MARGIN, viewportW, viewportH, SWP_NOZORDER);

    // ---- Layout da sidebar (coluna direita) ----
    int sbX = width - SIDEBAR_W + 14;
    int sbW = SIDEBAR_W - 28;
    int y = TOP_MARGIN + 14;
    const int rowH = 24;

    SetWindowPos(m_sbTitle, nullptr, sbX, y, sbW, 26, SWP_NOZORDER); y += 34;
    for (int i = 0; i < 5; i++)
    {
        SetWindowPos(m_sbStatLabels[i], nullptr, sbX, y, sbW / 2 + 20, rowH, SWP_NOZORDER);
        SetWindowPos(m_sbStatValues[i], nullptr, sbX + sbW / 2 + 20, y, sbW / 2 - 20, rowH, SWP_NOZORDER);
        y += rowH + 4;
    }
    y += 16;
    SetWindowPos(m_sbDisplayTitle, nullptr, sbX, y, sbW, 26, SWP_NOZORDER); y += 34;
    SetWindowPos(m_btnMaterial, nullptr, sbX, y, sbW, 30, SWP_NOZORDER); y += 36;
    SetWindowPos(m_btnWireframe, nullptr, sbX, y, sbW, 30, SWP_NOZORDER); y += 36;
    SetWindowPos(m_btnShadows, nullptr, sbX, y, sbW, 30, SWP_NOZORDER); y += 36;
    SetWindowPos(m_btnReframe, nullptr, sbX, y, sbW, 30, SWP_NOZORDER); y += 46;
    SetWindowPos(m_sbHint, nullptr, sbX, y, sbW, 70, SWP_NOZORDER);

    if (m_activeTab >= 0 && m_activeTab < (int)m_documents.size())
    {
        TabDocument& doc = *m_documents[m_activeTab];
        if (doc.swapChain)
        {
            m_renderer.ResizeSwapChain(doc.swapChain, (UINT)viewportW, (UINT)viewportH, doc.rtv, doc.dsv, doc.depthTex);
            RenderActiveTab();
        }
    }
}

void MainWindow::OnCommand(WPARAM wParam)
{
    int id = LOWORD(wParam);
    TabDocument* doc = (m_activeTab >= 0 && m_activeTab < (int)m_documents.size())
        ? m_documents[m_activeTab].get() : nullptr;

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
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Modelos 3D (*.fbx;*.obj)\0*.fbx;*.obj\0Arquivos FBX (*.fbx)\0*.fbx\0Arquivos OBJ (*.obj)\0*.obj\0Todos os arquivos\0*.*\0";
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = ARRAYSIZE(fileBuffer);
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return;

    // Com OFN_ALLOWMULTISELECT, o buffer contem: "diretorio\0arquivo1\0arquivo2\0\0"
    // (ou um unico caminho completo, se so um arquivo foi escolhido).
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
    if (!LoadFbxFile(path, doc->model, error))
    {
        std::wstring msg = L"Nao foi possivel abrir o arquivo:\n" + path + L"\n\n" + error;
        MessageBoxW(m_hwnd, msg.c_str(), L"Erro ao abrir arquivo", MB_ICONERROR);
        return;
    }

    if (!m_renderer.UploadModel(doc->model, doc->gpuModel))
    {
        MessageBoxW(m_hwnd, L"Falha ao enviar a malha para a GPU.", L"Erro", MB_ICONERROR);
        return;
    }
    doc->model.gpuReady = true;

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

    // Titulo da aba inclui o "X" de fechar (hit-test no TabSubclassProc)
    std::wstring tabText = m_documents[newIndex]->model.displayName + L"   \u2715";
    TCITEMW tie = {};
    tie.mask = TCIF_TEXT;
    tie.pszText = tabText.data();
    TabCtrl_InsertItem(m_tabControl, newIndex, &tie);
    TabCtrl_SetCurSel(m_tabControl, newIndex);

    m_activeTab = newIndex;
    UpdateSidebar();
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
    UpdateSidebar();
    RenderActiveTab();
}

void MainWindow::OnTabChanged()
{
    m_activeTab = TabCtrl_GetCurSel(m_tabControl);
    UpdateSidebar();

    // A aba que acabou de ficar ativa pode ter uma swapchain com tamanho
    // desatualizado, caso a janela tenha sido redimensionada enquanto outra
    // aba estava em primeiro plano. Sincroniza antes de desenhar.
    if (m_activeTab >= 0 && m_activeTab < (int)m_documents.size())
    {
        TabDocument& doc = *m_documents[m_activeTab];
        RECT rc;
        GetClientRect(m_viewportContainer, &rc);
        UINT width = std::max<LONG>(1, rc.right - rc.left);
        UINT height = std::max<LONG>(1, rc.bottom - rc.top);
        if (doc.swapChain)
            m_renderer.ResizeSwapChain(doc.swapChain, width, height, doc.rtv, doc.dsv, doc.depthTex);
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

void MainWindow::UpdateSidebar()
{
    bool hasDoc = (m_activeTab >= 0 && m_activeTab < (int)m_documents.size());

    if (hasDoc)
    {
        const TabDocument& doc = *m_documents[m_activeTab];
        const MeshStats& s = doc.model.stats;
        SetWindowTextW(m_sbStatValues[0], FormatThousands(s.triangleCount).c_str());
        SetWindowTextW(m_sbStatValues[1], FormatThousands(s.vertexCount).c_str());
        SetWindowTextW(m_sbStatValues[2], FormatThousands(s.edgeCount).c_str());
        SetWindowTextW(m_sbStatValues[3], FormatThousands(s.meshCount).c_str());
        SetWindowTextW(m_sbStatValues[4], FormatThousands(s.materialCount).c_str());

        SendMessageW(m_btnMaterial, BM_SETCHECK, doc.showMaterial ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_btnWireframe, BM_SETCHECK, doc.showWireframe ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(m_btnShadows, BM_SETCHECK, doc.showShadows ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowWindow(m_sbHint, SW_HIDE);
    }
    else
    {
        for (int i = 0; i < 5; i++)
            SetWindowTextW(m_sbStatValues[i], L"\u2014");
        SendMessageW(m_btnMaterial, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(m_btnWireframe, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(m_btnShadows, BM_SETCHECK, BST_UNCHECKED, 0);
        ShowWindow(m_sbHint, SW_SHOW);
    }

    EnableWindow(m_btnMaterial, hasDoc);
    EnableWindow(m_btnWireframe, hasDoc);
    EnableWindow(m_btnShadows, hasDoc);
    EnableWindow(m_btnReframe, hasDoc);
}

void MainWindow::RenderActiveTab()
{
    if (m_activeTab < 0 || m_activeTab >= (int)m_documents.size()) return;
    TabDocument& doc = *m_documents[m_activeTab];
    if (!doc.swapChain || !doc.rtv || !doc.dsv) return;

    RECT rc;
    GetClientRect(m_viewportContainer, &rc);
    UINT width = std::max<LONG>(1, rc.right - rc.left);
    UINT height = std::max<LONG>(1, rc.bottom - rc.top);

    ShadingMode mode = doc.showMaterial ? ShadingMode::Material : ShadingMode::NoMaterial;
    m_renderer.RenderScene(doc.rtv.Get(), doc.dsv.Get(), width, height,
        doc.gpuModel, doc.model, doc.camera, mode, doc.showWireframe, doc.showShadows);

    m_renderer.EndFrame(doc.swapChain.Get());
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
    // janela seja limpa (cinza escuro) quando todas as abas forem fechadas —
    // caso contrario o ultimo frame renderizado pelo D3D fica "congelado".
    if (msg == WM_ERASEBKGND)
    {
        if (m_activeTab < 0)
        {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(RGB(46, 46, 51)); // mesmo tom do clear do D3D
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
        }
        return 1; // com aba ativa o D3D limpa o frame; sem aba ja pintamos aqui
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (m_activeTab < 0)
        {
            HBRUSH brush = CreateSolidBrush(RGB(46, 46, 51));
            FillRect(hdc, &ps.rcPaint, brush);
            DeleteObject(brush);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    if (m_activeTab < 0 || m_activeTab >= (int)m_documents.size())
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    TabDocument& doc = *m_documents[m_activeTab];

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
        SetFocus(hwnd);
        doc.dragging = true;
        doc.lastMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        doc.dragging = false;
        if (!doc.panning) ReleaseCapture();
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
        float factor = (delta > 0) ? 0.85f : 1.18f;
        doc.camera.distance *= factor;
        doc.camera.distance = std::clamp(doc.camera.distance, 0.01f, 100000.0f);
        RenderActiveTab();
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == 'W')
        {
            doc.showWireframe = !doc.showWireframe;
            UpdateSidebar();
            RenderActiveTab();
        }
        else if (wParam == 'F')
        {
            FrameCameraToModel(doc);
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
