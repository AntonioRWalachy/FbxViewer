#include "ExportImageDialog.h"
#include "resource.h"

#include <commctrl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    // Presets de tamanho, na mesma linha do que o visualizador nativo oferece.
    // width/height 0 = "usar o tamanho atual do viewport";
    // -1 = "personalizado" (o usuario digita).
    struct SizePreset
    {
        const wchar_t* label;
        int width;
        int height;
    };

    const SizePreset kPresets[] = {
        { L"Tamanho atual do viewport",  0,    0 },
        { L"640 × 480 (4:3)",            640,  480 },
        { L"800 × 600 (4:3)",            800,  600 },
        { L"1024 × 768 (4:3)",           1024, 768 },
        { L"1280 × 720 (16:9)",          1280, 720 },
        { L"1920 × 1080 (16:9)",         1920, 1080 },
        { L"2560 × 1440 (16:9)",         2560, 1440 },
        { L"3840 × 2160 (16:9)",         3840, 2160 },
        { L"Personalizado",              -1,   -1 },
    };
    constexpr int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));
    constexpr int kCustomPresetIndex = kPresetCount - 1;

    constexpr int kMaxDimension = 8192;

    struct DialogState
    {
        ExportImageOptions* options;
        int viewportWidth;
        int viewportHeight;
        bool updating = false; // evita que a UI reaja as proprias mudancas
    };

    int ReadEditInt(HWND dialog, int controlId, int fallback)
    {
        wchar_t buffer[32] = {};
        GetDlgItemTextW(dialog, controlId, buffer, ARRAYSIZE(buffer));
        if (buffer[0] == L'\0') return fallback;
        int value = _wtoi(buffer);
        return (value > 0) ? value : fallback;
    }

    void SetEditInt(HWND dialog, int controlId, int value)
    {
        wchar_t buffer[32];
        swprintf_s(buffer, L"%d", value);
        SetDlgItemTextW(dialog, controlId, buffer);
    }

    const wchar_t* QualityAdjective(int quality)
    {
        if (quality >= 95) return L"muito alta";
        if (quality >= 80) return L"alta";
        if (quality >= 55) return L"média";
        if (quality >= 30) return L"baixa";
        return L"muito baixa";
    }

    void UpdateQualityLabel(HWND dialog, int quality)
    {
        wchar_t buffer[96];
        swprintf_s(buffer, L"Qualidade %d%% (%s)", quality, QualityAdjective(quality));
        SetDlgItemTextW(dialog, IDC_EXP_QUALITY_LABEL, buffer);
    }

    // Liga/desliga o que depende do formato e do preset escolhidos.
    void RefreshEnabledState(HWND dialog, DialogState& state)
    {
        const int formatIndex = (int)SendDlgItemMessageW(dialog, IDC_EXP_FORMAT, CB_GETCURSEL, 0, 0);
        const int presetIndex = (int)SendDlgItemMessageW(dialog, IDC_EXP_PRESET, CB_GETCURSEL, 0, 0);
        const bool custom = (presetIndex == kCustomPresetIndex);
        const bool isPng = (formatIndex == (int)ExportFormat::Png);
        const bool isJpeg = (formatIndex == (int)ExportFormat::Jpeg);

        EnableWindow(GetDlgItem(dialog, IDC_EXP_WIDTH), custom);
        EnableWindow(GetDlgItem(dialog, IDC_EXP_HEIGHT), custom);
        EnableWindow(GetDlgItem(dialog, IDC_EXP_WIDTH_LABEL), custom);
        EnableWindow(GetDlgItem(dialog, IDC_EXP_HEIGHT_LABEL), custom);
        EnableWindow(GetDlgItem(dialog, IDC_EXP_KEEPRATIO), custom);

        // Só o PNG dos três guarda canal alfa.
        EnableWindow(GetDlgItem(dialog, IDC_EXP_TRANSPARENT), isPng);
        if (!isPng)
            CheckDlgButton(dialog, IDC_EXP_TRANSPARENT, BST_UNCHECKED);

        EnableWindow(GetDlgItem(dialog, IDC_EXP_QUALITY), isJpeg);
        EnableWindow(GetDlgItem(dialog, IDC_EXP_QUALITY_LABEL), isJpeg);

        (void)state;
    }

    void ApplyPreset(HWND dialog, DialogState& state)
    {
        const int index = (int)SendDlgItemMessageW(dialog, IDC_EXP_PRESET, CB_GETCURSEL, 0, 0);
        if (index < 0 || index >= kPresetCount) return;
        if (index == kCustomPresetIndex) return; // mantem o que o usuario digitou

        int width = kPresets[index].width;
        int height = kPresets[index].height;
        if (width == 0 || height == 0)
        {
            width = state.viewportWidth;
            height = state.viewportHeight;
        }

        state.updating = true;
        SetEditInt(dialog, IDC_EXP_WIDTH, width);
        SetEditInt(dialog, IDC_EXP_HEIGHT, height);
        state.updating = false;
    }

    // Com "fixar proporcao" marcado, mexer numa dimensao recalcula a outra
    // usando a proporcao do viewport.
    void SyncAspect(HWND dialog, DialogState& state, bool widthChanged)
    {
        if (state.updating) return;
        if (!IsDlgButtonChecked(dialog, IDC_EXP_KEEPRATIO)) return;
        if (state.viewportWidth <= 0 || state.viewportHeight <= 0) return;

        const double aspect = (double)state.viewportWidth / (double)state.viewportHeight;
        state.updating = true;
        if (widthChanged)
        {
            int width = ReadEditInt(dialog, IDC_EXP_WIDTH, state.viewportWidth);
            SetEditInt(dialog, IDC_EXP_HEIGHT, std::max(1, (int)(width / aspect + 0.5)));
        }
        else
        {
            int height = ReadEditInt(dialog, IDC_EXP_HEIGHT, state.viewportHeight);
            SetEditInt(dialog, IDC_EXP_WIDTH, std::max(1, (int)(height * aspect + 0.5)));
        }
        state.updating = false;
    }

    bool CommitOptions(HWND dialog, DialogState& state, bool toClipboard)
    {
        ExportImageOptions& options = *state.options;

        const int formatIndex = (int)SendDlgItemMessageW(dialog, IDC_EXP_FORMAT, CB_GETCURSEL, 0, 0);
        options.format = (ExportFormat)std::clamp(formatIndex, 0, 2);

        options.width = ReadEditInt(dialog, IDC_EXP_WIDTH, state.viewportWidth);
        options.height = ReadEditInt(dialog, IDC_EXP_HEIGHT, state.viewportHeight);

        if (options.width < 1 || options.height < 1 ||
            options.width > kMaxDimension || options.height > kMaxDimension)
        {
            wchar_t message[160];
            swprintf_s(message,
                L"Largura e altura precisam estar entre 1 e %d pixels.", kMaxDimension);
            MessageBoxW(dialog, message, L"Exportar imagem", MB_ICONWARNING);
            return false;
        }

        options.transparent = IsDlgButtonChecked(dialog, IDC_EXP_TRANSPARENT) == BST_CHECKED;
        options.renderShadows = IsDlgButtonChecked(dialog, IDC_EXP_SHADOWS) == BST_CHECKED;
        options.showGrid = IsDlgButtonChecked(dialog, IDC_EXP_GRID) == BST_CHECKED;
        options.quality = (int)SendDlgItemMessageW(dialog, IDC_EXP_QUALITY, TBM_GETPOS, 0, 0);
        options.quality = std::clamp(options.quality, 1, 100);
        options.toClipboard = toClipboard;
        return true;
    }

    INT_PTR CALLBACK ExportDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
    {
        DialogState* state = (DialogState*)GetWindowLongPtrW(dialog, GWLP_USERDATA);

        switch (message)
        {
        case WM_INITDIALOG:
        {
            state = (DialogState*)lParam;
            SetWindowLongPtrW(dialog, GWLP_USERDATA, (LONG_PTR)state);

            const wchar_t* formats[] = { L"PNG", L"JPEG", L"BMP" };
            for (const wchar_t* name : formats)
                SendDlgItemMessageW(dialog, IDC_EXP_FORMAT, CB_ADDSTRING, 0, (LPARAM)name);
            SendDlgItemMessageW(dialog, IDC_EXP_FORMAT, CB_SETCURSEL,
                (WPARAM)state->options->format, 0);

            for (const SizePreset& preset : kPresets)
            {
                std::wstring label = preset.label;
                if (preset.width == 0)
                    label += L" (" + std::to_wstring(state->viewportWidth) + L" × "
                        + std::to_wstring(state->viewportHeight) + L")";
                SendDlgItemMessageW(dialog, IDC_EXP_PRESET, CB_ADDSTRING, 0, (LPARAM)label.c_str());
            }
            // Padrao: o tamanho do proprio viewport, que e o "o que voce ve".
            SendDlgItemMessageW(dialog, IDC_EXP_PRESET, CB_SETCURSEL, 0, 0);

            SendDlgItemMessageW(dialog, IDC_EXP_QUALITY, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
            SendDlgItemMessageW(dialog, IDC_EXP_QUALITY, TBM_SETPOS, TRUE,
                (LPARAM)std::clamp(state->options->quality, 1, 100));
            UpdateQualityLabel(dialog, state->options->quality);

            CheckDlgButton(dialog, IDC_EXP_KEEPRATIO, BST_CHECKED);
            CheckDlgButton(dialog, IDC_EXP_TRANSPARENT,
                state->options->transparent ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_EXP_SHADOWS,
                state->options->renderShadows ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_EXP_GRID,
                state->options->showGrid ? BST_CHECKED : BST_UNCHECKED);

            ApplyPreset(dialog, *state);
            RefreshEnabledState(dialog, *state);
            return TRUE;
        }

        case WM_HSCROLL:
            if (state && (HWND)lParam == GetDlgItem(dialog, IDC_EXP_QUALITY))
            {
                int quality = (int)SendDlgItemMessageW(dialog, IDC_EXP_QUALITY, TBM_GETPOS, 0, 0);
                UpdateQualityLabel(dialog, quality);
            }
            return TRUE;

        case WM_COMMAND:
        {
            if (!state) break;
            const int id = LOWORD(wParam);
            const int notification = HIWORD(wParam);

            switch (id)
            {
            case IDC_EXP_FORMAT:
                if (notification == CBN_SELCHANGE)
                    RefreshEnabledState(dialog, *state);
                return TRUE;

            case IDC_EXP_PRESET:
                if (notification == CBN_SELCHANGE)
                {
                    ApplyPreset(dialog, *state);
                    RefreshEnabledState(dialog, *state);
                }
                return TRUE;

            case IDC_EXP_WIDTH:
                if (notification == EN_CHANGE) SyncAspect(dialog, *state, true);
                return TRUE;

            case IDC_EXP_HEIGHT:
                if (notification == EN_CHANGE) SyncAspect(dialog, *state, false);
                return TRUE;

            case IDOK:
                if (CommitOptions(dialog, *state, false))
                    EndDialog(dialog, IDOK);
                return TRUE;

            case IDC_EXP_CLIPBOARD:
                if (CommitOptions(dialog, *state, true))
                    EndDialog(dialog, IDOK);
                return TRUE;

            case IDCANCEL:
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
        }

        case WM_CLOSE:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }
}

bool ShowExportImageDialog(HINSTANCE instance, HWND owner,
    int viewportWidth, int viewportHeight, ExportImageOptions& options)
{
    DialogState state;
    state.options = &options;
    state.viewportWidth = std::max(1, viewportWidth);
    state.viewportHeight = std::max(1, viewportHeight);

    INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_EXPORT_IMAGE),
        owner, ExportDialogProc, (LPARAM)&state);
    return result == IDOK;
}

