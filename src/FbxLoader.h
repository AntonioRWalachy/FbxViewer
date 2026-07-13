#pragma once
#include "SceneData.h"
#include <memory>

// Carrega um arquivo .fbx (ou .obj, o FBX SDK le ambos) para um SceneModel.
// Retorna true em caso de sucesso. Em caso de erro, outError recebe a mensagem.
bool LoadFbxFile(const std::wstring& filePath, SceneModel& outModel, std::wstring& outError);
