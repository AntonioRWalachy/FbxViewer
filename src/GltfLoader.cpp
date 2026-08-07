#include "GltfLoader.h"
#include "Json.h"
#include "LoaderUtils.h"

#include <cstring>
#include <unordered_map>

using namespace loaderutil;

namespace
{
    // Component types do glTF (secao 5.1.1 da especificacao)
    constexpr int kByte = 5120;
    constexpr int kUnsignedByte = 5121;
    constexpr int kShort = 5122;
    constexpr int kUnsignedShort = 5123;
    constexpr int kUnsignedInt = 5125;
    constexpr int kFloat = 5126;

    // Primitive modes
    constexpr int kModeTriangles = 4;
    constexpr int kModeTriangleStrip = 5;
    constexpr int kModeTriangleFan = 6;

    size_t ComponentSize(int componentType)
    {
        switch (componentType)
        {
        case kByte: case kUnsignedByte: return 1;
        case kShort: case kUnsignedShort: return 2;
        case kUnsignedInt: case kFloat: return 4;
        default: return 0;
        }
    }

    int ComponentCount(const std::string& type)
    {
        if (type == "SCALAR") return 1;
        if (type == "VEC2") return 2;
        if (type == "VEC3") return 3;
        if (type == "VEC4") return 4;
        if (type == "MAT2") return 4;
        if (type == "MAT3") return 9;
        if (type == "MAT4") return 16;
        return 0;
    }

    // -------------------------------------------------------------------
    // base64 (data URIs)
    // -------------------------------------------------------------------
    int Base64Digit(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    bool Base64Decode(const std::string& in, std::vector<uint8_t>& out)
    {
        out.clear();
        out.reserve(in.size() * 3 / 4 + 3);
        uint32_t accum = 0;
        int bits = 0;
        for (char c : in)
        {
            if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
            int digit = Base64Digit(c);
            if (digit < 0) return false;
            accum = (accum << 6) | (uint32_t)digit;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back((uint8_t)((accum >> bits) & 0xFF));
            }
        }
        return true;
    }

