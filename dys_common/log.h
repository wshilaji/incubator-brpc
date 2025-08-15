#ifndef BASE_LOG_H
#define BASE_LOG_H

#include <boost/log/common.hpp>
#include <boost/log/trivial.hpp>
#include <fmt/ostream.h>
#include <fmt/printf.h>
#include <fmt/time.h>
#include <iostream>

namespace base {

    namespace impl {
        extern boost::log::trivial::severity_level CONFIGURED_LOG_LEVEL;
    }

bool initPLogger(const std::string &dir, const std::string &prefix, const std::string &lvlStr);
void stopPLogger();
namespace impl {
bool ploggerIsInited();
bool ploggerIsStop();
constexpr const char *trimToFilename(const char *path) {
    const char *pre = path;
    for (; *path != '\0'; ++path) {
        if (*path == '/') {
            pre = path;
        }
    }
    return pre + 1;
}
typedef boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level, std::string> plogger_type_mt;
template <typename Logger>
void writeLog(Logger &l, boost::log::trivial::severity_level lvl, const std::string &msg) {
    if (ploggerIsInited()) {
        BOOST_LOG_SEV(l, lvl) << msg;
    } else {
        auto now = fmt::localtime(std::time(nullptr));
        std::cout << fmt::format("{:%Y-%m-%d %X} [{}]{}", now, lvl, msg) << std::endl;
    }
}
std::string s_alog_assert_func(bool condition);
} // namespace impl
BOOST_LOG_INLINE_GLOBAL_LOGGER_INIT(plogger_service, impl::plogger_type_mt) {
    return impl::plogger_type_mt(boost::log::keywords::channel = "service");
}
BOOST_LOG_INLINE_GLOBAL_LOGGER_INIT(plogger_audit, impl::plogger_type_mt) {
    return impl::plogger_type_mt(boost::log::keywords::channel = "audit");
}
BOOST_LOG_INLINE_GLOBAL_LOGGER_INIT(plogger_recall_audit,
                                    impl::plogger_type_mt) {
    return impl::plogger_type_mt(boost::log::keywords::channel = "recall");
}
BOOST_LOG_INLINE_GLOBAL_LOGGER_INIT(plogger_galaxy_dump_audit,
                                    impl::plogger_type_mt) {
    return impl::plogger_type_mt(boost::log::keywords::channel = "dump");
}

#define LEVEL_TO_BOOST_LEVEL(lvl) ::boost::log::trivial::severity_level::lvl

#define ALOG(lvl, ...) {                                                                                                                    \
    if (!::base::impl::ploggerIsInited()) {                                                                                                 \
        auto now = fmt::localtime(std::time(nullptr));                                                                                      \
        std::cout << fmt::format("{:%Y-%m-%d %X} [{}]{}", now, LEVEL_TO_BOOST_LEVEL(lvl)                                                    \
                , ::fmt::format(" {}:{} ", ::base::impl::trimToFilename(__FILE__), __LINE__) + ::fmt::sprintf(__VA_ARGS__)) << std::endl;   \
    } else if (LEVEL_TO_BOOST_LEVEL(lvl) >= ::base::impl::CONFIGURED_LOG_LEVEL) {                                                           \
        std::string __msg__ = ::fmt::format(" {}:{} ", ::base::impl::trimToFilename(__FILE__), __LINE__) + ::fmt::sprintf(__VA_ARGS__);     \
        BOOST_LOG_SEV(base::plogger_service::get(), LEVEL_TO_BOOST_LEVEL(lvl)) << __msg__;                                                  \
    }                                                                                                                                       \
}

#define ALOG_IF(lvl, condition, ...) {       \
    if (condition) {                         \
        ALOG(lvl, __VA_ARGS__);              \
    }                                        \
}

#define ALOG_ASSERT(condition)                                           \
    ALOG_IF(fatal, !(condition), "Assert failed: " #condition ". ");     \
    assert(condition);

#define ALOG_CHECK(condition, ...)             \
    ALOG_IF(error, !(condition), __VA_ARGS__);

#define ALOG_AUDIT(lvl, ...) {                                                                                                              \
    if (!::base::impl::ploggerIsInited()) {                                                                                                 \
        auto now = fmt::localtime(std::time(nullptr));                                                                                      \
        std::cout << fmt::format("{:%Y-%m-%d %X} [{}]{}", now, LEVEL_TO_BOOST_LEVEL(lvl)                                                    \
                , " " + ::fmt::sprintf(__VA_ARGS__)) << std::endl;                                                                          \
    } else if (LEVEL_TO_BOOST_LEVEL(lvl) >= ::base::impl::CONFIGURED_LOG_LEVEL) {                                                           \
        std::string __msg__ = " " + ::fmt::sprintf(__VA_ARGS__);                                                                            \
        BOOST_LOG_SEV(base::plogger_audit::get(), LEVEL_TO_BOOST_LEVEL(lvl)) << __msg__;                                                    \
    }                                                                                                                                       \
}

