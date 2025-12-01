# Sentinel Support

uRedis provides first-class support for Redis Sentinel through:

- `resolve_master_from_sentinel`
- `RedisSentinelPool`

Sentinel is used to auto-discover the current **master node**, track failovers, and reconnect pools automatically.

## Key Features

- Automatic master discovery (`SENTINEL get-master-addr-by-name`)
- Full async API
- Auto-reconnect on I/O errors
- One-time retry of failed commands after re-resolve
- Fully integrated with `RedisPool`

## Example

```cpp
RedisSentinelConfig cfg;
cfg.master_name = "mymaster";
cfg.sentinels = {
    {"127.0.0.1", 26379},
};

RedisSentinelPool pool{cfg};
co_await pool.connect();

auto v = co_await pool.command("INCRBY", "counter", "1");
```