    // Decodifica escapes de URI ("%20" -> " ") e troca '/' por '\' para o
    // caminho local.
    std::string UriDecode(const std::string& uri)
    {
        std::string out;
        out.reserve(uri.size());
        for (size_t i = 0; i < uri.size(); i++)
        {
            if (uri[i] == '%' && i + 2 < uri.size())
            {
                auto hex = [](char c) -> int
                {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                int hi = hex(uri[i + 1]), lo = hex(uri[i + 2]);
                if (hi >= 0 && lo >= 0)
                {
                    out += (char)((hi << 4) | lo);
                    i += 2;
                    continue;
                }
            }
            out += uri[i];
        }
        return out;
    }

    bool IsDataUri(const std::string& uri)
    {
        return uri.compare(0, 5, "data:") == 0;
    }

    // "data:image/png;base64,AAAA..." -> bytes
    bool DecodeDataUri(const std::string& uri, std::vector<uint8_t>& out)
    {
        size_t comma = uri.find(',');
        if (comma == std::string::npos) return false;
        std::string header = uri.substr(0, comma);
        std::string payload = uri.substr(comma + 1);
        if (header.find(";base64") == std::string::npos)
        {
            // data URI sem base64: percent-encoded
            std::string decoded = UriDecode(payload);
            out.assign(decoded.begin(), decoded.end());
            return true;
        }
        return Base64Decode(payload, out);
    }

    // -------------------------------------------------------------------
    // Estado do carregamento
    // -------------------------------------------------------------------
    struct GltfContext
    {
        const minijson::Value* root = nullptr;
        std::vector<std::vector<uint8_t>> buffers;
        std::wstring baseDir;
        std::wstring error;

        const minijson::Value* Array(const char* name) const
        {
            const minijson::Value* v = root->Find(name);
            return (v && v->IsArray()) ? v : nullptr;
        }

        const minijson::Value* ItemAt(const char* arrayName, int index) const
        {
            const minijson::Value* arr = Array(arrayName);
            if (!arr || index < 0 || (size_t)index >= arr->Size()) return nullptr;
            return arr->At((size_t)index);
        }

        // Le um accessor como floats, sempre com "components" componentes por
        // elemento (preenchendo com 0 se o accessor tiver menos).
        bool ReadFloats(int accessorIndex, int components, std::vector<float>& out)
        {
            out.clear();
            const minijson::Value* accessor = ItemAt("accessors", accessorIndex);
            if (!accessor)
            {
                error = L"Accessor inexistente referenciado pela malha.";
                return false;
            }

            int componentType = accessor->IntAt("componentType", 0);
            size_t count = (size_t)accessor->IntAt("count", 0);
            std::string type = accessor->StringAt("type");
            bool normalized = false;
            if (const minijson::Value* n = accessor->Find("normalized")) normalized = n->Bool();

            int accComponents = ComponentCount(type);
            size_t compSize = ComponentSize(componentType);
            if (accComponents == 0 || compSize == 0)
            {
                error = L"Accessor com tipo de componente não suportado.";
                return false;
            }

            out.assign(count * (size_t)components, 0.0f);
            if (count == 0) return true;

            int viewIndex = accessor->IntAt("bufferView", -1);
            if (viewIndex < 0)
                return true; // accessor sem bufferView = tudo zero (valido no glTF)

            const minijson::Value* view = ItemAt("bufferViews", viewIndex);
            if (!view)
            {
                error = L"bufferView inexistente referenciado por um accessor.";
                return false;
            }

            int bufferIndex = view->IntAt("buffer", -1);
            if (bufferIndex < 0 || (size_t)bufferIndex >= buffers.size())
            {
                error = L"Buffer inexistente referenciado por um bufferView.";
                return false;
            }
            const std::vector<uint8_t>& buffer = buffers[(size_t)bufferIndex];

            size_t viewOffset = (size_t)view->NumberAt("byteOffset", 0);
            size_t viewLength = (size_t)view->NumberAt("byteLength", 0);
            size_t stride = (size_t)view->NumberAt("byteStride", 0);
            size_t accOffset = (size_t)accessor->NumberAt("byteOffset", 0);

            size_t elementSize = compSize * (size_t)accComponents;
            if (stride == 0) stride = elementSize;

            if (viewOffset > buffer.size() || viewLength > buffer.size() - viewOffset)
            {
                error = L"bufferView aponta para fora do buffer.";
                return false;
            }
            const uint8_t* base = buffer.data() + viewOffset + accOffset;
            size_t available = viewLength - std::min(viewLength, accOffset);
            if (count > 0 && (count - 1) * stride + elementSize > available)
            {
                error = L"Accessor aponta para fora do bufferView.";
                return false;
            }

            for (size_t i = 0; i < count; i++)
            {
                const uint8_t* element = base + i * stride;
                int copyCount = std::min(components, accComponents);
                for (int c = 0; c < copyCount; c++)
                {
                    const uint8_t* p = element + (size_t)c * compSize;
                    float value = 0.0f;
                    switch (componentType)
                    {
                    case kFloat:         { float v;    memcpy(&v, p, 4); value = v; break; }
                    case kByte:          { int8_t v;   memcpy(&v, p, 1); value = normalized ? std::max(v / 127.0f, -1.0f) : (float)v; break; }
                    case kUnsignedByte:  { uint8_t v;  memcpy(&v, p, 1); value = normalized ? v / 255.0f : (float)v; break; }
                    case kShort:         { int16_t v;  memcpy(&v, p, 2); value = normalized ? std::max(v / 32767.0f, -1.0f) : (float)v; break; }
                    case kUnsignedShort: { uint16_t v; memcpy(&v, p, 2); value = normalized ? v / 65535.0f : (float)v; break; }
                    case kUnsignedInt:   { uint32_t v; memcpy(&v, p, 4); value = (float)v; break; }
                    default: break;
                    }
                    out[i * (size_t)components + (size_t)c] = value;
                }
            }
            return true;
        }

        bool ReadIndices(int accessorIndex, std::vector<uint32_t>& out)
        {
            out.clear();
            std::vector<float> asFloats;
            const minijson::Value* accessor = ItemAt("accessors", accessorIndex);
            if (!accessor)
            {
                error = L"Accessor de indices inexistente.";
                return false;
            }
            // Indices podem ser UNSIGNED_INT, que nao cabe exato em float —
            // le como floats so quando o tipo e pequeno, senao le direto.
            int componentType = accessor->IntAt("componentType", 0);
            if (componentType != kUnsignedInt)
            {
                if (!ReadFloats(accessorIndex, 1, asFloats)) return false;
                out.reserve(asFloats.size());
                for (float f : asFloats) out.push_back((uint32_t)(f < 0 ? 0 : f));
                return true;
            }

            size_t count = (size_t)accessor->IntAt("count", 0);
            int viewIndex = accessor->IntAt("bufferView", -1);
            const minijson::Value* view = ItemAt("bufferViews", viewIndex);
            if (!view)
            {
                error = L"bufferView de indices inexistente.";
                return false;
            }
            int bufferIndex = view->IntAt("buffer", -1);
            if (bufferIndex < 0 || (size_t)bufferIndex >= buffers.size())
            {
                error = L"Buffer de indices inexistente.";
                return false;
            }
            const std::vector<uint8_t>& buffer = buffers[(size_t)bufferIndex];
            size_t offset = (size_t)view->NumberAt("byteOffset", 0) + (size_t)accessor->NumberAt("byteOffset", 0);
            size_t stride = (size_t)view->NumberAt("byteStride", 0);
            if (stride == 0) stride = 4;

            if (count > 0 && (offset + (count - 1) * stride + 4 > buffer.size()))
            {
                error = L"Accessor de indices aponta para fora do buffer.";
                return false;
            }
            out.resize(count);
            for (size_t i = 0; i < count; i++)
                memcpy(&out[i], buffer.data() + offset + i * stride, 4);
            return true;
        }
    };

    // Matriz local de um node: "matrix" explicita ou a composicao T * R * S.
    XMMATRIX NodeLocalMatrix(const minijson::Value& node)
    {
        if (const minijson::Value* m = node.Find("matrix"))
        {
            if (m->IsArray() && m->Size() == 16)
            {
                float f[16];
                for (int i = 0; i < 16; i++)
                    f[i] = (float)m->At((size_t)i)->Number(i % 5 == 0 ? 1.0 : 0.0);
                // glTF grava a matriz por colunas; o DirectXMath usa vetores-
                // linha, entao preencher as linhas na ordem do array ja produz
                // a transposta necessaria.
                return XMMATRIX(f[0], f[1], f[2], f[3],
                                f[4], f[5], f[6], f[7],
                                f[8], f[9], f[10], f[11],
                                f[12], f[13], f[14], f[15]);
            }
        }

        XMVECTOR scale = XMVectorSet(1, 1, 1, 0);
        XMVECTOR rotation = XMVectorSet(0, 0, 0, 1);
        XMVECTOR translation = XMVectorSet(0, 0, 0, 0);

        if (const minijson::Value* s = node.Find("scale"))
            if (s->IsArray() && s->Size() >= 3)
                scale = XMVectorSet((float)s->At(0)->Number(1), (float)s->At(1)->Number(1), (float)s->At(2)->Number(1), 0);
        if (const minijson::Value* r = node.Find("rotation"))
            if (r->IsArray() && r->Size() >= 4)
                rotation = XMVectorSet((float)r->At(0)->Number(0), (float)r->At(1)->Number(0),
                                       (float)r->At(2)->Number(0), (float)r->At(3)->Number(1));
        if (const minijson::Value* t = node.Find("translation"))
            if (t->IsArray() && t->Size() >= 3)
                translation = XMVectorSet((float)t->At(0)->Number(0), (float)t->At(1)->Number(0), (float)t->At(2)->Number(0), 0);

        return XMMatrixScalingFromVector(scale)
             * XMMatrixRotationQuaternion(XMVector4Normalize(rotation))
             * XMMatrixTranslationFromVector(translation);
    }

    // Resolve a imagem de um material (bufferView embutido, data URI ou
    // arquivo externo ao lado do .gltf).
    void ResolveImage(GltfContext& ctx, int imageIndex, MaterialData& material)
    {
        const minijson::Value* image = ctx.ItemAt("images", imageIndex);
        if (!image) return;

        std::string name = image->StringAt("name");
        std::string uri = image->StringAt("uri");

        if (!uri.empty())
        {
            if (IsDataUri(uri))
            {
                std::vector<uint8_t> bytes;
                if (DecodeDataUri(uri, bytes) && !bytes.empty())
                {
                    material.textureBytes = std::move(bytes);
                    material.textureName = name.empty() ? L"(embutida)" : Utf8ToWide(name.c_str());
                }
                return;
            }

            std::string decoded = UriDecode(uri);
            for (char& c : decoded) if (c == '/') c = '\\';
            std::wstring relative = Utf8ToWide(decoded.c_str());
            const std::wstring candidates[] = {
                ctx.baseDir + relative,
                relative,
                ctx.baseDir + FileNameOf(relative),
                ctx.baseDir + L"textures\\" + FileNameOf(relative),
            };
            for (const std::wstring& candidate : candidates)
            {
                if (FileExists(candidate))
                {
                    material.texturePath = candidate;
                    material.textureName = FileNameOf(candidate);
                    return;
                }
            }
            return;
        }

        int viewIndex = image->IntAt("bufferView", -1);
        const minijson::Value* view = ctx.ItemAt("bufferViews", viewIndex);
        if (!view) return;
        int bufferIndex = view->IntAt("buffer", -1);
        if (bufferIndex < 0 || (size_t)bufferIndex >= ctx.buffers.size()) return;

        const std::vector<uint8_t>& buffer = ctx.buffers[(size_t)bufferIndex];
        size_t offset = (size_t)view->NumberAt("byteOffset", 0);
        size_t length = (size_t)view->NumberAt("byteLength", 0);
        if (offset > buffer.size() || length > buffer.size() - offset) return;

        material.textureBytes.assign(buffer.begin() + offset, buffer.begin() + offset + length);
        material.textureName = name.empty() ? L"(embutida)" : Utf8ToWide(name.c_str());
    }
}

bool LoadGltfFile(const std::wstring& filePath, SceneModel& outModel, std::wstring& outError)
{
    std::vector<uint8_t> fileBytes;
    if (!ReadWholeFile(filePath, fileBytes, outError))
        return false;

    GltfContext ctx;
    ctx.baseDir = DirectoryOf(filePath);

    // ---------------------------------------------------------------------
    // 1) Separa JSON e (no GLB) o bloco binario
    // ---------------------------------------------------------------------
    std::string jsonText;
    std::vector<uint8_t> glbBinaryChunk;
    bool isGlb = fileBytes.size() >= 12 && memcmp(fileBytes.data(), "glTF", 4) == 0;

    if (isGlb)
    {
        uint32_t version = 0, totalLength = 0;
        memcpy(&version, fileBytes.data() + 4, 4);
        memcpy(&totalLength, fileBytes.data() + 8, 4);
        if (version != 2)
        {
            outError = L"Somente GLB versão 2 é suportado (arquivo declara versão "
                + std::to_wstring(version) + L").";
            return false;
        }
        size_t limit = std::min<size_t>(fileBytes.size(), totalLength ? totalLength : fileBytes.size());

        size_t offset = 12;
        while (offset + 8 <= limit)
        {
            uint32_t chunkLength = 0, chunkType = 0;
            memcpy(&chunkLength, fileBytes.data() + offset, 4);
            memcpy(&chunkType, fileBytes.data() + offset + 4, 4);
            offset += 8;
            if (chunkLength > limit - offset)
            {
                outError = L"Chunk do GLB com tamanho inválido (arquivo truncado?).";
                return false;
            }
            const uint8_t* chunkData = fileBytes.data() + offset;
            if (chunkType == 0x4E4F534A) // "JSON"
                jsonText.assign((const char*)chunkData, chunkLength);
            else if (chunkType == 0x004E4942) // "BIN\0"
                glbBinaryChunk.assign(chunkData, chunkData + chunkLength);
            offset += chunkLength;
            offset = (offset + 3) & ~(size_t)3; // chunks sao alinhados em 4 bytes
        }
        if (jsonText.empty())
        {
            outError = L"O GLB não contém o chunk JSON.";
            return false;
        }
    }
    else
    {
        // .gltf de texto — pode vir com BOM UTF-8
        size_t start = 0;
        if (fileBytes.size() >= 3 && fileBytes[0] == 0xEF && fileBytes[1] == 0xBB && fileBytes[2] == 0xBF)
            start = 3;
        jsonText.assign((const char*)fileBytes.data() + start, fileBytes.size() - start);
    }

    minijson::Value root;
    std::string jsonError;
    if (!minijson::Parse(jsonText.data(), jsonText.data() + jsonText.size(), root, jsonError) || !root.IsObject())
    {
        outError = L"JSON do glTF inválido: " + Utf8ToWide(jsonError.c_str());
        return false;
    }
    ctx.root = &root;

    // ---------------------------------------------------------------------
    // 2) Buffers
    // ---------------------------------------------------------------------
    if (const minijson::Value* buffers = ctx.Array("buffers"))
    {
        for (size_t i = 0; i < buffers->Size(); i++)
        {
            const minijson::Value& buffer = *buffers->At(i);
            std::vector<uint8_t> bytes;
            std::string uri = buffer.StringAt("uri");

            if (uri.empty())
            {
                // Sem uri = o chunk BIN do GLB (so vale para o buffer 0)
                bytes = glbBinaryChunk;
            }
            else if (IsDataUri(uri))
            {
                if (!DecodeDataUri(uri, bytes))
                {
                    outError = L"Falha ao decodificar um data URI de buffer.";
                    return false;
                }
            }
            else
            {
                std::string decoded = UriDecode(uri);
                for (char& c : decoded) if (c == '/') c = '\\';
                std::wstring path = ctx.baseDir + Utf8ToWide(decoded.c_str());
                std::wstring readError;
                if (!ReadWholeFile(path, bytes, readError))
                {
                    outError = L"Não foi possível abrir o buffer externo \""
                        + Utf8ToWide(decoded.c_str()) + L"\": " + readError;
                    return false;
                }
            }
            ctx.buffers.push_back(std::move(bytes));
        }
    }

    // ---------------------------------------------------------------------
    // 3) Materiais
    // ---------------------------------------------------------------------
    const minijson::Value* materials = ctx.Array("materials");
    size_t materialCount = materials ? materials->Size() : 0;
    for (size_t i = 0; i < materialCount; i++)
    {
        const minijson::Value& source = *materials->At(i);
        MaterialData material;
        material.name = source.StringAt("name", "Material");

        const minijson::Value* pbr = source.Find("pbrMetallicRoughness");
        if (pbr && pbr->IsObject())
        {
            if (const minijson::Value* factor = pbr->Find("baseColorFactor"))
            {
                if (factor->IsArray() && factor->Size() >= 3)
                {
                    material.diffuseColor = XMFLOAT4(
                        (float)factor->At(0)->Number(1),
                        (float)factor->At(1)->Number(1),
                        (float)factor->At(2)->Number(1),
                        factor->Size() >= 4 ? (float)factor->At(3)->Number(1) : 1.0f);
                }
            }
            if (const minijson::Value* baseTex = pbr->Find("baseColorTexture"))
            {
                int textureIndex = baseTex->IntAt("index", -1);
                if (const minijson::Value* texture = ctx.ItemAt("textures", textureIndex))
                    ResolveImage(ctx, texture->IntAt("source", -1), material);
            }
        }
        outModel.materials.push_back(std::move(material));
    }

    // ---------------------------------------------------------------------
    // 4) Percorre a hierarquia de nodes e achata tudo em um unico buffer
    // ---------------------------------------------------------------------
    const minijson::Value* nodes = ctx.Array("nodes");
    const minijson::Value* meshes = ctx.Array("meshes");

    BoundsAccumulator bounds;
    EdgeCounter edgeCounter;
    UINT meshInstanceCount = 0;
    // Um SubMesh por (node, primitive); agrupamos por material na hora de
    // montar para manter uma draw call por primitive, como no FbxLoader.
    std::vector<uint8_t> visited(nodes ? nodes->Size() : 0, 0);

    // Emite uma primitive ja transformada para o mundo.
    auto emitPrimitive = [&](const minijson::Value& primitive, const XMMATRIX& world,
        bool flipWinding, int meshIndex) -> bool
    {
        int mode = primitive.IntAt("mode", kModeTriangles);
        if (mode != kModeTriangles && mode != kModeTriangleStrip && mode != kModeTriangleFan)
            return true; // pontos e linhas: ignorados silenciosamente

        const minijson::Value* attributes = primitive.Find("attributes");
        if (!attributes || !attributes->IsObject()) return true;

        int positionAccessor = attributes->IntAt("POSITION", -1);
        if (positionAccessor < 0) return true;

        std::vector<float> positions, normals, uvs, colors;
        if (!ctx.ReadFloats(positionAccessor, 3, positions)) { outError = ctx.error; return false; }
        size_t vertexCount = positions.size() / 3;
        if (vertexCount == 0) return true;

        int normalAccessor = attributes->IntAt("NORMAL", -1);
        if (normalAccessor >= 0 && !ctx.ReadFloats(normalAccessor, 3, normals)) { outError = ctx.error; return false; }
        int uvAccessor = attributes->IntAt("TEXCOORD_0", -1);
        if (uvAccessor >= 0 && !ctx.ReadFloats(uvAccessor, 2, uvs)) { outError = ctx.error; return false; }
        if (uvAccessor >= 0) outModel.hasTexCoords = true;
        int colorAccessor = attributes->IntAt("COLOR_0", -1);
        if (colorAccessor >= 0 && !ctx.ReadFloats(colorAccessor, 4, colors)) { outError = ctx.error; return false; }
        // COLOR_0 pode ser VEC3: o alfa nao lido fica 0, entao corrigimos.
        if (colorAccessor >= 0)
        {
            const minijson::Value* accessor = ctx.ItemAt("accessors", colorAccessor);
            if (accessor && ComponentCount(accessor->StringAt("type")) == 3)
                for (size_t i = 3; i < colors.size(); i += 4) colors[i] = 1.0f;
        }

        std::vector<uint32_t> indices;
        int indexAccessor = primitive.IntAt("indices", -1);
        if (indexAccessor >= 0)
        {
            if (!ctx.ReadIndices(indexAccessor, indices)) { outError = ctx.error; return false; }
        }
        else
        {
            indices.resize(vertexCount);
            for (size_t i = 0; i < vertexCount; i++) indices[i] = (uint32_t)i;
        }

        // Normais precisam da inversa transposta para sobreviver a escalas
        // nao uniformes.
        XMMATRIX normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

        uint32_t baseVertex = (uint32_t)outModel.vertices.size();
        outModel.vertices.reserve(outModel.vertices.size() + vertexCount);
        for (size_t i = 0; i < vertexCount; i++)
        {
            Vertex vertex;

            XMVECTOR p = XMVectorSet(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2], 1.0f);
            XMFLOAT3 worldPos;
            XMStoreFloat3(&worldPos, XMVector3Transform(p, world));
            vertex.position = RhToLh(worldPos);

            XMFLOAT3 worldNormal(0, 1, 0);
            if (normals.size() >= (i + 1) * 3)
            {
                XMVECTOR n = XMVectorSet(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2], 0.0f);
                XMStoreFloat3(&worldNormal, XMVector3Normalize(XMVector3TransformNormal(n, normalMatrix)));
            }
            vertex.normal = RhToLh(worldNormal);

            // O glTF ja usa a origem da UV no canto superior esquerdo, igual
            // ao DirectX — nao ha inversao de V aqui (diferente de FBX/OBJ).
            vertex.uv = (uvs.size() >= (i + 1) * 2)
                ? XMFLOAT2(uvs[i * 2], uvs[i * 2 + 1])
                : XMFLOAT2(0, 0);

            if (colors.size() >= (i + 1) * 4)
                vertex.color = PackColorRgba(colors[i * 4], colors[i * 4 + 1], colors[i * 4 + 2], colors[i * 4 + 3]);

            bounds.Add(vertex.position);
            outModel.vertices.push_back(vertex);
        }

