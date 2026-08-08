#pragma once
#include "SceneData.h"
#include <string>

// Carrega glTF 2.0 nos dois empacotamentos: .glb (binario, com a textura
// embutida) e .gltf (JSON, com buffers/imagens externos ou em data URI).
bool LoadGltfFile(const std::wstring& filePath, SceneModel& outModel, std::wstring& outError);
