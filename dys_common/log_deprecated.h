#ifndef BASE_GLOG_H
#define BASE_GLOG_H

#include <boost/log/sinks/sink.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/trivial.hpp>
#include <iostream>
#include <string>

#define LOG_TRACE(_fmt_, args...) \
    base::GLog::writeLog(boost::log::trivial::trace, _fmt_, ##args)

#define LOG_DEBUG(_fmt_, args...) \
    base::GLog::writeLog(boost::log::trivial::debug, "[%s][%d] " #_fmt_, __FILE__, __LINE__, ##args)

#define LOG_INFO(_fmt_, args...) \
    base::GLog::writeLog(boost::log::trivial::info, _fmt_, ##args)

#define LOG_WARNING(_fmt_, args...) \
    base::GLog::writeLog(boost::log::trivial::warning, "[%s][%d] " #_fmt_, __FILE__, __LINE__, ##args)

#define LOG_ERROR(_fmt_, args...) \
    base::GLog::writeLog(boost::log::trivial::error, "[%s][%d] " #_fmt_, __FILE__, __LINE__, ##args)

#define LOG_FATAL(_fmt_, args...) \
    base::GLog::writeLog(boost::log::trivial::error, "[%s][%d] " #_fmt_, __FILE__, __LINE__, ##args)

namespace base {
class GLog {
public:
    GLog() = delete;
    // 在使用之前必须先调用此函数, 只有大于filter_level的日志才会被记录，info、warning、error、fatal一定被记录
    // info记录在$prefix.log中，其他记录在$prefix.log.wf中
    static int init(const std::string &log_dir, const std::string &log_file_prefix,
                    const size_t &rotation_size, bool auto_flush, size_t log_len_limit,
                    boost::log::trivial::severity_level filter_level, uint64_t interval);
    static int init(const std::string &log_dir, const std::string &log_file_prefix,
                    const size_t &rotation_size, bool auto_flush, size_t log_len_limit,
                    const std::string &log_level, uint64_t interval);
    static void writeLog(boost::log::trivial::severity_level log_level, const char *fmt, ...);

private:
    static bool isInit();
    static boost::log::sources::severity_logger<boost::log::trivial::severity_level> &getLogger();
    static bool isTimeToRotate(uint8_t idx);

private:
    static bool _is_init;
    static boost::log::sources::severity_logger<boost::log::trivial::severity_level> _s_slg;
    static boost::log::trivial::severity_level _filter_level;
    static size_t _log_buffer_len;
    static std::string _info_log_file;
    static std::string _wf_log_file;
    static boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>> _info_log_sink;
    static boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>> _wf_log_sink;
    static size_t _rotation_size;
    static bool _auto_flush;
    static uint64_t _last_millstone[2];
    static uint64_t _rotate_interval;
};
}; // namespace base

#endif
