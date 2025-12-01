# Internals: RESP Parser & Types

## RedisType, RedisValue, RedisResult, RedisError

```cpp
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

    bool is_null() const;
    bool is_error() const;
    bool is_simple_string() const;
    bool is_bulk_string() const;
    bool is_integer() const;
    bool is_array() const;

    const std::string& as_string() const;
    int64_t            as_integer() const;
    const Array&       as_array() const;
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
```

* **Io** – socket errors, timeouts, short writes/reads, disconnects.
* **Protocol** – malformed RESP or unexpected reply type.
* **ServerReply** – Redis returned an error reply (`-ERR ...`), propagated as failure.

## RespParser

`RespParser` is an incremental RESP parser used by both `RedisClient` and `RedisSubscriber`.

```cpp
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
```

### Usage pattern

```cpp
RespParser parser;

// in the read loop:
parser.feed(reinterpret_cast<const uint8_t*>(buf.data()), buf.size());

while (true) {
    auto val_opt = parser.next();
    if (!val_opt) break;

    RedisValue v = std::move(*val_opt);
    // handle v
}
```

### Parser behavior

* `feed()` appends raw bytes to an internal buffer.
* `next()`:

    * Returns `std::nullopt` if there is not enough data for a full reply.
    * Otherwise parses exactly one RESP value and advances internal position.
* `compact_if_needed()` periodically trims already-consumed data from the front of the buffer to avoid unbounded growth.

Supported RESP prefixes:

* `'+'` → Simple string
* `'-'` → Error string
* `':'` → Integer
* `'$'` → Bulk string (including null bulk when length `< 0`)
* `'*'` → Array (recursive, elements can be any of the above)