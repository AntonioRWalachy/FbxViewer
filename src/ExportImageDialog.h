#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

// Formatos oferecidos na caixa "Exportar imagem".
enum class ExportFormat
{
    Png = 0,
    Jpeg = 1,
    Bmp = 2,
};

struct ExportImageOptions
{
    ExportFormat format = ExportFormat::Png;
    int width = 1920;
    int height = 1080;
    bool transparent = false;   // so faz sentido em PNG
    bool renderShadows = true;
    bool showGrid = false;
    int quality = 100;          // 1..100, usado no JPEG
    bool toClipboard = false;   // true = o usuario clicou em "Copiar"
};

// Mostra a caixa de dialogo. Retorna false se o usuario cancelou.
// viewportWidth/Height alimentam o preset "tamanho atual do viewport" e a
// proporcao usada pelo "fixar proporcao".
bool ShowExportImageDialog(HINSTANCE instance, HWND owner,
    int viewportWidth, int viewportHeight, ExportImageOptions& options);

// Grava os pixels (BGRA, linhas de cima para baixo) no arquivo indicado,
// codificando conforme options.format.
bool SaveImageToFile(const std::wstring& path, const ExportImageOptions& options,
    const std::vector<uint8_t>& bgra, int width, int height, std::wstring& outError);

// Coloca a imagem na area de transferencia como CF_DIBV5 (que preserva alfa).
bool CopyImageToClipboard(HWND owner, const std::vector<uint8_t>& bgra,
    int width, int height, bool keepAlpha);

// Extensao e filtro do GetSaveFileName correspondentes ao formato.
const wchar_t* ExportFormatExtension(ExportFormat format);
std::wstring ExportFormatFilter(ExportFormat format);
