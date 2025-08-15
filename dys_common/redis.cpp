#define BOOST_SCOPE_EXIT_CONFIG_USE_LAMBDAS
#include "redis.h"
#include "log.h"
#include <boost/scope_exit.hpp>
#include <chrono>
#include <functional>

namespace base {
RedisReply::RedisReply(Error e) {
    v = e;
}
RedisReply::RedisReply(redisReply *r)
    : v(Error{"Redis: unknown reply type"}) {
    if (r == nullptr) {
        return;
    }
    BOOST_SCOPE_EXIT(r) {
        freeReplyObject(r);
    };
    switch (r->type) {
        case (REDIS_REPLY_STRING):
            v = std::string(r->str, r->len);
            break;
        case (REDIS_REPLY_ARRAY):
            v = std::vector<RedisReply>(r->element, r->element + r->elements);
            std::fill_n(r->element, r->elements, nullptr);
            break;
        case (REDIS_REPLY_INTEGER):
            v = r->integer;
            break;
        case (REDIS_REPLY_NIL):
            v = redis::null{};
            break;
        case (REDIS_REPLY_STATUS):
            v = true;
            break;
        case (REDIS_REPLY_ERROR):
            v = Error(std::string(r->str, r->len));
            break;
    }
}
bool RedisReply::ok() const noexcept {
    return v.type() != typeid(Error);
}
Error RedisReply::error() const {
    return get<Error>();
}
std::string RedisReply::errorString() const {
    return get<Error>()();
}
bool RedisReply::empty() const noexcept {
    return is<redis::null>();
}

RedisClusterClient::RedisClusterClient(RedisClusterClient &&c)
    : ctx(std::move(c.ctx)) {}
RedisClusterClient &RedisClusterClient::operator=(RedisClusterClient &&c) {
    ctx = std::move(c.ctx);
    return *this;
}
RedisClusterClient::RedisClusterClient(const char *addrs, const TimeoutParam& timeout, bool use_slots)
    : ctx(redisClusterConnectWithTimeout(addrs, toTimeval(timeout.oneSlotTimeout), use_slots ? HIRCLUSTER_FLAG_ROUTE_USE_SLOTS : HIRCLUSTER_FLAG_NULL),
          redisClusterFree), tm(timeout) {
    if (!ok()) {
        return;
    }
#ifdef AI_BUILD_WITH_BAZEL
    redisClusterSetOptionTimeout(ctx.get(), toTimeval(timeout.totalTimeout));
#endif  // AI_BUILD_WITH_BAZEL
    redisClusterSetMaxRedirect(ctx.get(), 0);
    redisClusterSetMaxConnFailed(ctx.get(), 3);
}
RedisClusterClient::RedisClusterClient(const Error &e)
    : err(e) {}
bool RedisClusterClient::ok() const noexcept {
    return ctx.get() != nullptr && ctx->err == 0;
}
std::string RedisClusterClient::errorString() const noexcept {
    if (ctx.get() == nullptr) {
        return err();
    }
    if (ctx->err != 0) {
        return std::string("Redis: ") + ctx->errstr;
    }
    return "";
}
bool RedisClusterClient::reset() noexcept {
    if (ctx.get() == nullptr) {
        return false;
    }
    redisClusterReset(ctx.get());
    return true;
}
bool RedisClusterClient::empty() const noexcept {
    return ctx.get() == nullptr;
}
timeval RedisClusterClient::toTimeval(const duration d) {
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(d);
    auto usec = std::chrono::duration_cast<std::chrono::microseconds>(d - sec);
    return timeval{
        .tv_sec = static_cast<decltype(std::declval<timeval>().tv_sec)>(sec.count()),
        .tv_usec = static_cast<decltype(std::declval<timeval>().tv_usec)>(usec.count()),
    };
}

// RedisHandler
RedisHandler::RedisHandler(const std::string &name)
    : name(name), db(&dbs.at(name)), clt(db->pool.New()) {}
RedisHandler::~RedisHandler() {
    if (clt.get() == nullptr) {
        return;
    }
    if (!clt->ok() && clt->empty()) {
        dbs.at(name).pool.Drop();
        return;
    }
    dbs.at(name).pool.Delete(std::move(clt));
}

void RedisHandler::setRedisConfig(const std::string &name, const std::string &addrs, const TimeoutParam &timeout, const RetryParam &retryParam, const uint32_t threshhold, bool use_slots) noexcept {
    dbs.emplace(std::piecewise_construct,
                std::forward_as_tuple(name),
                std::forward_as_tuple(addrs, timeout, threshhold, use_slots));
    retry.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(RetryParam{
        .quota = retryParam.quota,
        .limit = retryParam.limit,
    }));
    m.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple());
    retryTimes.emplace(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple());
}

void RedisHandler::setRedisConfig(const std::string &name, const std::string &addrs, const TimeoutParam &timeout, const RetryParam &retryParam) noexcept {
    setRedisConfig(name, addrs, timeout, retryParam, 20);
}
void RedisHandler::setLimit(const std::string &name, size_t limit, size_t preAlloc) noexcept {
    if (preAlloc > limit) {
        preAlloc = limit;
    }
    dbs.at(name).pool.SetLimit(limit);
    dbs.at(name).pool.PreAllocate(preAlloc);
}
RedisHandler::redisClientCreator RedisHandler::newPoolFunc(
    CircuitBreaker &breaker, std::atomic<bool> &connecting,
    const std::string &addrs, const TimeoutParam& timeout, bool use_slots) {
    std::function<RedisClusterClient *(void)> f = [addrs, timeout, use_slots]() {
        return new RedisClusterClient(addrs.c_str(), timeout, use_slots);
    };
    return [f, &breaker, &connecting]() {
        bool expected = false;
        if (!connecting.compare_exchange_strong(expected, true)) {
            return std::make_unique<RedisClusterClient>(
                Error("RedisHandler: already connecting"));
        }
        auto res = breaker.run(f);
        connecting.store(false);
        if (!res.ok()) {
            return std::make_unique<RedisClusterClient>(Error(res.errorString()));
        }
        return std::unique_ptr<RedisClusterClient>(res.unwrap());
    };
}
bool RedisHandler::ok() const noexcept {
    return clt.get() != nullptr && clt->ok();
}
Error RedisHandler::error() const noexcept {
    if (clt.get() == nullptr) {
        return Error{"RedisHandler: too many redis clients"};
    }
    if (!clt->ok()) {
        return Error{clt->errorString()};
    }
    return {};
}

uint16_t RedisHandler::retryCount() const {
    auto now = static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    auto &queue = retryTimes.at(name);
    std::lock_guard<std::mutex> guard(m.at(name));
    while (!queue.empty() && queue.front() + 60 < now) {
        queue.pop();
    }
    return queue.size();
}

void RedisHandler::retryInc() const {
    auto now = static_cast<time_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    std::lock_guard<std::mutex> guard(m.at(name));
    retryTimes.at(name).push(now);
}
std::unordered_map<std::string, RedisHandler::DB> RedisHandler::dbs{};
std::unordered_map<std::string, RetryParam> RedisHandler::retry{};
std::unordered_map<std::string, std::queue<time_t>> RedisHandler::retryTimes{};
std::unordered_map<std::string, std::mutex> RedisHandler::m{};
} // namespace base
