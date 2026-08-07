#pragma once
#include <windows.h>

// Preferencias do aplicativo persistidas em HKCU\Software\FbxViewer.
namespace appsettings
{
    // Caminho da chave usada por todo o app (tambem pelo registro de
    // associacoes de arquivo).
    inline constexpr wchar_t kSettingsKey[] = L"Software\\FbxViewer";

    // Guarda posicao, tamanho e estado (maximizado) da janela.
    void SaveWindowPlacement(HWND hwnd);

    // Recupera o que foi salvo na sessao anterior. Retorna false se nao ha
    // nada salvo ou se a posicao nao cai em nenhum monitor conectado — nesse
    // caso o chamador deve usar o tamanho padrao.
    bool LoadWindowPlacement(int& outX, int& outY, int& outWidth, int& outHeight, bool& outMaximized);
}