        // Converte strip/fan para lista de triangulos
        std::vector<uint32_t> triangles;
        if (mode == kModeTriangles)
        {
            triangles = std::move(indices);
        }
        else if (mode == kModeTriangleStrip)
        {
            for (size_t i = 0; i + 2 < indices.size(); i++)
            {
                if (i % 2 == 0) { triangles.push_back(indices[i]); triangles.push_back(indices[i + 1]); triangles.push_back(indices[i + 2]); }
                else            { triangles.push_back(indices[i + 1]); triangles.push_back(indices[i]); triangles.push_back(indices[i + 2]); }
            }
        }
        else // fan
        {
            for (size_t i = 1; i + 1 < indices.size(); i++)
            {
                triangles.push_back(indices[0]);
                triangles.push_back(indices[i]);
                triangles.push_back(indices[i + 1]);
            }
        }

        SubMesh sub;
        sub.indexStart = (UINT)outModel.indices.size();
        sub.materialIndex = primitive.IntAt("material", -1);
        if (sub.materialIndex >= (int)outModel.materials.size()) sub.materialIndex = -1;

        outModel.indices.reserve(outModel.indices.size() + triangles.size());
        for (size_t i = 0; i + 2 < triangles.size(); i += 3)
        {
            uint32_t a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
            if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
            {
                outError = L"Índice fora do intervalo em uma primitive do glTF.";
                return false;
            }
            edgeCounter.AddTriangle(meshIndex, a, b, c);
            outModel.indices.push_back(baseVertex + a);
            if (flipWinding)
            {
                outModel.indices.push_back(baseVertex + c);
                outModel.indices.push_back(baseVertex + b);
            }
            else
            {
                outModel.indices.push_back(baseVertex + b);
                outModel.indices.push_back(baseVertex + c);
            }
        }

