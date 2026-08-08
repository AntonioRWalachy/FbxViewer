#include "FileAssociation.h"
#include "AppSettings.h"
#include "ModelLoader.h"

#include <shlobj.h>
#include <shellapi.h>
#include <string>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace
{
    // ProgID mantido igual ao do registrar_associacao_fbx.bat: quem ja tinha
    // rodado o .bat continua com a associacao valida depois de atualizar.
    constexpr wchar_t kProgId[] = L"FbxViewer.Model";
    constexpr wchar_t kAppExeKey[] = L"Software\\Classes\\Applications\\FbxViewer.exe";
    constexpr wchar_t kCapabilitiesKey[] = L"Software\\FbxViewer\\Capabilities";
    constexpr wchar_t kFriendlyName[] = L"Visualizador 3D";
    constexpr wchar_t kProgIdDescription[] = L"Modelo 3D";

    // Bump quando o conjunto de chaves gravadas mudar, para que instalacoes
    // antigas sejam reescritas na proxima abertura.
    constexpr DWORD kRegistrationVersion = 2;

    constexpr wchar_t kValueExePath[] = L"RegisteredExePath";
    constexpr wchar_t kValueVersion[] = L"RegistrationVersion";

    std::wstring GetExecutablePath()
    {
        wchar_t buffer[MAX_PATH] = {};
        DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return L"";
        return buffer;
    }

    bool SetStringValue(HKEY root, const std::wstring& subKey, const wchar_t* valueName,
        const std::wstring& data)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
            != ERROR_SUCCESS)
            return false;

        LSTATUS status = RegSetValueExW(key, valueName, 0, REG_SZ,
            (const BYTE*)data.c_str(), (DWORD)((data.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }

    // Valor REG_NONE vazio — e o que o Windows espera em OpenWithProgids.
    bool SetEmptyNoneValue(HKEY root, const std::wstring& subKey, const wchar_t* valueName)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
            != ERROR_SUCCESS)
            return false;

        LSTATUS status = RegSetValueExW(key, valueName, 0, REG_NONE, nullptr, 0);
        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }

    bool NeedsRegistration(const std::wstring& exePath)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, appsettings::kSettingsKey, 0, KEY_QUERY_VALUE, &key)
            != ERROR_SUCCESS)
            return true;

        wchar_t stored[MAX_PATH] = {};
        DWORD size = sizeof(stored);
        DWORD type = 0;
        bool pathMatches = RegQueryValueExW(key, kValueExePath, nullptr, &type, (BYTE*)stored, &size)
            == ERROR_SUCCESS && type == REG_SZ && exePath == stored;

        DWORD version = 0;
        DWORD versionSize = sizeof(version);
        bool versionMatches = RegQueryValueExW(key, kValueVersion, nullptr, &type,
            (BYTE*)&version, &versionSize) == ERROR_SUCCESS
            && type == REG_DWORD && version == kRegistrationVersion;

        RegCloseKey(key);
        return !(pathMatches && versionMatches);
    }

    void StoreRegistrationStamp(const std::wstring& exePath)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, appsettings::kSettingsKey, 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return;

        RegSetValueExW(key, kValueExePath, 0, REG_SZ,
            (const BYTE*)exePath.c_str(), (DWORD)((exePath.size() + 1) * sizeof(wchar_t)));
        DWORD version = kRegistrationVersion;
        RegSetValueExW(key, kValueVersion, 0, REG_DWORD, (const BYTE*)&version, sizeof(version));
        RegCloseKey(key);
    }
}

bool fileassoc::RegisterNow()
{
    const std::wstring exePath = GetExecutablePath();
    if (exePath.empty()) return false;

    const std::wstring quotedExe = L"\"" + exePath + L"\"";
    const std::wstring openCommand = quotedExe + L" \"%1\"";
    const std::wstring iconRef = exePath + L",0";

    bool ok = true;

    // ---- ProgID: a "identidade" do tipo de arquivo ----
    const std::wstring progIdKey = std::wstring(L"Software\\Classes\\") + kProgId;
    ok &= SetStringValue(HKEY_CURRENT_USER, progIdKey, nullptr, kProgIdDescription);
    ok &= SetStringValue(HKEY_CURRENT_USER, progIdKey + L"\\DefaultIcon", nullptr, iconRef);
    ok &= SetStringValue(HKEY_CURRENT_USER, progIdKey + L"\\shell\\open", nullptr,
        L"Abrir com o Visualizador 3D");
    ok &= SetStringValue(HKEY_CURRENT_USER, progIdKey + L"\\shell\\open\\command", nullptr, openCommand);

    // ---- Entrada em "Applications": faz o app aparecer em "Abrir com" ----
    ok &= SetStringValue(HKEY_CURRENT_USER, kAppExeKey, L"FriendlyAppName", kFriendlyName);
    ok &= SetStringValue(HKEY_CURRENT_USER, std::wstring(kAppExeKey) + L"\\DefaultIcon", nullptr, iconRef);
    ok &= SetStringValue(HKEY_CURRENT_USER, std::wstring(kAppExeKey) + L"\\shell\\open\\command",
        nullptr, openCommand);

    // ---- Capabilities: e assim que o app aparece em "Aplicativos padrao" ----
    ok &= SetStringValue(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationName", kFriendlyName);
    ok &= SetStringValue(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationDescription",
        L"Visualizador de modelos 3D (FBX, OBJ, PLY, glTF/GLB, Collada, 3DS e DXF).");
    ok &= SetStringValue(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationIcon", iconRef);

    for (int i = 0; i < kSupportedExtensionCount; i++)
    {
        const std::wstring extension = L"." + std::wstring(kSupportedExtensions[i]);

        // Adiciona o app a lista de "Abrir com" da extensao SEM roubar o
        // padrao atual: quem decide o programa padrao e o usuario, pelo
        // Windows. Por isso nao mexemos no valor padrao de .<ext> nem na
        // chave UserChoice (que o Windows protege).
        ok &= SetEmptyNoneValue(HKEY_CURRENT_USER,
            L"Software\\Classes\\" + extension + L"\\OpenWithProgids", kProgId);

        // Tipos suportados: o Windows usa para filtrar a lista de "Abrir com".
        ok &= SetStringValue(HKEY_CURRENT_USER,
            std::wstring(kAppExeKey) + L"\\SupportedTypes", extension.c_str(), L"");

        ok &= SetStringValue(HKEY_CURRENT_USER,
            std::wstring(kCapabilitiesKey) + L"\\FileAssociations", extension.c_str(), kProgId);
    }

    ok &= SetStringValue(HKEY_CURRENT_USER, L"Software\\RegisteredApplications",
        L"FbxViewer", kCapabilitiesKey);

    if (ok)
        StoreRegistrationStamp(exePath);

    // Avisa o Explorer para reler as associacoes (sem reiniciar o Explorer,
    // como o .bat antigo fazia).
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

void fileassoc::EnsureRegistered()
{
    const std::wstring exePath = GetExecutablePath();
    if (exePath.empty()) return;
    if (!NeedsRegistration(exePath)) return; // caso comum: nao escreve nada
    RegisterNow();
}

void fileassoc::OpenDefaultAppsSettings()
{
    // ms-settings: e a pagina de aplicativos padrao no Windows 10/11.
    if ((INT_PTR)ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps",
        nullptr, nullptr, SW_SHOWNORMAL) <= 32)
    {
        ShellExecuteW(nullptr, L"open", L"control.exe", L"/name Microsoft.DefaultPrograms",
            nullptr, SW_SHOWNORMAL);
    }
}
