#include "log.h"
#include <boost/filesystem.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/log/utility/exception_handler.hpp>
#include <chrono>
#include <string>
#include <unordered_map>

namespace base {
namespace {
using SeverityLevel = boost::log::trivial::severity_level;
std::unordered_map<std::string, SeverityLevel> severityLevelFromStringMap = {
    {"trace", SeverityLevel::trace},
    {"debug", SeverityLevel::debug},
    {"info", SeverityLevel::info},
    {"warning", SeverityLevel::warning},
    {"error", SeverityLevel::error},
    {"fatal", SeverityLevel::fatal},
};
SeverityLevel severityLevelFromString(const std::string &s) noexcept {
    return severityLevelFromStringMap.at(s);
}
std::unordered_map<std::string, time_t> millstone;
bool isTimeToRotate(const std::string &suffix) {
    using clock = std::chrono::system_clock;
    auto ms = clock::to_time_t(clock::now()) / 60 * 60; //
    if (ms > millstone.at(suffix)) {
        millstone.at(suffix) = ms;
        return true;
    }
    return false;
}
bool inited = false;
bool isStop = false;
} // namespace

namespace impl {
boost::log::trivial::severity_level CONFIGURED_LOG_LEVEL = boost::log::trivial::severity_level::trace;
bool ploggerIsInited() {
    return inited;
}
bool ploggerIsStop() {
    return isStop;
}
std::string getCurrentTimeZone(){
  auto now = fmt::localtime(std::time(nullptr));
  return fmt::format("{:%z}", now);
}
std::string s_alog_assert_func(bool condition) {
    if (!condition) {
        assert(condition);
    }
    return "";
}
} // namespace impl

void stopPLogger() {
    isStop = true;
}
bool initPLogger(const std::string &dir, const std::string &prefix, const std::string &lvlStr) {
    if (impl::ploggerIsInited()) {
        ALOG(error, "init Plogger multiple times");
        return false;
    }
    using namespace boost::log;
    auto lvl = severityLevelFromString(lvlStr);
    auto tz = impl::getCurrentTimeZone();
    auto addFileLog = [&](const std::string &channel) {
        millstone[channel] = 0; // init insert
        auto s = add_file_log(
            keywords::open_mode = std::ios::app,
            keywords::file_name = dir + "/" + prefix + "." + channel + ".%Y%m%d%H",
            keywords::time_based_rotation = std::bind(&isTimeToRotate, channel),
            keywords::filter = expressions::attr<std::string>("Channel") == channel &&
                               trivial::severity >= lvl,
            keywords::format =
                (expressions::stream
                 << expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%dT%H:%M:%S.%f")
                 << tz
                 << " [" << trivial::severity << "]"
                 << expressions::smessage));
        s->locked_backend()->auto_flush(true);
        return s;
    };
    auto c = core::get();
    c->add_sink(addFileLog("service"));
    c->add_sink(addFileLog("audit"));
    c->add_sink(addFileLog("recall"));
    c->add_sink(addFileLog("dump"));
    add_common_attributes();
    impl::CONFIGURED_LOG_LEVEL = lvl;
    inited = true;
    return true;
}
} // namespace base
