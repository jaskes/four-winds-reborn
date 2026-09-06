#include "wirejson.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Multiplayer
{
namespace
{
    constexpr std::size_t MaximumBytes = 1024 * 1024;
    constexpr unsigned MaximumDepth = 32;
    constexpr std::size_t MaximumTokens = 65536;

    // Construct the existing SWE value types directly. Its text parser accepts
    // several non-JSON forms and does not decode Unicode escapes, so validating
    // and then reparsing through it would still alter valid wire semantics.
    struct WireObject : SWE::JsonObject
    {
        void insert(const std::string & key, std::unique_ptr<SWE::JsonValue> value)
        {
            SWE::JsonValuePtr owned(value.release());
            content.emplace(key, std::move(owned));
        }
    };

    struct WireArray : SWE::JsonArray
    {
        void append(std::unique_ptr<SWE::JsonValue> value)
        {
            SWE::JsonValuePtr owned(value.release());
            content.emplace_back(std::move(owned));
        }
    };

    class Parser
    {
        const std::string & source;
        std::size_t position = 0;
        std::size_t tokens = 0;

        [[noreturn]] void fail(const char * message) const
        {
            throw std::runtime_error(std::string(message) + " at byte " + std::to_string(position));
        }

        void token()
        {
            if(++tokens > MaximumTokens) fail("JSON token limit exceeded");
        }

        void whitespace()
        {
            while(position < source.size())
            {
                const char ch = source[position];
                if(ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
                ++position;
            }
        }

        bool take(char expected)
        {
            if(position < source.size() && source[position] == expected)
            {
                ++position;
                return true;
            }
            return false;
        }

        static bool digit(char ch) { return ch >= '0' && ch <= '9'; }

        std::uint32_t hex4()
        {
            std::uint32_t result = 0;
            for(int index = 0; index < 4; ++index)
            {
                if(position == source.size()) fail("Incomplete Unicode escape");
                const char ch = source[position++];
                unsigned value;
                if(ch >= '0' && ch <= '9') value = ch - '0';
                else if(ch >= 'a' && ch <= 'f') value = ch - 'a' + 10;
                else if(ch >= 'A' && ch <= 'F') value = ch - 'A' + 10;
                else fail("Invalid Unicode escape");
                result = (result << 4) | value;
            }
            return result;
        }

        static void appendCodepoint(std::string & result, std::uint32_t codepoint)
        {
            if(codepoint <= 0x7f) result += static_cast<char>(codepoint);
            else if(codepoint <= 0x7ff)
            {
                result += static_cast<char>(0xc0 | (codepoint >> 6));
                result += static_cast<char>(0x80 | (codepoint & 0x3f));
            }
            else if(codepoint <= 0xffff)
            {
                result += static_cast<char>(0xe0 | (codepoint >> 12));
                result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
                result += static_cast<char>(0x80 | (codepoint & 0x3f));
            }
            else
            {
                result += static_cast<char>(0xf0 | (codepoint >> 18));
                result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
                result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
                result += static_cast<char>(0x80 | (codepoint & 0x3f));
            }
        }

        void unicodeEscape(std::string & result)
        {
            std::uint32_t codepoint = hex4();
            if(codepoint >= 0xd800 && codepoint <= 0xdbff)
            {
                if(!take('\\') || !take('u')) fail("Missing low surrogate");
                const auto low = hex4();
                if(low < 0xdc00 || low > 0xdfff) fail("Invalid low surrogate");
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + low - 0xdc00;
            }
            else if(codepoint >= 0xdc00 && codepoint <= 0xdfff)
                fail("Unpaired low surrogate");
            appendCodepoint(result, codepoint);
        }

        void utf8(std::string & result)
        {
            const auto begin = position;
            const auto first = static_cast<unsigned char>(source[position++]);
            unsigned remaining = 0;
            std::uint32_t codepoint = 0;
            std::uint32_t minimum = 0;
            if(first >= 0xc2 && first <= 0xdf)
            {
                remaining = 1; codepoint = first & 0x1f; minimum = 0x80;
            }
            else if(first >= 0xe0 && first <= 0xef)
            {
                remaining = 2; codepoint = first & 0x0f; minimum = 0x800;
            }
            else if(first >= 0xf0 && first <= 0xf4)
            {
                remaining = 3; codepoint = first & 0x07; minimum = 0x10000;
            }
            else fail("Invalid UTF-8 leading byte");
            for(unsigned index = 0; index < remaining; ++index)
            {
                if(position == source.size()) fail("Incomplete UTF-8 sequence");
                const auto next = static_cast<unsigned char>(source[position++]);
                if((next & 0xc0) != 0x80) fail("Invalid UTF-8 continuation byte");
                codepoint = (codepoint << 6) | (next & 0x3f);
            }
            if(codepoint < minimum || codepoint > 0x10ffff ||
               (codepoint >= 0xd800 && codepoint <= 0xdfff))
                fail("Invalid UTF-8 codepoint");
            result.append(source, begin, position - begin);
        }

        std::string string()
        {
            if(!take('"')) fail("Expected JSON string");
            std::string result;
            while(position < source.size())
            {
                const auto ch = static_cast<unsigned char>(source[position]);
                if(ch == '"') { ++position; return result; }
                if(ch < 0x20) fail("Unescaped control character");
                if(ch >= 0x80) { utf8(result); continue; }
                ++position;
                if(ch != '\\') { result += static_cast<char>(ch); continue; }
                if(position == source.size()) fail("Incomplete string escape");
                switch(source[position++])
                {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case 'u': unicodeEscape(result); break;
                    default: fail("Invalid string escape");
                }
            }
            fail("Unterminated JSON string");
        }

        std::unique_ptr<SWE::JsonValue> number()
        {
            const auto begin = position;
            take('-');
            if(position == source.size()) fail("Incomplete number");
            if(take('0'))
            {
                if(position < source.size() && digit(source[position])) fail("Leading zero in number");
            }
            else
            {
                if(source[position] < '1' || source[position] > '9') fail("Invalid number");
                while(position < source.size() && digit(source[position])) ++position;
            }
            bool real = false;
            if(take('.'))
            {
                real = true;
                if(position == source.size() || !digit(source[position])) fail("Missing fractional digits");
                while(position < source.size() && digit(source[position])) ++position;
            }
            if(take('e') || take('E'))
            {
                real = true;
                if(!take('+')) take('-');
                if(position == source.size() || !digit(source[position])) fail("Missing exponent digits");
                while(position < source.size() && digit(source[position])) ++position;
            }
            if(position - begin > 128) fail("Number token is too long");
            if(!real)
            {
                int result = 0;
                const auto converted = std::from_chars(source.data() + begin, source.data() + position, result);
                if(converted.ec != std::errc() || converted.ptr != source.data() + position)
                    fail("Integer is outside the signed 32-bit range");
                return std::make_unique<SWE::JsonInteger>(result);
            }
            std::istringstream stream(source.substr(begin, position - begin));
            stream.imbue(std::locale::classic());
            double result = 0;
            stream >> result;
            if(stream.fail() || !stream.eof() || !std::isfinite(result))
                fail("Non-finite or out-of-range number");
            return std::make_unique<SWE::JsonDouble>(result);
        }

        void literal(const char * text)
        {
            while(*text)
            {
                if(position == source.size() || source[position++] != *text++)
                    fail("Invalid JSON literal");
            }
        }

        std::unique_ptr<SWE::JsonValue> object(unsigned depth)
        {
            ++position;
            auto result = std::make_unique<WireObject>();
            std::unordered_set<std::string> keys;
            whitespace();
            if(take('}')) return result;
            for(;;)
            {
                token();
                const std::string key = string();
                if(!keys.insert(key).second) fail("Duplicate object key");
                whitespace();
                if(!take(':')) fail("Missing colon after object key");
                result->insert(key, value(depth + 1));
                whitespace();
                if(take('}')) return result;
                if(!take(',')) fail("Missing comma between object members");
                whitespace();
            }
        }

        std::unique_ptr<SWE::JsonValue> array(unsigned depth)
        {
            ++position;
            auto result = std::make_unique<WireArray>();
            whitespace();
            if(take(']')) return result;
            for(;;)
            {
                result->append(value(depth + 1));
                whitespace();
                if(take(']')) return result;
                if(!take(',')) fail("Missing comma between array entries");
                whitespace();
            }
        }

        std::unique_ptr<SWE::JsonValue> value(unsigned depth)
        {
            if(depth > MaximumDepth) fail("JSON nesting limit exceeded");
            token();
            whitespace();
            if(position == source.size()) fail("Missing JSON value");
            switch(source[position])
            {
                case '{': return object(depth);
                case '[': return array(depth);
                case '"': return std::make_unique<SWE::JsonString>(string());
                case 't': literal("true"); return std::make_unique<SWE::JsonBoolean>(true);
                case 'f': literal("false"); return std::make_unique<SWE::JsonBoolean>(false);
                case 'n': literal("null"); return std::make_unique<SWE::JsonNull>();
                default:
                    if(source[position] == '-' || digit(source[position])) return number();
                    fail("Unexpected JSON value");
            }
        }

    public:
        explicit Parser(const std::string & input) : source(input) {}
        SWE::JsonObject parse()
        {
            if(source.empty() || source.size() > MaximumBytes) fail("JSON message size is outside the allowed range");
            whitespace();
            if(position == source.size() || source[position] != '{') fail("Wire message must be a JSON object");
            auto result = value(1);
            whitespace();
            if(position != source.size()) fail("Trailing data after JSON object");
            return std::move(*static_cast<SWE::JsonObject *>(result.get()));
        }
    };
}

bool parseWireObject(const std::string & text, SWE::JsonObject & result, std::string & error)
{
    error.clear();
    try
    {
        auto parsed = Parser(text).parse();
        result = std::move(parsed);
        return true;
    }
    catch(const std::exception & exception)
    {
        error = std::string("Invalid wire JSON: ") + exception.what();
        return false;
    }
}
}
