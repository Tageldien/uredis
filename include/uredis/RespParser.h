#ifndef RESPPARSE_H
#define RESPPARSE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uredis/RedisTypes.h"

namespace usub::uredis
{
    class RespParser
    {
    public:
        RespParser() = default;

        void reset();

        void feed(const uint8_t* data, std::size_t len);

        std::optional<RedisValue> next();

    private:
        std::vector<uint8_t> buffer_;
        std::size_t pos_{0};

        bool ensure(std::size_t n) const;
        std::optional<std::size_t> find_crlf(std::size_t from) const;
        std::optional<std::string> read_line();
        std::optional<RedisValue> parse_value();
        std::optional<RedisValue> parse_simple_string();
        std::optional<RedisValue> parse_error();
        std::optional<RedisValue> parse_integer();
        std::optional<RedisValue> parse_bulk_string();
        std::optional<RedisValue> parse_array();

        void compact_if_needed();
    };
} // namespace usub::uredis

#endif //RESPPARSE_H
