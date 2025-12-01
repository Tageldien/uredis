# Redis Cluster Client

uRedis includes a production-grade async **Redis Cluster client**:

- CRC16 slot hashing  
- Full MOVED / ASK redirection support  
- Automatic `CLUSTER SLOTS` discovery  
- Individual async clients per node  
- Lazy connection buildup  
- Zero dependencies  

Cluster mode is sharded across **16384 slots**.  
Client selects the correct shard for a key and transparently handles migrations.

## Architecture

- `RedisClusterClient`
- Slot table: `slot_to_node[16384]`
- Node list managed dynamically
- Redirection rules:
  - MOVED → update slot mapping + retry
  - ASK → send `ASKING` then retry once

## Example

```cpp
RedisClusterConfig cfg;
cfg.seeds = {
    {"127.0.0.1", 7000},
    {"127.0.0.1", 7001}
};

RedisClusterClient cluster{cfg};
co_await cluster.connect();

co_await cluster.command("SET", "user:1", "Kirill");
auto val = co_await cluster.command("GET", "user:1");
```