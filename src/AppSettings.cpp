#include "AppSettings.h"

#include <algorithm>
#include <string>

namespace
{
    constexpr wchar_t kValueX[] = L"WindowX";
    constexpr wchar_t kValueY[] = L"WindowY";
    constexpr wchar_t kValueWidth[] = L"WindowWidth";
    constexpr wchar_t kValueHeight[] = L"WindowHeight";
    constexpr wchar_t kValueMaximized[] = L"WindowMaximized";

    // Tamanho minimo aceito ao restaurar: protege contra um valor corrompido
    // deixar a janela inutilizavel.
    constexpr int kMinWidth = 480;
    constexpr int kMinHeight = 360;

    bool WriteDword(HKEY key, const wchar_t* name, DWORD value)
    {
        return RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value, sizeof(value)) == ERROR_SUCCESS;
    }

    bool ReadDword(HKEY key, const wchar_t* name, DWORD& outValue)
    {
        DWORD type = 0;
        DWORD size = sizeof(DWORD);
        return RegQueryValueExW(key, name, nullptr, &type, (BYTE*)&outValue, &size) == ERROR_SUCCESS
            && type == REG_DWORD;
    }

    // A janela precisa ter uma parte visivel em algum monitor conectado —
    // senao ela reabriria fora da tela depois de desligar um monitor.
    bool IsRectOnAnyMonitor(const RECT& rect)
    {
        HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONULL);
        if (!monitor) return false;

        MONITORINFO info = { sizeof(info) };
        if (!GetMonitorInfoW(monitor, &info)) return false;

        RECT intersection = {};
        if (!IntersectRect(&intersection, &rect, &info.rcWork)) return false;

        // Exige uma faixa util visivel (barra de titulo alcancavel pelo mouse).
        return (intersection.right - intersection.left) >= 160
            && (intersection.bottom - intersection.top) >= 80;
    }
}

void appsettings::SaveWindowPlacement(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return;

    WINDOWPLACEMENT placement = { sizeof(placement) };
    if (!GetWindowPlacement(hwnd, &placement)) return;

    // rcNormalPosition e o retangulo "restaurado": guardando ele, uma janela
    // fechada maximizada volta maximizada e, ao restaurar, retoma o tamanho
    // que o usuario tinha escolhido.
    const RECT& normal = placement.rcNormalPosition;
    const bool maximized = (placement.showCmd == SW_SHOWMAXIMIZED);

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;

    WriteDword(key, kValueX, (DWORD)(LONG)normal.left);
    WriteDword(key, kValueY, (DWORD)(LONG)normal.top);
    WriteDword(key, kValueWidth, (DWORD)(normal.right - normal.left));
    WriteDword(key, kValueHeight, (DWORD)(normal.bottom - normal.top));
    WriteDword(key, kValueMaximized, maximized ? 1u : 0u);
    RegCloseKey(key);
}

bool appsettings::LoadWindowPlacement(int& outX, int& outY, int& outWidth, int& outHeight, bool& outMaximized)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    DWORD x = 0, y = 0, width = 0, height = 0, maximized = 0;
    bool ok = ReadDword(key, kValueX, x)
        && ReadDword(key, kValueY, y)
        && ReadDword(key, kValueWidth, width)
        && ReadDword(key, kValueHeight, height);
    ReadDword(key, kValueMaximized, maximized);
    RegCloseKey(key);

    if (!ok) return false;

    outX = (int)(LONG)x;
    outY = (int)(LONG)y;
    outWidth = std::max((int)width, kMinWidth);
    outHeight = std::max((int)height, kMinHeight);
    outMaximized = (maximized != 0);

    RECT rect = { outX, outY, outX + outWidth, outY + outHeight };
    return IsRectOnAnyMonitor(rect);
}

// ---------------------------------------------------------------------------
// Lista de arquivos recentes
// ---------------------------------------------------------------------------
namespace
{
    const std::wstring kRecentKey = std::wstring(appsettings::kSettingsKey) + L"\\Recent";

    std::wstring RecentValueName(int index)
    {
        return L"File" + std::to_wstring(index);
    }

    bool ReadString(HKEY key, const std::wstring& name, std::wstring& out)
    {
        DWORD type = 0;
        DWORD bytes = 0;
        if (RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS
            || type != REG_SZ || bytes < sizeof(wchar_t))
            return false;

        std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(key, name.c_str(), nullptr, &type,
            (BYTE*)buffer.data(), &bytes) != ERROR_SUCCESS)
            return false;

        while (!buffer.empty() && buffer.back() == L'\0')
            buffer.pop_back();
        out = buffer;
        return !out.empty();
    }

    void WriteRecentList(const std::vector<std::wstring>& files)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRecentKey.c_str(), 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return;

        for (int i = 0; i < appsettings::kMaxRecentFiles; i++)
        {
            const std::wstring name = RecentValueName(i);
            if (i < (int)files.size())
            {
                RegSetValueExW(key, name.c_str(), 0, REG_SZ, (const BYTE*)files[i].c_str(),
                    (DWORD)((files[i].size() + 1) * sizeof(wchar_t)));
            }
            else
            {
                RegDeleteValueW(key, name.c_str());
            }
        }
        RegCloseKey(key);
    }
}

std::vector<std::wstring> appsettings::LoadRecentFiles()
{
    std::vector<std::wstring> files;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRecentKey.c_str(), 0, KEY_QUERY_VALUE, &key)
        != ERROR_SUCCESS)
        return files;

    for (int i = 0; i < kMaxRecentFiles; i++)
    {
        std::wstring path;
        if (!ReadString(key, RecentValueName(i), path)) continue;

        // Arquivo movido ou apagado nao volta para o menu.
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;

        files.push_back(path);
    }
    RegCloseKey(key);
    return files;
}

void appsettings::AddRecentFile(const std::wstring& path)
{
    if (path.empty()) return;

    std::vector<std::wstring> files = LoadRecentFiles();

    // Remove a entrada antiga (comparacao sem diferenciar maiusculas, como o
    // proprio sistema de arquivos do Windows) antes de colocar no topo.
    files.erase(std::remove_if(files.begin(), files.end(),
        [&path](const std::wstring& existing)
        {
            return _wcsicmp(existing.c_str(), path.c_str()) == 0;
        }), files.end());

    files.insert(files.begin(), path);
    if ((int)files.size() > kMaxRecentFiles)
        files.resize(kMaxRecentFiles);

    WriteRecentList(files);
}

void appsettings::ClearRecentFiles()
{
    WriteRecentList({});
}
