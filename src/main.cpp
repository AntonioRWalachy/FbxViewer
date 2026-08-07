#include "MainWindow.h"
#include "FileAssociation.h"
#include <objbase.h>
#include <shellapi.h>
#include <vector>
#include <string>

// Habilita os "visual styles" do Windows (ComCtl32 v6): sem isto, botoes,
// checkboxes e abas sao desenhados com o visual classico do Windows 95.
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
    // Nome global do mutex que identifica uma instancia ja em execucao.
    // "Local\" restringe ao desktop/sessao do usuario atual.
    const wchar_t* kInstanceMutexName = L"Local\\FbxViewer_SingleInstance_2AAA9632";

    std::vector<std::wstring> GetCommandLineFiles()
    {
        std::vector<std::wstring> files;
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv)
        {
            for (int i = 1; i < argc; i++) // argv[0] = caminho do proprio exe
                if (argv[i] && argv[i][0] != L'\0')
                    files.push_back(argv[i]);
            LocalFree(argv);
        }
        return files;
    }

    // Localiza a janela da instancia original. A primeira instancia pode
    // ainda estar inicializando (ex.: usuario deu duplo clique em varios
    // arquivos em sequencia rapida), entao tenta por ate ~3 segundos.
    HWND FindExistingInstanceWindow()
    {
        for (int attempt = 0; attempt < 30; attempt++)
        {
            HWND hwnd = FindWindowW(kMainWindowClassName, nullptr);
            if (hwnd) return hwnd;
            Sleep(100);
        }
        return nullptr;
    }

    // Envia os arquivos para a instancia original abrir como abas.
    // Retorna true se conseguiu entregar (ou trazer a janela para frente).
    bool ForwardToExistingInstance(const std::vector<std::wstring>& files)
    {
        HWND target = FindExistingInstanceWindow();
        if (!target) return false;

        if (IsIconic(target))
            ShowWindow(target, SW_RESTORE);
        SetForegroundWindow(target);

        for (const std::wstring& f : files)
        {
            COPYDATASTRUCT cds = {};
            cds.dwData = kCopyDataOpenFile;
            cds.cbData = (DWORD)((f.size() + 1) * sizeof(wchar_t)); // inclui o \0
            cds.lpData = (PVOID)f.c_str();
            SendMessageW(target, WM_COPYDATA, 0, (LPARAM)&cds);
        }
        return true;
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    std::vector<std::wstring> files = GetCommandLineFiles();

    // ---- Single-instance ----
    // Tenta criar o mutex nomeado. Se ele ja existe, ha outra instancia
    // rodando: encaminha os arquivos para ela via WM_COPYDATA e encerra.
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, kInstanceMutexName);
    bool alreadyRunning = (instanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS);

    if (alreadyRunning)
    {
        bool delivered = ForwardToExistingInstance(files);
        if (instanceMutex) CloseHandle(instanceMutex);
        if (delivered)
            return 0;
        // Se a outra instancia nao respondeu (ex.: travada/encerrando),
        // segue em frente e abre uma janela propria como fallback.
    }

    (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Registra o app como visualizador de arquivos 3D no primeiro uso (e
    // sempre que o .exe mudar de pasta). Nas demais aberturas nao escreve
    // nada no registro — nao ha mais necessidade de rodar o .bat.
    fileassoc::EnsureRegistered();

    MainWindow window;
    if (!window.Create(hInstance))
    {
        // Falha visivel: sem isso, o processo morre em silencio e fica
        // impossivel diagnosticar (ex.: quando aberto por duplo clique).
        MessageBoxW(nullptr,
            L"O Visualizador 3D não conseguiu inicializar.\n\n"
            L"Verifique se a pasta \"shaders\" e a libfbxsdk.dll estão\n"
            L"na mesma pasta do FbxViewer.exe.",
            L"Visualizador 3D - Erro", MB_ICONERROR);
        if (instanceMutex && !alreadyRunning) { ReleaseMutex(instanceMutex); CloseHandle(instanceMutex); }
        CoUninitialize();
        return -1;
    }

    window.OpenFiles(files);

    int result = window.RunMessageLoop();

    if (instanceMutex && !alreadyRunning)
    {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
    }
    CoUninitialize();
    return result;
}
