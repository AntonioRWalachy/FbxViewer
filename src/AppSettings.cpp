#include "AppSettings.h"

#include <algorithm>

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
