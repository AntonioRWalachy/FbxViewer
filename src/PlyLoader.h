#pragma once
#include "SceneData.h"
#include <string>

// Carrega um arquivo .ply (Stanford Polygon Format) nos tres formatos do
// padrao: ascii, binary_little_endian e binary_big_endian.
bool LoadPlyFile(const std::wstring& filePath, SceneModel& outModel, std::wstring& outError);
