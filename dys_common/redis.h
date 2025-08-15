#ifndef BASE_REDIS_H
#define BASE_REDIS_H

#include "circuit_breaker.h"
#include "obj_pool.h"
#include "result.h"
#include <atomic>
#include <boost/variant.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <memory>

extern "C" {
#include "hiredis-vip/hircluster.h"
}

namespace base {
class RedisReply;
namespace redis {
using null = boost::blank;
using integer = decltype(std::declval<redisReply>().integer);
using status = bool;
using string = std::string;
using array = std::vector<RedisReply>;
} // namespace redis
class RedisReply {
public:

    RedisReply(Error e);
    RedisReply(redisReply *r);
    bool empty() const noexcept;
    bool ok() const noexcept;
    Error error() const;
    std::string errorString() const;
    template <typename T>
    T get() const {
        return boost::get<T>(v);
    }
    template <typename T>
    T relaxed_get() const {
        return boost::relaxed_get<T>(v);
    }
    template <typename T>
    bool is() const noexcept {
        return v.type() == typeid(T);
    }
    void setRetry(uint16_t r) {
        retry = r;
    }
    uint16_t getRetry() {
        return retry;
    }
    std::size_t getSize() const noexcept {
        if (is<redis::null>()) {
            return 0;
        } else if (is<redis::integer>()) {
            return sizeof(redis::integer);
        } else if (is<redis::status>()) {
            return sizeof(redis::status);
        } else if (is<redis::string>()) {
            return sizeof(boost::get<redis::string>(v));
        } else if (is<redis::array>()) {
            std::size_t res = 0;
            for (auto &reply : boost::get<redis::array>(v)) {
                res += reply.getSize();
            }
            return res;
        } else if (is<Error>()) {
            return 0;
        }
        return -1;
    }

private:
    boost::variant<redis::null, redis::integer, redis::status, redis::string, redis::array, Error> v;
    uint16_t retry = 0;
};

template <class T>
RedisReply returnRedisReply(T v, uint16_t r) {
    RedisReply result(v);
    result.setRetry(r);
    return result;
}

struct TimeoutParam {
    using duration = std::chrono::system_clock::duration;
    duration oneSlotTimeout = std::chrono::milliseconds(0);
    duration totalTimeout = std::chrono::milliseconds(0);
};

struct RetryParam {
    const uint16_t quota = 0;
    const uint16_t limit = 0;
};

class RedisClusterClient {
    using duration = std::chrono::system_clock::duration;

public:
    RedisClusterClient(const RedisClusterClient &) = delete;
    RedisClusterClient &operator=(const RedisClusterClient &) = delete;
    RedisClusterClient(RedisClusterClient &&c);
    RedisClusterClient &operator=(RedisClusterClient &&c);
    explicit RedisClusterClient(const char *addrs, const TimeoutParam& timeout, bool use_slots = false);
    explicit RedisClusterClient(const Error &e);
    bool ok() const noexcept;
    bool empty() const noexcept;
    std::string errorString() const noexcept;
    template <typename... Args>
    RedisReply Exec(const char *format, Args... args) noexcept {
        auto timeout = toTimeval(tm.totalTimeout);
        RedisReply reply = (redisReply *)redisClusterCommand(ctx.get(), &timeout, format, std::forward<Args>(args)...);
        if (!ok()) {
            return Error(errorString());
        }
        return reply;
    }
    RedisReply Exec(const char *cmd) noexcept {
        auto timeout = toTimeval(tm.totalTimeout);
        RedisReply reply = (redisReply *)redisClusterCmd(ctx.get(), &timeout, cmd);
        if (!ok()) {
            return Error(errorString());
        }
        return reply;
    }
    bool reset() noexcept;

private:
    static timeval toTimeval(const duration d);
    std::unique_ptr<redisClusterContext, std::function<void(redisClusterContext *)>> ctx;
    Error err = Error("Redis: nil");
    TimeoutParam tm;
};

class RedisHandler {
    using duration = std::chrono::system_clock::duration;

public:
    RedisHandler(const std::string &db);
    ~RedisHandler();
    static void setRedisConfig(const std::string &db, const std::string &addrs, const TimeoutParam& param, const RetryParam &retryParam) noexcept;
    static void setRedisConfig(const std::string &db, const std::string &addrs, const TimeoutParam& param, const RetryParam &retryParam, const uint32_t threshhold, bool use_slots = false) noexcept;
    static void setLimit(const std::string &name, size_t limit, size_t preAlloc) noexcept;
    template <typename... Args>
    RedisReply Exec(const char *format, Args... args) noexcept {
        if (clt.get() == nullptr || clt->empty()) {
            return error();
        }
        std::function<RedisReply()> exec = [&] {
            return clt->Exec(format, std::forward<Args>(args)...);
        };
        Error lastErr;
        auto &retryParam = retry.at(name);
        auto curRetry = 0;
        for (; curRetry <= retryParam.limit; ++curRetry) {
            if (!clt->ok()) {
                clt->reset();
            }
            auto res = db->breaker.run(exec);
            if (!res.ok()) {
                return returnRedisReply(res.error(), curRetry);
            }
            auto reply = res.unwrap();
            if (reply.ok()) {
                return returnRedisReply(reply, curRetry);
            }
            if (retryCount() >= retryParam.quota) {
                return returnRedisReply(reply, curRetry);
            }
            lastErr = reply.error();
            retryInc();
        }
        return returnRedisReply(lastErr, curRetry - 1);
    }
    //don't worry thread safe
    static bool CheckRedisDBExists(const std::string &db_name) {
        if (db_name.empty()) {
            return false;
        }
        return dbs.count(db_name) > 0;
    }
    RedisReply Exec(const char *cmd) noexcept {
        if (clt.get() == nullptr || clt->empty()) {
            return error();
        }
        std::function<RedisReply()> exec = [&] {
            return clt->Exec(cmd);
        };
        Error lastErr;
        auto &retryParam = retry.at(name);
        auto curRetry = 0;
        for (; curRetry <= retryParam.limit; ++curRetry) {
            if (!clt->ok()) {
                clt->reset();
            }
            auto res = db->breaker.run(exec);
            if (!res.ok()) {
                return returnRedisReply(res.error(), curRetry);
            }
            auto reply = res.unwrap();
            if (reply.ok()) {
                return returnRedisReply(reply, curRetry);
            }
            if (retryCount() >= retryParam.quota) {
                return returnRedisReply(reply, curRetry);
            }
            lastErr = reply.error();
            retryInc();
        }
        return returnRedisReply(lastErr, curRetry - 1);
    }

  private:
    using redisClientCreator = std::function<std::unique_ptr<RedisClusterClient>()>;
    static redisClientCreator newPoolFunc(
        CircuitBreaker &breaker, std::atomic<bool> &connecting,
        const std::string &addrs, const TimeoutParam& timeout, bool use_slots = false);
    struct DB {
        DB(const std::string &addrs, const TimeoutParam& timeout, uint32_t threshhold, bool use_slots = false)
            : breaker{std::chrono::seconds(1), threshhold},
              pool{newPoolFunc(breaker, connecting, addrs, timeout, use_slots)},
              connecting{false} {}
        CircuitBreaker breaker;
        ObjectPool<RedisClusterClient> pool;
        std::atomic<bool> connecting;
    };
    static std::unordered_map<std::string, DB> dbs;
    static std::unordered_map<std::string, RetryParam> retry;
    static std::unordered_map<std::string, std::mutex> m;
    static std::unordered_map<std::string, std::queue<time_t>> retryTimes;
    std::string name;
    DB *db;
    std::unique_ptr<RedisClusterClient> clt;

    bool ok() const noexcept;
    Error error() const noexcept;
    uint16_t retryCount() const;
    void retryInc() const;
};

} // namespace base

#endif // BASE_REDIS_H
