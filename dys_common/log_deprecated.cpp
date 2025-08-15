#include "log_deprecated.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include <boost/log/common.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup.hpp>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace keywords = boost::log::keywords;
namespace sinks = boost::log::sinks;
namespace expr = boost::log::expressions;
namespace trivial = boost::log::trivial;

namespace base {
using boost::log::trivial::severity_level;

BOOST_LOG_INLINE_GLOBAL_LOGGER_DEFAULT(my_logger, src::logger_mt)
boost::log::sources::severity_logger<trivial::severity_level> GLog::_s_slg;
boost::log::trivial::severity_level GLog::_filter_level;
bool GLog::_is_init = false;
size_t GLog::_log_buffer_len;
std::string GLog::_info_log_file;
std::string GLog::_wf_log_file;
boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>> GLog::_info_log_sink;
boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>> GLog::_wf_log_sink;
size_t GLog::_rotation_size;
bool GLog::_auto_flush;
uint64_t GLog::_rotate_interval;
uint64_t GLog::_last_millstone[2];

bool GLog::isInit() {
    return _is_init;
}
boost::log::sources::severity_logger<boost::log::trivial::severity_level> &GLog::getLogger() {
    return _s_slg;
}
int GLog::init(const std::string &log_dir, const std::string &log_file_prefix, const size_t &rotation_size,
               bool auto_flush, size_t log_len_limit, const std::string &log_level, uint64_t interval) {
    boost::log::trivial::severity_level filter_level;
    if (boost::iequals(log_level, "trace")) {
        filter_level = boost::log::trivial::trace;
    } else if (boost::iequals(log_level, "debug")) {
        filter_level = boost::log::trivial::debug;
    } else {
        filter_level = boost::log::trivial::info;
    }
    return init(log_dir, log_file_prefix, rotation_size, auto_flush, log_len_limit, filter_level, interval);
}
int GLog::init(const std::string &log_dir, const std::string &log_file_prefix, const size_t &rotation_size,
               bool auto_flush, size_t log_len_limit,
               boost::log::trivial::severity_level filter_level, uint64_t interval) {
    if (boost::filesystem::exists(log_dir) == false) {
        boost::filesystem::create_directories(log_dir);
    }

    if (_is_init == true) {
        LOG_FATAL("log have inited before, re-init is not allowed!");
        return -1;
    }

    _log_buffer_len = log_len_limit;
    _filter_level = filter_level;
    _rotation_size = rotation_size;
    _auto_flush = auto_flush;
    _rotate_interval = interval;
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    _last_millstone[0] = tt / interval;
    _last_millstone[1] = _last_millstone[0];

    _wf_log_file = log_dir + "/" + log_file_prefix + ".log.wf";
    _wf_log_sink = logging::add_file_log(
        keywords::open_mode = std::ios::app,
        keywords::file_name = _wf_log_file + ".%Y%m%d%H",
        keywords::time_based_rotation = std::bind(&base::GLog::isTimeToRotate, 1),
        keywords::filter = expr::attr<severity_level>("Severity") != trivial::info,
        keywords::format =
            (expr::stream
             << expr::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S")
             << " " << expr::attr<boost::log::aux::thread::id>("ThreadID")
             << " [" << logging::trivial::severity
             << "] " << expr::smessage));
    _wf_log_sink->locked_backend()->auto_flush(_auto_flush);

    // 打开正常请求日志
    _info_log_file = log_dir + "/" + log_file_prefix + ".log";
    _info_log_sink = logging::add_file_log(
        keywords::open_mode = std::ios::app,
        keywords::file_name = _info_log_file + ".%Y%m%d%H",
        keywords::time_based_rotation = std::bind(&base::GLog::isTimeToRotate, 0),
        keywords::filter = expr::attr<severity_level>("Severity") == trivial::info,
        keywords::format =
            (expr::stream
             << expr::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S")
             << " " << expr::attr<boost::log::aux::thread::id>("ThreadID")
             << " [" << logging::trivial::severity
             << "] " << expr::smessage));
    _info_log_sink->locked_backend()->auto_flush(_auto_flush);

    logging::add_common_attributes();
    _is_init = true;

    return 0;
}
void GLog::writeLog(boost::log::trivial::severity_level log_level, const char *fmt, ...) {
    size_t buffer_len = 2048;
    if (isInit() && log_level < _filter_level) {
        return;
    }
    if (isInit()) {
        buffer_len = _log_buffer_len;
    }
    char msg[buffer_len];
    va_list argptr;
    va_start(argptr, fmt);
    vsnprintf(msg, buffer_len, fmt, argptr);
    if (isInit()) {
        BOOST_LOG_SEV((base::GLog::getLogger()), log_level) << msg;
    } else {
        fprintf(stderr, "GLOG:[%d]%s\n", (int)(log_level), msg);
    }
    va_end(argptr);
    return;
}
bool GLog::isTimeToRotate(uint8_t idx) {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    auto ms = std::chrono::system_clock::to_time_t(now) / _rotate_interval;
    if (ms > _last_millstone[idx]) {
        _last_millstone[idx] = ms;
        return true;
    } else {
        return false;
    }
}
}; // namespace base
