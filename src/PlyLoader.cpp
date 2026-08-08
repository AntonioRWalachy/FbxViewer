#include "PlyLoader.h"
#include "LoaderUtils.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

using namespace loaderutil;

namespace
{
    // -----------------------------------------------------------------------
    // Tipos escalares do PLY
    // -----------------------------------------------------------------------
    enum class PlyType { Invalid, I8, U8, I16, U16, I32, U32, F32, F64 };

    size_t TypeSize(PlyType t)
    {
        switch (t)
        {
        case PlyType::I8:  case PlyType::U8:  return 1;
        case PlyType::I16: case PlyType::U16: return 2;
        case PlyType::I32: case PlyType::U32: case PlyType::F32: return 4;
        case PlyType::F64: return 8;
        default: return 0;
        }
    }

    PlyType ParseType(const std::string& s)
    {
        if (s == "char" || s == "int8") return PlyType::I8;
        if (s == "uchar" || s == "uint8") return PlyType::U8;
        if (s == "short" || s == "int16") return PlyType::I16;
        if (s == "ushort" || s == "uint16") return PlyType::U16;
        if (s == "int" || s == "int32") return PlyType::I32;
        if (s == "uint" || s == "uint32") return PlyType::U32;
        if (s == "float" || s == "float32") return PlyType::F32;
        if (s == "double" || s == "float64") return PlyType::F64;
        return PlyType::Invalid;
    }

    struct PlyProperty
    {
        std::string name;
        bool isList = false;
        PlyType countType = PlyType::Invalid; // so para listas
        PlyType type = PlyType::Invalid;
    };

    struct PlyElement
    {
        std::string name;
        size_t count = 0;
        std::vector<PlyProperty> properties;
    };

    enum class PlyFormat { Ascii, BinaryLittle, BinaryBig };

    // -----------------------------------------------------------------------
    // Leitor unificado do corpo do arquivo: le valores como double e converte
    // depois. O PLY nunca tem inteiros grandes o bastante para perder precisao
    // em double (o maior e uint32).
    // -----------------------------------------------------------------------
    class BodyReader
    {
    public:
        BodyReader(const uint8_t* begin, const uint8_t* end, PlyFormat format)
            : m_cur(begin), m_end(end), m_format(format) {}

        bool ReadValue(PlyType type, double& out)
        {
            if (m_format == PlyFormat::Ascii)
                return ReadAscii(out);
            return ReadBinary(type, out);
        }

    private:
        bool ReadAscii(double& out)
        {
            // Avanca ate o proximo token
            while (m_cur < m_end && (*m_cur == ' ' || *m_cur == '\t' || *m_cur == '\r' || *m_cur == '\n'))
                m_cur++;
            if (m_cur >= m_end) return false;

            const char* start = (const char*)m_cur;
            char* stop = nullptr;
            out = strtod(start, &stop);
            if (stop == start) return false;
            m_cur = (const uint8_t*)stop;
            return true;
        }

        bool ReadBinary(PlyType type, double& out)
        {
            size_t size = TypeSize(type);
            if (size == 0 || (size_t)(m_end - m_cur) < size) return false;

            uint8_t raw[8];
            memcpy(raw, m_cur, size);
            m_cur += size;

            if (m_format == PlyFormat::BinaryBig)
                for (size_t i = 0; i < size / 2; i++)
                    std::swap(raw[i], raw[size - 1 - i]);

            switch (type)
            {
            case PlyType::I8:  { int8_t v;   memcpy(&v, raw, 1); out = v; break; }
            case PlyType::U8:  { uint8_t v;  memcpy(&v, raw, 1); out = v; break; }
            case PlyType::I16: { int16_t v;  memcpy(&v, raw, 2); out = v; break; }
            case PlyType::U16: { uint16_t v; memcpy(&v, raw, 2); out = v; break; }
            case PlyType::I32: { int32_t v;  memcpy(&v, raw, 4); out = v; break; }
            case PlyType::U32: { uint32_t v; memcpy(&v, raw, 4); out = v; break; }
            case PlyType::F32: { float v;    memcpy(&v, raw, 4); out = v; break; }
            case PlyType::F64: { double v;   memcpy(&v, raw, 8); out = v; break; }
            default: return false;
            }
            return true;
        }

