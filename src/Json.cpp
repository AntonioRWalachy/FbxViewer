#include "Json.h"

#include <cstdlib>
#include <cstring>

namespace minijson
{

const Value* Value::Find(const char* key) const
{
    if (type != Type::Object || !key) return nullptr;
    for (const auto& member : members)
        if (member.first == key)
            return &member.second;
    return nullptr;
}

double Value::NumberAt(const char* key, double fallback) const
{
    const Value* v = Find(key);
    return (v && v->IsNumber()) ? v->number : fallback;
}

int Value::IntAt(const char* key, int fallback) const
{
    const Value* v = Find(key);
    return (v && v->IsNumber()) ? (int)v->number : fallback;
}

std::string Value::StringAt(const char* key, const char* fallback) const
{
    const Value* v = Find(key);
    return (v && v->IsString()) ? v->text : std::string(fallback);
}

namespace
{
    // Profundidade maxima: protege contra estouro de pilha com entrada
    // maliciosa ou corrompida (colchetes aninhados sem fim).
    constexpr int kMaxDepth = 200;

    struct Parser
    {
        const char* cur;
        const char* end;
        std::string error;

        void SkipWhitespace()
        {
            while (cur < end && (*cur == ' ' || *cur == '\t' || *cur == '\n' || *cur == '\r'))
                cur++;
        }

        bool Fail(const char* message)
        {
            if (error.empty())
                error = message;
            return false;
        }

        // Converte um code point para UTF-8 (usado nos escapes \uXXXX).
        static void AppendUtf8(std::string& out, unsigned int cp)
        {
            if (cp < 0x80)
            {
                out += (char)cp;
            }
            else if (cp < 0x800)
            {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            else
            {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        }

        bool ParseHex4(unsigned int& out)
        {
            if (end - cur < 4) return Fail("escape \\u incompleto");
            out = 0;
            for (int i = 0; i < 4; i++)
            {
                char c = cur[i];
                unsigned int digit;
                if (c >= '0' && c <= '9') digit = (unsigned int)(c - '0');
                else if (c >= 'a' && c <= 'f') digit = (unsigned int)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') digit = (unsigned int)(c - 'A' + 10);
                else return Fail("digito hexadecimal invalido no escape \\u");
                out = (out << 4) | digit;
            }
            cur += 4;
            return true;
        }

        bool ParseString(std::string& out)
        {
            if (cur >= end || *cur != '"') return Fail("esperava aspas");
            cur++;
            out.clear();
            while (cur < end)
            {
                char c = *cur++;
                if (c == '"') return true;
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (cur >= end) return Fail("escape incompleto");
                char esc = *cur++;
                switch (esc)
                {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                {
                    unsigned int cp = 0;
                    if (!ParseHex4(cp)) return false;
                    // Par substituto (surrogate pair) UTF-16
                    if (cp >= 0xD800 && cp <= 0xDBFF && end - cur >= 6 &&
                        cur[0] == '\\' && cur[1] == 'u')
                    {
                        const char* save = cur;
                        cur += 2;
                        unsigned int low = 0;
                        if (ParseHex4(low) && low >= 0xDC00 && low <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        else
                            cur = save;
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    return Fail("escape desconhecido em string");
                }
            }
            return Fail("string sem fechamento");
        }

        bool ParseValue(Value& out, int depth)
        {
            if (depth > kMaxDepth) return Fail("JSON aninhado demais");
            SkipWhitespace();
            if (cur >= end) return Fail("fim inesperado do JSON");

            char c = *cur;
            if (c == '{')
            {
                cur++;
                out.type = Value::Type::Object;
                SkipWhitespace();
                if (cur < end && *cur == '}') { cur++; return true; }
                while (true)
                {
                    SkipWhitespace();
                    std::string key;
                    if (!ParseString(key)) return false;
                    SkipWhitespace();
                    if (cur >= end || *cur != ':') return Fail("esperava ':' no objeto");
                    cur++;
                    Value child;
                    if (!ParseValue(child, depth + 1)) return false;
                    out.members.emplace_back(std::move(key), std::move(child));
                    SkipWhitespace();
                    if (cur < end && *cur == ',') { cur++; continue; }
                    if (cur < end && *cur == '}') { cur++; return true; }
                    return Fail("esperava ',' ou '}' no objeto");
                }
            }
            if (c == '[')
            {
                cur++;
                out.type = Value::Type::Array;
                SkipWhitespace();
                if (cur < end && *cur == ']') { cur++; return true; }
                while (true)
                {
                    Value child;
                    if (!ParseValue(child, depth + 1)) return false;
                    out.items.push_back(std::move(child));
                    SkipWhitespace();
                    if (cur < end && *cur == ',') { cur++; continue; }
                    if (cur < end && *cur == ']') { cur++; return true; }
                    return Fail("esperava ',' ou ']' no array");
                }
            }
            if (c == '"')
            {
                out.type = Value::Type::String;
                return ParseString(out.text);
            }
            if (c == 't' && (size_t)(end - cur) >= 4 && strncmp(cur, "true", 4) == 0)
            {
                cur += 4;
                out.type = Value::Type::Bool;
                out.boolean = true;
                return true;
            }
            if (c == 'f' && (size_t)(end - cur) >= 5 && strncmp(cur, "false", 5) == 0)
            {
                cur += 5;
                out.type = Value::Type::Bool;
                out.boolean = false;
                return true;
            }
            if (c == 'n' && (size_t)(end - cur) >= 4 && strncmp(cur, "null", 4) == 0)
            {
                cur += 4;
                out.type = Value::Type::Null;
                return true;
            }
            if (c == '-' || (c >= '0' && c <= '9'))
            {
                // strtod precisa de string terminada em '\0'; copiamos so o
                // trecho numerico (sempre curto).
                char buffer[64];
                size_t n = 0;
                const char* scan = cur;
                while (scan < end && n + 1 < sizeof(buffer) &&
                    (*scan == '-' || *scan == '+' || *scan == '.' ||
                     *scan == 'e' || *scan == 'E' || (*scan >= '0' && *scan <= '9')))
                {
                    buffer[n++] = *scan++;
                }
                buffer[n] = '\0';
                char* stop = nullptr;
                double value = strtod(buffer, &stop);
                if (stop == buffer) return Fail("numero invalido");
                cur += (size_t)(stop - buffer);
                out.type = Value::Type::Number;
                out.number = value;
                return true;
            }
            return Fail("token JSON inesperado");
        }
    };
}

bool Parse(const char* begin, const char* end, Value& outValue, std::string& outError)
{
    Parser parser{ begin, end, {} };
    outValue = Value();
    if (!parser.ParseValue(outValue, 0))
    {
        outError = parser.error.empty() ? "JSON invalido" : parser.error;
        return false;
    }
    parser.SkipWhitespace();
    return true;
}

} // namespace minijson
