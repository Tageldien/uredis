#include "uredis/RedisClient.h"

#include <charconv>
#include <cstring>
#include <thread>
#include <chrono>

#ifdef UREDIS_LOGS
#include <ulog/ulog.h>
#endif

namespace usub::uredis
{
    using usub::uvent::utils::DynamicBuffer;

    RedisClient::RedisClient(RedisConfig cfg)
        : config_(std::move(cfg))
    {
    }

    RedisClient::~RedisClient()
    {
        this->closing_ = true;
        this->socket_.shutdown();

        if (this->reader_started_)
        {
            while (!this->reader_stopped_.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    task::Awaitable<RedisResult<void>> RedisClient::connect()
    {
        if (this->connected_)
        {
            co_return RedisResult<void>{};
        }

        std::string port_str = std::to_string(this->config_.port);

#ifdef UREDIS_LOGS
        ulog::info("RedisClient::connect: host={} port={}",
                   this->config_.host,
                   this->config_.port);
#endif

        auto res = co_await this->socket_.async_connect(
            this->config_.host.c_str(),
            port_str.c_str());

        if (res.has_value())
        {
            RedisError err{RedisErrorCategory::Io, "async_connect failed"};
            co_return std::unexpected(err);
        }

        this->socket_.set_timeout_ms(this->config_.io_timeout_ms);
        this->connected_ = true;
        this->closing_ = false;

        if (!this->reader_started_)
        {
            this->reader_started_ = true;
            this->reader_stopped_.store(false, std::memory_order_release);
            system::co_spawn(this->reader_loop());
        }

        if (this->config_.password.has_value())
        {
            std::string user;
            std::string pass = *this->config_.password;

            std::array<std::string_view, 2> arr2{};
            std::array<std::string_view, 1> arr1{};

            std::span<const std::string_view> span_args;

            if (this->config_.username.has_value())
            {
                user = *this->config_.username;
                arr2[0] = user;
                arr2[1] = pass;
                span_args = std::span<const std::string_view>(arr2.data(), 2);
            }
            else
            {
                arr1[0] = pass;
                span_args = std::span<const std::string_view>(arr1.data(), 1);
            }

            auto auth_resp = co_await this->command("AUTH", span_args);
            if (!auth_resp)
            {
                co_return std::unexpected(auth_resp.error());
            }
        }

        if (this->config_.db != 0)
        {
            std::string db_str = std::to_string(this->config_.db);
            std::string_view args_arr[1] = {db_str};
            auto resp = co_await this->command(
                "SELECT",
                std::span<const std::string_view>(args_arr, 1));
            if (!resp)
            {
                co_return std::unexpected(resp.error());
            }
        }

        co_return RedisResult<void>{};
    }

    std::vector<uint8_t> RedisClient::encode_command(
        std::string_view cmd,
        std::span<const std::string_view> args)
    {
        std::vector<uint8_t> out;
        std::size_t argc = 1 + args.size();

        out.reserve(64 + cmd.size() + args.size() * 16);

        auto append_str = [&out](std::string_view s)
        {
            out.insert(out.end(),
                       reinterpret_cast<const uint8_t*>(s.data()),
                       reinterpret_cast<const uint8_t*>(s.data()) + s.size());
        };

        auto append_bulk = [&append_str](std::string_view s, std::vector<uint8_t>&)
        {
            std::string len = std::to_string(s.size());
            std::string prefix = "$" + len + "\r\n";
            append_str(prefix);
            append_str(s);
            append_str("\r\n");
        };

        {
            std::string header = "*" + std::to_string(argc) + "\r\n";
            append_str(header);
        }

        append_bulk(cmd, out);
        for (auto& a : args)
        {
            append_bulk(a, out);
        }

        return out;
    }

    task::Awaitable<RedisResult<RedisValue>> RedisClient::command(
        std::string_view cmd,
        std::span<const std::string_view> args)
    {
        if (!this->connected_)
        {
            RedisError err{RedisErrorCategory::Io, "RedisClient not connected"};
            co_return std::unexpected(err);
        }

        auto pending = std::make_shared<PendingRequest>();
        {
            auto guard = co_await this->pending_mutex_.lock();
            this->pending_requests_.push_back(pending);
        }

        std::vector<uint8_t> frame = encode_command(cmd, args);

        {
            auto guard = co_await this->write_mutex_.lock();
            auto wrsz = co_await this->socket_.async_write(frame.data(), frame.size());
            this->socket_.update_timeout(this->config_.io_timeout_ms);

            if (wrsz <= 0 || static_cast<std::size_t>(wrsz) != frame.size())
            {
#ifdef UREDIS_LOGS
                ulog::error("RedisClient::command: async_write failed, wrsz={}", wrsz);
#endif
                RedisError err{RedisErrorCategory::Io, "async_write failed"};
                co_await this->fail_all_pending(RedisErrorCategory::Io, "write error");
                co_return std::unexpected(err);
            }
        }

        co_await pending->event.wait();
        co_return pending->result;
    }

    task::Awaitable<void> RedisClient::fail_all_pending(
        RedisErrorCategory cat,
        std::string_view message)
    {
        std::deque<std::shared_ptr<PendingRequest>> tmp;

        {
            auto guard = co_await this->pending_mutex_.lock();
            tmp.swap(this->pending_requests_);
        }

        for (auto& p : tmp)
        {
            p->result = std::unexpected(
                RedisError{cat, std::string(message)});
            p->event.set();
        }

        co_return;
    }

    task::Awaitable<void> RedisClient::reader_loop()
    {
#ifdef UREDIS_LOGS
        ulog::info("RedisClient::reader_loop: start");
#endif
        static constexpr std::size_t max_read_size = 64 * 1024;
        DynamicBuffer buf;
        buf.reserve(max_read_size);

        while (!this->closing_)
        {
            buf.clear();
            ssize_t rdsz = co_await this->socket_.async_read(buf, max_read_size);
            this->socket_.update_timeout(this->config_.io_timeout_ms);

            if (rdsz <= 0)
            {
#ifdef UREDIS_LOGS
                ulog::info("RedisClient::reader_loop: connection closed, rdsz={}", rdsz);
#endif
                break;
            }

            this->parser_.feed(
                reinterpret_cast<const uint8_t*>(buf.data()),
                buf.size());

            while (true)
            {
                auto val_opt = this->parser_.next();
                if (!val_opt) break;

                std::shared_ptr<PendingRequest> pending;
                {
                    auto guard = co_await this->pending_mutex_.lock();
                    if (this->pending_requests_.empty())
                    {
#ifdef UREDIS_LOGS
                        ulog::error("RedisClient::reader_loop: response without pending request");
#endif
                        continue;
                    }
                    pending = this->pending_requests_.front();
                    this->pending_requests_.pop_front();
                }

                RedisValue v = std::move(*val_opt);

                if (v.type == RedisType::Error)
                {
                    pending->result =
                        std::unexpected(RedisError{
                            RedisErrorCategory::ServerReply,
                            v.as_string()
                        });
                }
                else
                {
                    pending->result = std::move(v);
                }
                pending->event.set();
            }
        }

        this->closing_ = true;
        this->connected_ = false;
        this->socket_.shutdown();

        co_await this->fail_all_pending(
            RedisErrorCategory::Io,
            "connection closed");

#ifdef UREDIS_LOGS
        ulog::info("RedisClient::reader_loop: stop");
#endif
        this->reader_stopped_.store(true, std::memory_order_release);
        co_return;
    }

    task::Awaitable<RedisResult<std::optional<std::string>>> RedisClient::get(
        std::string_view key)
    {
        std::string_view args_arr[1] = {key};
        auto resp = co_await this->command(
            "GET",
            std::span<const std::string_view>(args_arr, 1));
        if (!resp)
        {
            co_return std::unexpected(resp.error());
        }

        const RedisValue& v = *resp;
        if (v.type == RedisType::Null)
        {
            co_return std::optional<std::string>{};
        }

        if (v.type != RedisType::BulkString)
        {
            RedisError err{RedisErrorCategory::Protocol, "GET: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return std::optional<std::string>{v.as_string()};
    }

    task::Awaitable<RedisResult<void>> RedisClient::set(
        std::string_view key,
        std::string_view value)
    {
        std::string_view args_arr[2] = {key, value};
        auto resp = co_await this->command(
            "SET",
            std::span<const std::string_view>(args_arr, 2));
        if (!resp)
        {
            co_return std::unexpected(resp.error());
        }

        const RedisValue& v = *resp;
        if (v.type != RedisType::SimpleString)
        {
            RedisError err{RedisErrorCategory::Protocol, "SET: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return RedisResult<void>{};
    }

    task::Awaitable<RedisResult<void>> RedisClient::setex(
        std::string_view key,
        int ttl_sec,
        std::string_view value)
    {
        std::string ttl = std::to_string(ttl_sec);
        std::string_view args_arr[3] = {key, ttl, value};
        auto resp = co_await this->command(
            "SETEX",
            std::span<const std::string_view>(args_arr, 3));
        if (!resp)
        {
            co_return std::unexpected(resp.error());
        }

        const RedisValue& v = *resp;
        if (v.type != RedisType::SimpleString)
        {
            RedisError err{RedisErrorCategory::Protocol, "SETEX: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return RedisResult<void>{};
    }

    task::Awaitable<RedisResult<int64_t>> RedisClient::del(
        std::span<const std::string_view> keys)
    {
        if (keys.empty())
        {
            co_return int64_t{0};
        }

        std::vector<std::string_view> args;
        args.reserve(keys.size());
        for (auto& k : keys) args.push_back(k);

        auto resp = co_await this->command(
            "DEL",
            std::span<const std::string_view>(args.data(), args.size()));
        if (!resp)
        {
            co_return std::unexpected(resp.error());
        }

        const RedisValue& v = *resp;
        if (v.type != RedisType::Integer)
        {
            RedisError err{RedisErrorCategory::Protocol, "DEL: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return v.as_integer();
    }

    task::Awaitable<RedisResult<int64_t>> RedisClient::incrby(
        std::string_view key,
        int64_t delta)
    {
        std::string delta_str = std::to_string(delta);
        std::string_view args_arr[2] = {key, delta_str};

        auto resp = co_await this->command(
            "INCRBY",
            std::span<const std::string_view>(args_arr, 2));
        if (!resp)
        {
            co_return std::unexpected(resp.error());
        }

        const RedisValue& v = *resp;
        if (v.type != RedisType::Integer)
        {
            RedisError err{RedisErrorCategory::Protocol, "INCRBY: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return v.as_integer();
    }

    task::Awaitable<RedisResult<int64_t>> RedisClient::hset(
        std::string_view key,
        std::string_view field,
        std::string_view value)
    {
        std::string_view args_arr[3] = {key, field, value};
        auto resp = co_await this->command(
            "HSET",
            std::span<const std::string_view>(args_arr, 3));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type != RedisType::Integer)
        {
            RedisError err{RedisErrorCategory::Protocol, "HSET: unexpected type"};
            co_return std::unexpected(err);
        }
        co_return v.as_integer();
    }

    task::Awaitable<RedisResult<std::optional<std::string>>> RedisClient::hget(
        std::string_view key,
        std::string_view field)
    {
        std::string_view args_arr[2] = {key, field};
        auto resp = co_await this->command(
            "HGET",
            std::span<const std::string_view>(args_arr, 2));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type == RedisType::Null)
            co_return std::optional<std::string>{};

        if (v.type != RedisType::BulkString)
        {
            RedisError err{RedisErrorCategory::Protocol, "HGET: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return std::optional<std::string>{v.as_string()};
    }

    task::Awaitable<RedisResult<std::unordered_map<std::string, std::string>>> RedisClient::hgetall(
        std::string_view key)
    {
        std::string_view args_arr[1] = {key};
        auto resp = co_await this->command(
            "HGETALL",
            std::span<const std::string_view>(args_arr, 1));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type == RedisType::Null)
        {
            co_return std::unordered_map<std::string, std::string>{};
        }

        if (v.type != RedisType::Array)
        {
            RedisError err{RedisErrorCategory::Protocol, "HGETALL: unexpected type"};
            co_return std::unexpected(err);
        }

        const auto& arr = v.as_array();
        std::unordered_map<std::string, std::string> out;
        if ((arr.size() & 1) != 0)
        {
            RedisError err{RedisErrorCategory::Protocol, "HGETALL: odd array size"};
            co_return std::unexpected(err);
        }

        for (size_t i = 0; i < arr.size(); i += 2)
        {
            const auto& f = arr[i];
            const auto& val = arr[i + 1];
            if (!f.is_bulk_string() && !f.is_simple_string()) continue;
            if (!val.is_bulk_string() && !val.is_simple_string()) continue;
            out.emplace(f.as_string(), val.as_string());
        }

        co_return out;
    }

    task::Awaitable<RedisResult<int64_t>> RedisClient::sadd(
        std::string_view key,
        std::span<const std::string_view> members)
    {
        if (members.empty())
            co_return int64_t{0};

        std::vector<std::string_view> args;
        args.reserve(1 + members.size());
        args.push_back(key);
        for (auto& m : members)
            args.push_back(m);

        auto resp = co_await this->command(
            "SADD",
            std::span<const std::string_view>(args.data(), args.size()));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type != RedisType::Integer)
        {
            RedisError err{RedisErrorCategory::Protocol, "SADD: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return v.as_integer();
    }

    task::Awaitable<RedisResult<int64_t>> RedisClient::srem(
        std::string_view key,
        std::span<const std::string_view> members)
    {
        if (members.empty())
            co_return int64_t{0};

        std::vector<std::string_view> args;
        args.reserve(1 + members.size());
        args.push_back(key);
        for (auto& m : members)
            args.push_back(m);

        auto resp = co_await this->command(
            "SREM",
            std::span<const std::string_view>(args.data(), args.size()));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type != RedisType::Integer)
        {
            RedisError err{RedisErrorCategory::Protocol, "SREM: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return v.as_integer();
    }

    task::Awaitable<RedisResult<std::vector<std::string>>> RedisClient::smembers(
        std::string_view key)
    {
        std::string_view args_arr[1] = {key};
        auto resp = co_await this->command(
            "SMEMBERS",
            std::span<const std::string_view>(args_arr, 1));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type == RedisType::Null)
        {
            co_return std::vector<std::string>{};
        }

        if (v.type != RedisType::Array)
        {
            RedisError err{RedisErrorCategory::Protocol, "SMEMBERS: unexpected type"};
            co_return std::unexpected(err);
        }

        const auto& arr = v.as_array();
        std::vector<std::string> out;
        out.reserve(arr.size());
        for (auto& it : arr)
        {
            if (!it.is_bulk_string() && !it.is_simple_string())
                continue;
            out.push_back(it.as_string());
        }

        co_return out;
    }

    task::Awaitable<RedisResult<int64_t>> RedisClient::lpush(
        std::string_view key,
        std::span<const std::string_view> values)
    {
        if (values.empty())
            co_return int64_t{0};

        std::vector<std::string_view> args;
        args.reserve(1 + values.size());
        args.push_back(key);
        for (auto& v : values)
            args.push_back(v);

        auto resp = co_await this->command(
            "LPUSH",
            std::span<const std::string_view>(args.data(), args.size()));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type != RedisType::Integer)
        {
            RedisError err{RedisErrorCategory::Protocol, "LPUSH: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return v.as_integer();
    }

    task::Awaitable<RedisResult<std::vector<std::string>>> RedisClient::lrange(
        std::string_view key,
        int64_t start,
        int64_t stop)
    {
        std::string s_start = std::to_string(start);
        std::string s_stop = std::to_string(stop);

        std::string_view args_arr[3] = {key, s_start, s_stop};
        auto resp = co_await this->command(
            "LRANGE",
            std::span<const std::string_view>(args_arr, 3));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type != RedisType::Array)
        {
            RedisError err{RedisErrorCategory::Protocol, "LRANGE: unexpected type"};
            co_return std::unexpected(err);
        }

        const auto& arr = v.as_array();
        std::vector<std::string> out;
        out.reserve(arr.size());
        for (auto& it : arr)
        {
            if (!it.is_bulk_string() && !it.is_simple_string())
                continue;
            out.push_back(it.as_string());
        }

        co_return out;
    }

    task::Awaitable<RedisResult<int64_t>> RedisClient::zadd(
        std::string_view key,
        std::span<const std::pair<std::string, double>> members)
    {
        if (members.empty())
            co_return int64_t{0};

        std::vector<std::string> scores;
        scores.reserve(members.size());
        std::vector<std::string_view> args;
        args.reserve(1 + members.size() * 2);

        args.push_back(key);

        for (auto& m : members)
        {
            std::string s = std::to_string(m.second);
            scores.push_back(s);
            args.push_back(scores.back());
            args.push_back(m.first);
        }

        auto resp = co_await this->command(
            "ZADD",
            std::span<const std::string_view>(args.data(), args.size()));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type != RedisType::Integer)
        {
            RedisError err{RedisErrorCategory::Protocol, "ZADD: unexpected type"};
            co_return std::unexpected(err);
        }

        co_return v.as_integer();
    }

    task::Awaitable<RedisResult<std::vector<std::pair<std::string, double>>>>
    RedisClient::zrange_with_scores(
        std::string_view key,
        int64_t start,
        int64_t stop)
    {
        std::string s_start = std::to_string(start);
        std::string s_stop = std::to_string(stop);

        std::string with = "WITHSCORES";
        std::string_view args_arr[4] = {key, s_start, s_stop, with};

        auto resp = co_await this->command(
            "ZRANGE",
            std::span<const std::string_view>(args_arr, 4));
        if (!resp)
            co_return std::unexpected(resp.error());

        const RedisValue& v = *resp;
        if (v.type != RedisType::Array)
        {
            RedisError err{RedisErrorCategory::Protocol, "ZRANGE: unexpected type"};
            co_return std::unexpected(err);
        }

        const auto& arr = v.as_array();
        std::vector<std::pair<std::string, double>> out;
        if ((arr.size() & 1) != 0)
        {
            RedisError err{RedisErrorCategory::Protocol, "ZRANGE: odd array size"};
            co_return std::unexpected(err);
        }

        out.reserve(arr.size() / 2);
        for (size_t i = 0; i < arr.size(); i += 2)
        {
            const auto& m = arr[i];
            const auto& sc = arr[i + 1];
            if (!m.is_bulk_string() && !m.is_simple_string())
                continue;
            if (!sc.is_bulk_string() && !sc.is_simple_string())
                continue;

            const std::string& member = m.as_string();
            const std::string& score_str = sc.as_string();
            double d = std::stod(score_str);
            out.emplace_back(member, d);
        }

        co_return out;
    }
} // namespace usub::uredis
