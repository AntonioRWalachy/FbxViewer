#pragma once
// Helpers compartilhados pelos loaders (PLY, glTF e o dispatcher). Sao poucas
// funcoes pequenas, entao ficam inline no header em vez de um .cpp proprio.
#include "SceneData.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace loaderutil
{

inline std::wstring Utf8ToWide(const char* s, int byteCount = -1)
{
    if (!s) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s, byteCount, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, byteCount, result.data(), len);
    if (byteCount == -1 && !result.empty() && result.back() == L'\0')
        result.pop_back();
    return result;
}

inline std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, result.data(), len, nullptr, nullptr);
    return result;
}

// Pasta do arquivo, com a barra final ("C:\a\b.ply" -> "C:\a\").
inline std::wstring DirectoryOf(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"" : path.substr(0, slash + 1);
}

inline std::wstring FileNameOf(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

inline bool FileExists(const std::wstring& path)
{
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Le o arquivo inteiro para memoria. Retorna false (com mensagem) em erro.
inline bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out, std::wstring& outError)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        outError = L"Não foi possível abrir o arquivo para leitura.";
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0)
    {
        CloseHandle(h);
        outError = L"Arquivo vazio ou ilegível.";
        return false;
    }
    if (size.QuadPart > (LONGLONG)1024 * 1024 * 1024)
    {
        CloseHandle(h);
        outError = L"Arquivo maior que 1 GB — não suportado.";
        return false;
    }

    out.resize((size_t)size.QuadPart);
    size_t done = 0;
    while (done < out.size())
    {
        DWORD chunk = (DWORD)std::min<size_t>(out.size() - done, 32u * 1024 * 1024);
        DWORD read = 0;
        if (!ReadFile(h, out.data() + done, chunk, &read, nullptr) || read == 0)
        {
            CloseHandle(h);
            outError = L"Falha ao ler o conteúdo do arquivo.";
            return false;
        }
        done += read;
    }
    CloseHandle(h);
    return true;
}

// ---------------------------------------------------------------------------
// Conversao de sistema de coordenadas.
//
// OBJ, PLY e glTF usam right-handed com Y para cima e +Z apontando para o
// observador. O renderer usa matrizes left-handed (XMMatrixLookAtLH /
// PerspectiveFovLH), entao os dados precisam ser espelhados em um eixo — sem
// isso a imagem sai invertida como num espelho e o backface culling descarta
// as faces erradas.
//
// Espelhamos X (e nao Z) porque isso mantem a "frente" do modelo em +Z, que e
// o mesmo resultado que o FBX SDK produz ao converter a cena para o sistema
// DirectX. Assim o mesmo modelo em .obj, .ply e .glb aparece identico.
//
// Como e um espelhamento, cada loader tambem inverte a ordem dos vertices dos
// triangulos que emite — senao o backface culling descarta o lado errado.
// ---------------------------------------------------------------------------
inline XMFLOAT3 RhToLh(float x, float y, float z) { return XMFLOAT3(-x, y, z); }
inline XMFLOAT3 RhToLh(const XMFLOAT3& v) { return XMFLOAT3(-v.x, v.y, v.z); }

// Acumulador de bounding box.
struct BoundsAccumulator
{
    XMFLOAT3 mn = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
    XMFLOAT3 mx = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool any = false;

    void Add(const XMFLOAT3& p)
    {
        any = true;
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    }

    void StoreInto(SceneModel& model) const
    {
        model.boundsMin = any ? mn : XMFLOAT3(0, 0, 0);
        model.boundsMax = any ? mx : XMFLOAT3(0, 0, 0);
    }
};

// Conta arestas unicas da topologia. A chave inclui o indice da malha porque
// malhas diferentes reusam os mesmos indices de vertice.
struct EdgeCounter
{
    std::set<std::pair<int, uint64_t>> edges;

    void AddTriangle(int meshIndex, uint32_t a, uint32_t b, uint32_t c)
    {
        Add(meshIndex, a, b);
        Add(meshIndex, b, c);
        Add(meshIndex, c, a);
    }

    void Add(int meshIndex, uint32_t a, uint32_t b)
    {
        if (a > b) std::swap(a, b);
        edges.insert({ meshIndex, ((uint64_t)a << 32) | (uint64_t)b });
    }

    UINT Count() const { return (UINT)edges.size(); }
};

// Preenche as estatisticas derivadas dos buffers ja montados.
inline void FinalizeStats(SceneModel& model, UINT edgeCount, UINT meshCount)
{
    model.stats.vertexCount = (UINT)model.vertices.size();
    model.stats.triangleCount = (UINT)(model.indices.size() / 3);
    model.stats.edgeCount = edgeCount;
    model.stats.meshCount = meshCount;
    model.stats.materialCount = (UINT)model.materials.size();
    model.stats.drawCallCount = (UINT)model.subMeshes.size();
}

} // namespace loaderutil
