#ifndef REDISTYPES_H
#define REDISTYPES_H

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace usub::uredis
{
    enum class RedisType
    {
        Null,
        SimpleString,
        Error,
        Integer,
        BulkString,
        Array
    };

    struct RedisValue
    {
        using Array = std::vector<RedisValue>;

        RedisType type{RedisType::Null};
        std::variant<std::monostate, std::string, int64_t, Array> value;

        bool is_null() const { return this->type == RedisType::Null; }
        bool is_error() const { return this->type == RedisType::Error; }
        bool is_simple_string() const { return this->type == RedisType::SimpleString; }
        bool is_bulk_string() const { return this->type == RedisType::BulkString; }
        bool is_integer() const { return this->type == RedisType::Integer; }
        bool is_array() const { return this->type == RedisType::Array; }

        const std::string& as_string() const
        {
            return std::get<std::string>(this->value);
        }

        int64_t as_integer() const
        {
            return std::get<int64_t>(this->value);
        }

        const Array& as_array() const
        {
            return std::get<Array>(this->value);
        }
    };

    enum class RedisErrorCategory
    {
        Io,
        Protocol,
        ServerReply
    };

    struct RedisError
    {
        RedisErrorCategory category{RedisErrorCategory::Protocol};
        std::string message;
    };

    template <typename T>
    using RedisResult = std::expected<T, RedisError>;
} // namespace usub::uredis

#endif //REDISTYPES_H
