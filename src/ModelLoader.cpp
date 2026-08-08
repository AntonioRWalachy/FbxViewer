#include "ModelLoader.h"
#include "FbxLoader.h"
#include "GltfLoader.h"
#include "PlyLoader.h"

#include <algorithm>

// Extensoes aceitas. As cinco primeiras passam pelo FBX SDK (que tambem le
// OBJ, Collada, 3DS e DXF); as outras tem loader proprio.
const wchar_t* const kSupportedExtensions[] = {
    L"fbx", L"obj", L"dae", L"3ds", L"dxf",
    L"ply", L"glb", L"gltf",
};
const int kSupportedExtensionCount = (int)(sizeof(kSupportedExtensions) / sizeof(kSupportedExtensions[0]));

std::wstring GetFileExtensionLower(const std::wstring& filePath)
{
    size_t dot = filePath.find_last_of(L'.');
    size_t slash = filePath.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return L"";

    std::wstring ext = filePath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](wchar_t c) { return (wchar_t)towlower(c); });
    return ext;
}

bool IsSupportedModelExtension(const std::wstring& extLower)
{
    for (int i = 0; i < kSupportedExtensionCount; i++)
        if (extLower == kSupportedExtensions[i])
            return true;
    return false;
}

std::wstring BuildOpenDialogFilter()
{
    // O OPENFILENAME espera pares "descricao\0padrao\0", terminados por outro
    // '\0'. Montamos a lista a partir de kSupportedExtensions para que ela
    // nunca fique fora de sincronia com o que os loaders realmente abrem.
    std::wstring allPatterns;
    for (int i = 0; i < kSupportedExtensionCount; i++)
    {
        if (i > 0) allPatterns += L";";
        allPatterns += L"*." + std::wstring(kSupportedExtensions[i]);
    }

    std::wstring filter;
    auto append = [&filter](const std::wstring& description, const std::wstring& pattern)
    {
        filter += description;
        filter.push_back(L'\0');
        filter += pattern;
        filter.push_back(L'\0');
    };

    append(L"Modelos 3D (" + allPatterns + L")", allPatterns);
    append(L"Autodesk FBX (*.fbx)", L"*.fbx");
    append(L"Wavefront OBJ (*.obj)", L"*.obj");
    append(L"glTF 2.0 (*.glb;*.gltf)", L"*.glb;*.gltf");
    append(L"Stanford PLY (*.ply)", L"*.ply");
    append(L"Collada / 3DS / DXF (*.dae;*.3ds;*.dxf)", L"*.dae;*.3ds;*.dxf");
    append(L"Todos os arquivos (*.*)", L"*.*");
    filter.push_back(L'\0');
    return filter;
}

bool LoadModelFile(const std::wstring& filePath, SceneModel& outModel, std::wstring& outError)
{
    std::wstring ext = GetFileExtensionLower(filePath);

    if (ext == L"ply")
        return LoadPlyFile(filePath, outModel, outError);
    if (ext == L"glb" || ext == L"gltf")
        return LoadGltfFile(filePath, outModel, outError);

    // FBX, OBJ, DAE, 3DS, DXF — e tambem o fallback para extensoes
    // desconhecidas, ja que o importador do SDK detecta o formato pelo
    // conteudo do arquivo.
    return LoadFbxFile(filePath, outModel, outError);
}
