# Reflection helpers

Namespace: `usub::uredis::reflect`.

These helpers let you map **C++ aggregates** to Redis hashes using **ureflect**.

```cpp
namespace usub::uredis::reflect {

template <class T>
    requires std::is_aggregate_v<std::remove_cvref_t<T>>
task::Awaitable<RedisResult<int64_t>> hset_struct(
    RedisClient& client,
    std::string_view key,
    const T& value);

template <class T>
    requires std::is_aggregate_v<std::remove_cvref_t<T>>
task::Awaitable<RedisResult<std::optional<T>>> hget_struct(
    RedisClient& client,
    std::string_view key);

} // namespace usub::uredis::reflect
```

## Supported field types

Internally it converts fields to/from strings using `detail::to_redis_string_impl` / `from_redis_string_impl`.

Out of the box it handles:

* `std::string`, `std::string_view`, `const char*`
* `bool` (stored as `"0"` / `"1"`)
* integral types (via `std::to_chars` / `std::from_chars`)
* floating point types (via `std::to_string` / `std::stof` / `std::stod`)
* `std::optional<T>` for any supported `T`

## Example

```cpp
#include "uredis/RedisClient.h"
#include "uredis/RedisReflect.h"

struct User
{
    int64_t id;
    std::string name;
    bool active;
    std::optional<int64_t> age;
};

task::Awaitable<void> reflect_example()
{
    RedisConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 15100;

    RedisClient client{cfg};
    co_await client.connect();

    using namespace usub::uredis::reflect;

    User u{.id = 42, .name = "Kirill", .active = true, .age = 30};

    auto hset_res = co_await hset_struct(client, "user:42", u);

    auto loaded = co_await hget_struct<User>(client, "user:42");
    if (loaded && loaded.value().has_value())
    {
        const User& u2 = *loaded.value();
        // use u2
    }

    co_return;
}
```

On the Redis side this is equivalent to:

```text
HSET user:42 id 42 name "Kirill" active 1 age 30
HGETALL user:42
```

## Behavior

* `hset_struct`:

    * Uses `ureflect::for_each_field` to iterate fields.
    * For each field `(name, value)` pushes `name` and `string(value)` into args.
    * Executes `HSET key field1 value1 field2 value2 ...`.
    * Returns Redis integer reply (number of new fields added).

* `hget_struct`:

    * Calls `HGETALL key`.
    * Builds a `std::unordered_map<std::string, std::string>`.
    * For each struct field:

        * Looks up map by field name.
        * Converts string into field type with `from_redis_string`.
    * Returns `std::optional<T>`:

        * `std::nullopt` if hash does not exist or is empty.
        * `T` filled with decoded fields otherwise.