const wchar_t* ExportFormatExtension(ExportFormat format)
{
    switch (format)
    {
    case ExportFormat::Jpeg: return L"jpg";
    case ExportFormat::Bmp:  return L"bmp";
    default:                 return L"png";
    }
}

std::wstring ExportFormatFilter(ExportFormat format)
{
    std::wstring description;
    std::wstring pattern;
    switch (format)
    {
    case ExportFormat::Jpeg: description = L"Imagem JPEG (*.jpg)"; pattern = L"*.jpg"; break;
    case ExportFormat::Bmp:  description = L"Imagem BMP (*.bmp)";   pattern = L"*.bmp"; break;
    default:                 description = L"Imagem PNG (*.png)";   pattern = L"*.png"; break;
    }

    std::wstring filter = description;
    filter.push_back(L'\0');
    filter += pattern;
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    return filter;
}

bool SaveImageToFile(const std::wstring& path, const ExportImageOptions& options,
    const std::vector<uint8_t>& bgra, int width, int height, std::wstring& outError)
{
    if (bgra.size() < (size_t)width * height * 4)
    {
        outError = L"Buffer de imagem incompleto.";
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))))
    {
        outError = L"Não foi possível inicializar o codificador de imagens do Windows.";
        return false;
    }

    GUID container = GUID_ContainerFormatPng;
    if (options.format == ExportFormat::Jpeg) container = GUID_ContainerFormatJpeg;
    else if (options.format == ExportFormat::Bmp) container = GUID_ContainerFormatBmp;

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
    {
        outError = L"Não foi possível criar o arquivo de saída.";
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(container, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
    {
        outError = L"Não foi possível inicializar o codificador.";
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> frameProperties;
    if (FAILED(encoder->CreateNewFrame(&frame, &frameProperties)))
    {
        outError = L"Não foi possível criar o quadro da imagem.";
        return false;
    }

    if (options.format == ExportFormat::Jpeg && frameProperties)
    {
        PROPBAG2 option = {};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = std::clamp(options.quality, 1, 100) / 100.0f;
        frameProperties->Write(1, &option, &value);
    }

    if (FAILED(frame->Initialize(frameProperties.Get())) ||
        FAILED(frame->SetSize((UINT)width, (UINT)height)))
    {
        outError = L"Não foi possível configurar o quadro da imagem.";
        return false;
    }

    // Só o PNG carrega alfa; nos outros pedimos BGR de 24 bits e deixamos o
    // WIC descartar o canal.
    const bool wantAlpha = (options.format == ExportFormat::Png) && options.transparent;
    WICPixelFormatGUID pixelFormat = wantAlpha
        ? GUID_WICPixelFormat32bppBGRA
        : GUID_WICPixelFormat24bppBGR;
    WICPixelFormatGUID negotiated = pixelFormat;
    if (FAILED(frame->SetPixelFormat(&negotiated)))
    {
        outError = L"Formato de pixel recusado pelo codificador.";
        return false;
    }

    const UINT sourceStride = (UINT)width * 4;
    HRESULT hr;
    if (IsEqualGUID(negotiated, GUID_WICPixelFormat32bppBGRA))
    {
        hr = frame->WritePixels((UINT)height, sourceStride,
            (UINT)bgra.size(), const_cast<BYTE*>(bgra.data()));
    }
    else
    {
        // Converte para 24 bpp achatando o alfa sobre branco, para a imagem
        // não sair com halo escuro nas bordas antisserrilhadas.
        const UINT stride24 = (UINT)((width * 3 + 3) & ~3);
        std::vector<uint8_t> rows((size_t)stride24 * height, 0);
        for (int y = 0; y < height; y++)
        {
            const uint8_t* src = bgra.data() + (size_t)y * sourceStride;
            uint8_t* dst = rows.data() + (size_t)y * stride24;
            for (int x = 0; x < width; x++)
            {
                const float alpha = src[x * 4 + 3] / 255.0f;
                for (int c = 0; c < 3; c++)
                    dst[x * 3 + c] = (uint8_t)(src[x * 4 + c] * alpha + 255.0f * (1.0f - alpha) + 0.5f);
            }
        }
        hr = frame->WritePixels((UINT)height, stride24, (UINT)rows.size(), rows.data());
    }

    if (FAILED(hr) || FAILED(frame->Commit()) || FAILED(encoder->Commit()))
    {
        outError = L"Falha ao gravar os pixels no arquivo.";
        return false;
    }
    return true;
}

bool CopyImageToClipboard(HWND owner, const std::vector<uint8_t>& bgra,
    int width, int height, bool keepAlpha)
{
    if (width <= 0 || height <= 0) return false;
    if (bgra.size() < (size_t)width * height * 4) return false;

    const size_t rowBytes = (size_t)width * 4;
    const size_t pixelBytes = rowBytes * height;
    const size_t totalBytes = sizeof(BITMAPV5HEADER) + pixelBytes;

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, totalBytes);
    if (!handle) return false;

    uint8_t* memory = (uint8_t*)GlobalLock(handle);
    if (!memory)
    {
        GlobalFree(handle);
        return false;
    }

    BITMAPV5HEADER header = {};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = width;
    header.bV5Height = height; // positivo = linhas de baixo para cima
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = (DWORD)pixelBytes;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = keepAlpha ? 0xFF000000 : 0;
    header.bV5CSType = LCS_WINDOWS_COLOR_SPACE;
    memcpy(memory, &header, sizeof(header));

    // O DIB é bottom-up, então as linhas entram na ordem inversa.
    uint8_t* pixels = memory + sizeof(BITMAPV5HEADER);
    for (int y = 0; y < height; y++)
    {
        const uint8_t* src = bgra.data() + (size_t)(height - 1 - y) * rowBytes;
        uint8_t* dst = pixels + (size_t)y * rowBytes;
        if (keepAlpha)
        {
            memcpy(dst, src, rowBytes);
        }
        else
        {
            // Sem alfa: achata sobre branco e marca tudo como opaco, senão
            // apps que respeitam o canal mostram a imagem furada.
            for (int x = 0; x < width; x++)
            {
                const float alpha = src[x * 4 + 3] / 255.0f;
                for (int c = 0; c < 3; c++)
                    dst[x * 4 + c] = (uint8_t)(src[x * 4 + c] * alpha + 255.0f * (1.0f - alpha) + 0.5f);
                dst[x * 4 + 3] = 255;
            }
        }
    }
    GlobalUnlock(handle);

    if (!OpenClipboard(owner))
    {
        GlobalFree(handle);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_DIBV5, handle))
    {
        CloseClipboard();
        GlobalFree(handle);
        return false;
    }
    CloseClipboard(); // a partir daqui o bloco pertence a area de transferencia
    return true;
}