#define ALOG_RECALL(lvl, ...) {                                                                                                              \
    if (!::base::impl::ploggerIsInited()) {                                                                                                 \
        auto now = fmt::localtime(std::time(nullptr));                                                                                      \
        std::cout << fmt::format("{:%Y-%m-%d %X} [{}]{}", now, LEVEL_TO_BOOST_LEVEL(lvl)                                                    \
                , " " + ::fmt::sprintf(__VA_ARGS__)) << std::endl;                                                                          \
    } else if (LEVEL_TO_BOOST_LEVEL(lvl) >= ::base::impl::CONFIGURED_LOG_LEVEL) {                                                           \
        std::string __msg__ = " " + ::fmt::sprintf(__VA_ARGS__);                                                                            \
        BOOST_LOG_SEV(base::plogger_recall_audit::get(), LEVEL_TO_BOOST_LEVEL(lvl)) << __msg__;                                                    \
    }                                                                                                                                       \
}

#define ALOG_GALAXY_DUMP(lvl, ...) {                                                                                                              \
    if (!::base::impl::ploggerIsInited()) {                                                                                                 \
        auto now = fmt::localtime(std::time(nullptr));                                                                                      \
        std::cout << fmt::format("{:%Y-%m-%d %X} [{}]{}", now, LEVEL_TO_BOOST_LEVEL(lvl)                                                    \
                , " " + ::fmt::sprintf(__VA_ARGS__)) << std::endl;                                                                          \
    } else if (LEVEL_TO_BOOST_LEVEL(lvl) >= ::base::impl::CONFIGURED_LOG_LEVEL) {                                                           \
        std::string __msg__ = " " + ::fmt::sprintf(__VA_ARGS__);                                                                            \
        BOOST_LOG_SEV(base::plogger_galaxy_dump_audit::get(), LEVEL_TO_BOOST_LEVEL(lvl)) << __msg__;                                                    \
    }                                                                                                                                       \
}

#define LEVEL_TO_BOOST_LEVEL_TRACE ::boost::log::trivial::severity_level::trace
#define LEVEL_TO_BOOST_LEVEL_trace ::boost::log::trivial::severity_level::trace
#define LEVEL_TO_BOOST_LEVEL_DEBUG ::boost::log::trivial::severity_level::debug
#define LEVEL_TO_BOOST_LEVEL_debug ::boost::log::trivial::severity_level::debug
#define LEVEL_TO_BOOST_LEVEL_INFO ::boost::log::trivial::severity_level::info
#define LEVEL_TO_BOOST_LEVEL_info ::boost::log::trivial::severity_level::info
#define LEVEL_TO_BOOST_LEVEL_NOTICE ::boost::log::trivial::severity_level::info
#define LEVEL_TO_BOOST_LEVEL_notice ::boost::log::trivial::severity_level::info
#define LEVEL_TO_BOOST_LEVEL_WARNING ::boost::log::trivial::severity_level::warning
#define LEVEL_TO_BOOST_LEVEL_warning ::boost::log::trivial::severity_level::warning
#define LEVEL_TO_BOOST_LEVEL_ERROR ::boost::log::trivial::severity_level::error
#define LEVEL_TO_BOOST_LEVEL_error ::boost::log::trivial::severity_level::error
#define LEVEL_TO_BOOST_LEVEL_FATAL ::boost::log::trivial::severity_level::fatal
#define LEVEL_TO_BOOST_LEVEL_fatal ::boost::log::trivial::severity_level::fatal
#define USE_BOOST_LOG(lvl) BOOST_LOG_SEV(::base::plogger_service::get(), LEVEL_TO_BOOST_LEVEL_##lvl) \
    << ::fmt::format(" {}:{} ", ::base::impl::trimToFilename(__FILE__), __LINE__)
#define S_ALOG(lvl) if (!::base::impl::ploggerIsStop() && LEVEL_TO_BOOST_LEVEL_##lvl >= ::base::impl::CONFIGURED_LOG_LEVEL) USE_BOOST_LOG(lvl)
#define S_ALOG_IF(lvl, condition) if (!::base::impl::ploggerIsStop() && condition) S_ALOG(lvl)
#define S_ALOG_CHECK(condition) S_ALOG_IF(error, !(condition))
#define S_ALOG_ASSERT(condition) S_ALOG_IF(fatal, !(condition)) << "Assert failed: " #condition ". " << ::base::impl::s_alog_assert_func(condition)

} // namespace base

#endif // BASE_LOG_H