        sub.indexCount = (UINT)outModel.indices.size() - sub.indexStart;
        if (sub.indexCount > 0)
            outModel.subMeshes.push_back(sub);
        return true;
    };

    // Recursao pela hierarquia. A pilha explicita evita estouro em cenas com
    // hierarquias patologicas.
    struct PendingNode { int index; XMMATRIX parentWorld; };
    std::vector<PendingNode> stack;

    auto pushRoots = [&]()
    {
        const minijson::Value* scenes = ctx.Array("scenes");
        int sceneIndex = root.IntAt("scene", 0);
        const minijson::Value* scene = (scenes && (size_t)sceneIndex < scenes->Size())
            ? scenes->At((size_t)sceneIndex) : nullptr;

        if (scene)
        {
            if (const minijson::Value* rootNodes = scene->Find("nodes"))
                if (rootNodes->IsArray())
                    for (size_t i = rootNodes->Size(); i > 0; i--)
                        stack.push_back({ rootNodes->At(i - 1)->Int(-1), XMMatrixIdentity() });
        }
        else if (nodes)
        {
            // Sem "scenes": trata todos os nodes como raizes.
            for (size_t i = nodes->Size(); i > 0; i--)
                stack.push_back({ (int)(i - 1), XMMatrixIdentity() });
        }
    };
    pushRoots();

    while (!stack.empty())
    {
        PendingNode pending = stack.back();
        stack.pop_back();

        if (!nodes || pending.index < 0 || (size_t)pending.index >= nodes->Size()) continue;
        if (visited[(size_t)pending.index]) continue; // protege contra ciclos
        visited[(size_t)pending.index] = 1;

        const minijson::Value& node = *nodes->At((size_t)pending.index);
        XMMATRIX world = NodeLocalMatrix(node) * pending.parentWorld;

        int meshIndex = node.IntAt("mesh", -1);
        if (meshes && meshIndex >= 0 && (size_t)meshIndex < meshes->Size())
        {
            const minijson::Value& mesh = *meshes->At((size_t)meshIndex);
            const minijson::Value* primitives = mesh.Find("primitives");
            if (primitives && primitives->IsArray())
            {
                // O espelhamento de eixo (RhToLh) ja inverte o sentido dos
                // triangulos; um node com determinante negativo inverte de
                // novo, e as duas inversoes se cancelam.
                float determinant = XMVectorGetX(XMMatrixDeterminant(world));
                bool flipWinding = (determinant >= 0.0f);

                for (size_t i = 0; i < primitives->Size(); i++)
                    if (!emitPrimitive(*primitives->At(i), world, flipWinding, (int)meshInstanceCount))
                        return false;
                meshInstanceCount++;
            }
        }

        if (const minijson::Value* children = node.Find("children"))
            if (children->IsArray())
                for (size_t i = children->Size(); i > 0; i--)
                    stack.push_back({ children->At(i - 1)->Int(-1), world });
    }

    if (outModel.vertices.empty() || outModel.indices.empty())
    {
        outError = L"Nenhuma malha triangular encontrada no arquivo glTF.";
        return false;
    }

    bounds.StoreInto(outModel);

    if (outModel.materials.empty())
    {
        MaterialData fallback;
        fallback.name = "Default";
        outModel.materials.push_back(fallback);
        for (SubMesh& sub : outModel.subMeshes)
            if (sub.materialIndex < 0) sub.materialIndex = 0;
    }

    FinalizeStats(outModel, edgeCounter.Count(), std::max<UINT>(meshInstanceCount, 1));
    return true;
}
