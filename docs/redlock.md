# Redlock (Distributed Locks)

uRedis includes an optional implementation of **Redlock**, a distributed lock algorithm proposed for coordinating work across multiple independent Redis instances.

!!! Redlock is not a strong consensus algorithm. Use only where *best-effort distributed locks* are acceptable.

## Features

- Acquires lock on **quorum** of nodes (N/2 + 1)
- TTL-based self-expiration
- Fully async
- One-shot unlock across all nodes
- Optional construction from:
  - Redis configs
  - Existing RedisClient instances (Sentinel / Cluster pools)

## API

### Construct from configs

```cpp
RedlockConfig cfg;
cfg.nodes = {
    RedisConfig{"127.0.0.1", 15100},
    RedisConfig{"127.0.0.1", 15101},
    RedisConfig{"127.0.0.1", 15102}
};

RedisRedlock lock(cfg);
co_await lock.connect_all();
```

### Construct from existing clients

```cpp
std::vector<std::shared_ptr<RedisClient>> clients = {
    client1, client2, client3
};

RedisRedlock lock(clients, cfg);
```

### Acquire / Release

```cpp
auto h = co_await lock.lock("lock:order:42");

co_await lock.unlock(h);
```