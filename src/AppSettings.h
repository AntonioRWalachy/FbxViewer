#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Preferencias do aplicativo persistidas em HKCU\Software\FbxViewer.
namespace appsettings
{
    // Quantos arquivos a lista "Abrir recentes" guarda.
    inline constexpr int kMaxRecentFiles = 12;

    // Lista de arquivos abertos recentemente, do mais recente para o mais
    // antigo. Entradas que nao existem mais em disco sao descartadas.
    std::vector<std::wstring> LoadRecentFiles();

    // Move o caminho para o topo da lista (ou insere, se for novo).
    void AddRecentFile(const std::wstring& path);

    void ClearRecentFiles();

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