        const uint8_t* m_cur;
        const uint8_t* m_end;
        PlyFormat m_format;
    };

    // Vertice cru lido do arquivo, ainda no sistema de coordenadas de origem.
    struct RawVertex
    {
        float x = 0, y = 0, z = 0;
        float nx = 0, ny = 0, nz = 0;
        float u = 0, v = 0;
        uint32_t color = 0xFFFFFFFFu;
    };

    struct RawFace
    {
        std::vector<uint32_t> indices;
        std::vector<float> texcoords; // opcional: 2 floats por canto
    };

    // Divide uma linha do header em tokens separados por espaco.
    std::vector<std::string> SplitTokens(const std::string& line)
    {
        std::vector<std::string> tokens;
        size_t i = 0;
        while (i < line.size())
        {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
            size_t start = i;
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') i++;
            if (i > start) tokens.push_back(line.substr(start, i - start));
        }
        return tokens;
    }

    // Nomes alternativos usados por exportadores diferentes para a mesma coisa.
    bool IsUName(const std::string& n) { return n == "s" || n == "u" || n == "texture_u" || n == "texture_s"; }
    bool IsVName(const std::string& n) { return n == "t" || n == "v" || n == "texture_v" || n == "texture_t"; }
}

bool LoadPlyFile(const std::wstring& filePath, SceneModel& outModel, std::wstring& outError)
{
    std::vector<uint8_t> data;
    if (!ReadWholeFile(filePath, data, outError))
        return false;

    // ---------------------------------------------------------------------
    // 1) Header (sempre em ASCII, ate a linha "end_header")
    // ---------------------------------------------------------------------
    const char* text = (const char*)data.data();
    size_t size = data.size();

    size_t pos = 0;
    auto readLine = [&](std::string& out) -> bool
    {
        if (pos >= size) return false;
        size_t start = pos;
        while (pos < size && text[pos] != '\n') pos++;
        size_t end = pos;
        if (end > start && text[end - 1] == '\r') end--;
        out.assign(text + start, end - start);
        if (pos < size) pos++; // consome o '\n'
        return true;
    };

    std::string line;
    if (!readLine(line) || SplitTokens(line).empty() || SplitTokens(line)[0] != "ply")
    {
        outError = L"Arquivo não começa com a assinatura \"ply\".";
        return false;
    }

    PlyFormat format = PlyFormat::Ascii;
    bool formatFound = false;
    bool headerEnded = false;
    std::vector<PlyElement> elements;
    std::wstring textureFromComment;

    while (readLine(line))
    {
        std::vector<std::string> tok = SplitTokens(line);
        if (tok.empty()) continue;

        if (tok[0] == "format" && tok.size() >= 2)
        {
            if (tok[1] == "ascii") format = PlyFormat::Ascii;
            else if (tok[1] == "binary_little_endian") format = PlyFormat::BinaryLittle;
            else if (tok[1] == "binary_big_endian") format = PlyFormat::BinaryBig;
            else
            {
                outError = L"Formato PLY desconhecido: " + Utf8ToWide(tok[1].c_str());
                return false;
            }
            formatFound = true;
        }
        else if (tok[0] == "comment" && tok.size() >= 3)
        {
            // "comment TextureFile atlas.png" — convencao do MeshLab e cia.
            std::string key = tok[1];
            for (char& c : key) c = (char)tolower((unsigned char)c);
            if (key == "texturefile")
            {
                // O nome pode conter espacos: junta o resto da linha.
                std::string name = tok[2];
                for (size_t i = 3; i < tok.size(); i++) name += " " + tok[i];
                textureFromComment = Utf8ToWide(name.c_str());
            }
        }
        else if (tok[0] == "element" && tok.size() >= 3)
        {
            PlyElement e;
            e.name = tok[1];
            e.count = (size_t)strtoull(tok[2].c_str(), nullptr, 10);
            elements.push_back(e);
        }
        else if (tok[0] == "property" && !elements.empty())
        {
            PlyProperty p;
            if (tok.size() >= 5 && tok[1] == "list")
            {
                p.isList = true;
                p.countType = ParseType(tok[2]);
                p.type = ParseType(tok[3]);
                p.name = tok[4];
            }
            else if (tok.size() >= 3)
            {
                p.type = ParseType(tok[1]);
                p.name = tok[2];
            }
            else
            {
                continue;
            }
            if (p.type == PlyType::Invalid || (p.isList && p.countType == PlyType::Invalid))
            {
                outError = L"Tipo de propriedade PLY não suportado na linha: "
                    + Utf8ToWide(line.c_str());
                return false;
            }
            elements.back().properties.push_back(p);
        }
        else if (tok[0] == "end_header")
        {
            headerEnded = true;
            break;
        }
    }

    if (!headerEnded || !formatFound)
    {
        outError = L"Header do PLY incompleto (sem \"format\" ou \"end_header\").";
        return false;
    }

    // ---------------------------------------------------------------------
    // 2) Corpo
    // ---------------------------------------------------------------------
    BodyReader reader(data.data() + pos, data.data() + size, format);

    std::vector<RawVertex> vertices;
    std::vector<RawFace> faces;
    bool hasNormals = false;
    bool hasUvs = false;
    bool hasColors = false;

    for (const PlyElement& element : elements)
    {
        const bool isVertex = (element.name == "vertex");
        const bool isFace = (element.name == "face");

        if (isVertex) vertices.reserve(element.count);
        if (isFace) faces.reserve(element.count);

        for (size_t i = 0; i < element.count; i++)
        {
            RawVertex rv;
            RawFace rf;

            for (const PlyProperty& prop : element.properties)
            {
                if (prop.isList)
                {
                    double countValue = 0;
                    if (!reader.ReadValue(prop.countType, countValue))
                    {
                        outError = L"Fim inesperado do arquivo lendo a lista \""
                            + Utf8ToWide(prop.name.c_str()) + L"\".";
                        return false;
                    }
                    size_t n = (size_t)(countValue < 0 ? 0 : countValue);

                    const bool wantIndices = isFace &&
                        (prop.name == "vertex_indices" || prop.name == "vertex_index");
                    const bool wantTexcoords = isFace &&
                        (prop.name == "texcoord" || prop.name == "texcoords");

                    for (size_t k = 0; k < n; k++)
                    {
                        double value = 0;
                        if (!reader.ReadValue(prop.type, value))
                        {
                            outError = L"Fim inesperado do arquivo lendo os dados do corpo.";
                            return false;
                        }
                        if (wantIndices) rf.indices.push_back((uint32_t)(value < 0 ? 0 : value));
                        else if (wantTexcoords) rf.texcoords.push_back((float)value);
                    }
                }
                else
                {
                    double value = 0;
                    if (!reader.ReadValue(prop.type, value))
                    {
                        outError = L"Fim inesperado do arquivo lendo os dados do corpo.";
                        return false;
                    }
                    if (!isVertex) continue;

                    const std::string& n = prop.name;
                    // Cores podem vir como uchar 0..255 ou float 0..1.
                    auto colorChannel = [&](double v) -> uint32_t
                    {
                        double scaled = (prop.type == PlyType::F32 || prop.type == PlyType::F64)
                            ? v * 255.0 : v;
                        if (scaled < 0) scaled = 0;
                        if (scaled > 255) scaled = 255;
                        return (uint32_t)(scaled + 0.5);
                    };

                    if (n == "x") rv.x = (float)value;
                    else if (n == "y") rv.y = (float)value;
                    else if (n == "z") rv.z = (float)value;
                    else if (n == "nx") { rv.nx = (float)value; hasNormals = true; }
                    else if (n == "ny") rv.ny = (float)value;
                    else if (n == "nz") rv.nz = (float)value;
                    else if (IsUName(n)) { rv.u = (float)value; hasUvs = true; }
                    else if (IsVName(n)) rv.v = (float)value;
                    else if (n == "red" || n == "r")
                    {
                        rv.color = (rv.color & 0xFFFFFF00u) | colorChannel(value);
                        hasColors = true;
                    }
                    else if (n == "green" || n == "g")
                        rv.color = (rv.color & 0xFFFF00FFu) | (colorChannel(value) << 8);
                    else if (n == "blue" || n == "b")
                        rv.color = (rv.color & 0xFF00FFFFu) | (colorChannel(value) << 16);
                    else if (n == "alpha" || n == "a")
                        rv.color = (rv.color & 0x00FFFFFFu) | (colorChannel(value) << 24);
                }
            }

            if (isVertex) vertices.push_back(rv);
            else if (isFace && rf.indices.size() >= 3) faces.push_back(std::move(rf));
        }
    }

    if (vertices.empty())
    {
        outError = L"Nenhum vértice encontrado no arquivo PLY.";
        return false;
    }
    if (faces.empty())
    {
        outError = L"O arquivo PLY não tem faces (nuvens de pontos não são suportadas).";
        return false;
    }

    // ---------------------------------------------------------------------
    // 3) Monta o SceneModel
    // ---------------------------------------------------------------------
    // Quando as UVs vem por canto de face (convencao do MeshLab), os vertices
    // precisam ser duplicados; caso contrario reaproveitamos o array indexado.
    bool faceTexcoords = false;
    for (const RawFace& f : faces)
        if (f.texcoords.size() >= f.indices.size() * 2) { faceTexcoords = true; break; }

    BoundsAccumulator bounds;
    EdgeCounter edgeCounter;

    // As UVs do PLY seguem a convencao OpenGL (v=0 embaixo); o DirectX usa
    // v=0 no topo, entao invertemos. Sem UVs no arquivo deixamos (0,0) — a
    // inversao transformaria em (0,1) e daria a impressao de UV valida.
    const bool anyUv = hasUvs || faceTexcoords;
    auto makeVertex = [&](const RawVertex& rv, float u, float v) -> Vertex
    {
        Vertex out;
        out.position = RhToLh(rv.x, rv.y, rv.z);
        out.normal = RhToLh(rv.nx, rv.ny, rv.nz);
        out.uv = anyUv ? XMFLOAT2(u, 1.0f - v) : XMFLOAT2(0.0f, 0.0f);
        out.color = rv.color;
        bounds.Add(out.position);
        return out;
    };

    if (faceTexcoords)
    {
        for (const RawFace& f : faces)
        {
            size_t corners = f.indices.size();
            // Leque de triangulos a partir do primeiro canto
            for (size_t k = 1; k + 1 < corners; k++)
            {
                const size_t corner[3] = { 0, k, k + 1 };
                uint32_t tri[3];
                for (int c = 0; c < 3; c++)
                {
                    uint32_t vi = f.indices[corner[c]];
                    if (vi >= vertices.size())
                    {
                        outError = L"Índice de vértice fora do intervalo no PLY.";
                        return false;
                    }
                    float u = 0, v = 0;
                    if (f.texcoords.size() >= (corner[c] + 1) * 2)
                    {
                        u = f.texcoords[corner[c] * 2];
                        v = f.texcoords[corner[c] * 2 + 1];
                    }
                    tri[c] = (uint32_t)outModel.vertices.size();
                    outModel.vertices.push_back(makeVertex(vertices[vi], u, v));
                }
                edgeCounter.AddTriangle(0, f.indices[corner[0]], f.indices[corner[1]], f.indices[corner[2]]);
                // Winding invertido por causa do espelhamento de eixo (RhToLh)
                outModel.indices.push_back(tri[0]);
                outModel.indices.push_back(tri[2]);
                outModel.indices.push_back(tri[1]);
            }
        }
        hasUvs = true;
    }
    else
    {
        outModel.vertices.reserve(vertices.size());
        for (const RawVertex& rv : vertices)
            outModel.vertices.push_back(makeVertex(rv, rv.u, rv.v));

        for (const RawFace& f : faces)
        {
            size_t corners = f.indices.size();
            for (size_t k = 1; k + 1 < corners; k++)
            {
                uint32_t a = f.indices[0], b = f.indices[k], c = f.indices[k + 1];
                if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
                {
                    outError = L"Índice de vértice fora do intervalo no PLY.";
                    return false;
                }
                edgeCounter.AddTriangle(0, a, b, c);
                outModel.indices.push_back(a);
                outModel.indices.push_back(c);
                outModel.indices.push_back(b);
            }
        }
    }

    if (outModel.indices.empty())
    {
        outError = L"O arquivo PLY não gerou nenhum triângulo.";
        return false;
    }

    // Sem normais no arquivo: calcula normais suaves somando as normais das
    // faces adjacentes (o mesmo que o "shade smooth" do Blender).
    if (!hasNormals)
    {
        std::vector<XMFLOAT3> accum(outModel.vertices.size(), XMFLOAT3(0, 0, 0));
        for (size_t i = 0; i + 2 < outModel.indices.size(); i += 3)
        {
            uint32_t ia = outModel.indices[i], ib = outModel.indices[i + 1], ic = outModel.indices[i + 2];
            XMVECTOR pa = XMLoadFloat3(&outModel.vertices[ia].position);
            XMVECTOR pb = XMLoadFloat3(&outModel.vertices[ib].position);
            XMVECTOR pc = XMLoadFloat3(&outModel.vertices[ic].position);
            XMVECTOR n = XMVector3Cross(XMVectorSubtract(pb, pa), XMVectorSubtract(pc, pa));
            XMFLOAT3 nf;
            XMStoreFloat3(&nf, n);
            for (uint32_t idx : { ia, ib, ic })
            {
                accum[idx].x += nf.x;
                accum[idx].y += nf.y;
                accum[idx].z += nf.z;
            }
        }
        for (size_t i = 0; i < outModel.vertices.size(); i++)
        {
            XMVECTOR n = XMLoadFloat3(&accum[i]);
            if (XMVectorGetX(XMVector3LengthSq(n)) < 1e-20f)
                n = XMVectorSet(0, 1, 0, 0);
            XMStoreFloat3(&outModel.vertices[i].normal, XMVector3Normalize(n));
        }
    }

    bounds.StoreInto(outModel);
    outModel.hasTexCoords = anyUv;

    // ---------------------------------------------------------------------
    // 4) Material unico (o PLY nao tem materiais; so a textura do comentario)
    // ---------------------------------------------------------------------
    MaterialData material;
    material.name = "PLY";
    if (hasColors)
        material.diffuseColor = XMFLOAT4(1, 1, 1, 1); // a cor vem dos vertices

    if (!textureFromComment.empty() && hasUvs)
    {
        std::wstring baseDir = DirectoryOf(filePath);
        const std::wstring candidates[] = {
            textureFromComment,
            baseDir + textureFromComment,
            baseDir + FileNameOf(textureFromComment),
            baseDir + L"textures\\" + FileNameOf(textureFromComment),
        };
        for (const std::wstring& c : candidates)
        {
            if (FileExists(c))
            {
                material.texturePath = c;
                material.textureName = FileNameOf(c);
                material.diffuseColor = XMFLOAT4(1, 1, 1, 1);
                break;
            }
        }
    }
    outModel.materials.push_back(material);

    SubMesh sub;
    sub.indexStart = 0;
    sub.indexCount = (UINT)outModel.indices.size();
    sub.materialIndex = 0;
    outModel.subMeshes.push_back(sub);

    FinalizeStats(outModel, edgeCounter.Count(), 1);
    return true;
}
