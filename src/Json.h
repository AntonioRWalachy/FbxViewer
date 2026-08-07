#pragma once
// Parser JSON minimo, suficiente para o cabecalho de arquivos glTF 2.0.
// Nao ha dependencia externa no projeto, entao vale mais um parser pequeno e
// auditavel do que arrastar uma biblioteca inteira.
#include <string>
#include <utility>
#include <vector>

namespace minijson
{

class Value
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;                                   // Type::String
    std::vector<Value> items;                           // Type::Array
    std::vector<std::pair<std::string, Value>> members; // Type::Object

    bool IsNull()   const { return type == Type::Null; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray()  const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    // Busca uma chave do objeto. Retorna nullptr se nao existir.
    // Linear de proposito: objetos glTF tem poucas chaves.
    const Value* Find(const char* key) const;

    // Acessores com valor padrao, para nao poluir o loader com checagens.
    double Number(double fallback = 0.0) const { return IsNumber() ? number : fallback; }
    int Int(int fallback = 0) const { return IsNumber() ? (int)number : fallback; }
    bool Bool(bool fallback = false) const { return type == Type::Bool ? boolean : fallback; }
    std::string String(const char* fallback = "") const { return IsString() ? text : std::string(fallback); }

    double NumberAt(const char* key, double fallback = 0.0) const;
    int IntAt(const char* key, int fallback = 0) const;
    std::string StringAt(const char* key, const char* fallback = "") const;

    size_t Size() const { return IsArray() ? items.size() : 0; }
    const Value* At(size_t index) const { return (IsArray() && index < items.size()) ? &items[index] : nullptr; }
};

// Faz o parse de [begin, end). Em caso de erro devolve false e preenche
// outError com a posicao e o motivo.
bool Parse(const char* begin, const char* end, Value& outValue, std::string& outError);

} // namespace minijson
