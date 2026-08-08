#pragma once
#include "SceneData.h"
#include <string>

// ---------------------------------------------------------------------------
// Ponto de entrada unico para carregar qualquer formato suportado. Despacha
// pela extensao do arquivo:
//   .fbx .obj .dae .3ds .dxf  -> FbxLoader   (Autodesk FBX SDK)
//   .ply                      -> PlyLoader   (ascii + binario LE/BE)
//   .glb .gltf                -> GltfLoader  (glTF 2.0)
// ---------------------------------------------------------------------------
bool LoadModelFile(const std::wstring& filePath, SceneModel& outModel, std::wstring& outError);

// Extensao do caminho em minusculas e sem ponto ("cubo.GLB" -> "glb").
std::wstring GetFileExtensionLower(const std::wstring& filePath);

// true quando a extensao (minuscula, sem ponto) esta na lista suportada.
bool IsSupportedModelExtension(const std::wstring& extLower);

// Lista de extensoes suportadas, sem ponto. Usada pelo filtro do dialogo de
// abrir e pelo registro de associacoes de arquivo, para que as duas coisas
// nunca saiam de sincronia.
extern const wchar_t* const kSupportedExtensions[];
extern const int kSupportedExtensionCount;

// Filtro pronto para o OPENFILENAME (string com '\0' embutidos).
std::wstring BuildOpenDialogFilter();
