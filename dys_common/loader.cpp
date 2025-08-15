#include "loader.h"

#include <unistd.h>

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <fstream>
#include <memory>
#include <regex>
#include <string>
#include <utility>

extern "C" {
#include <sys/stat.h>
}

namespace base {

static void MaybeSetVersionFromSymbolicLink(
    const char* const filename,
    ThreadSafeSharePtr<std::string>* version) {
  struct stat st;
  if (lstat(filename, &st) == 0 && S_ISLNK(st.st_mode)) {
    std::string value(st.st_size + 1, '\0');
    // TODO: 在 C++17 后可以使用更加直观的 `value.data()`.
    int ret = readlink(filename, &value.front(), st.st_size + 1);
    if (ret < 0) {  // NOTE: 发生错误.
      value.clear();
    } else if (ret == st.st_size) {  // NOTE: 长度一致.
      value.resize(st.st_size);
    } else if (ret < st.st_size) {  // NOTE: 长度不一致且新内容较短.
      value.resize(ret);
    } else {  // NOTE: 长度不一致且新内容较长.
      // TODO: 可以考虑 "清空" 或者 "再读一次", 目前实现方案是 "忍了".
    }
    version->set(std::make_shared<std::string>(std::move(value)));
  }
}

std::pair<int64_t, int64_t> getMemoryUsed() {
    std::fstream fs{"/proc/self/status"};
    std::regex vm_size_re{R"(VmSize:\s+(\d+)\s+kB)"};
    std::regex rss_size_re{R"(VmRSS:\s+(\d+)\s+kB)"};

    int64_t vm_kB = 0;
    int64_t rss_kB = 0;
    std::smatch m;
    for (std::string line; std::getline(fs, line);) {
        std::regex_match(line, m, vm_size_re);
        if (!m.empty()) {
            vm_kB = std::stoll(m[1]);
            continue;
        }
        std::regex_match(line, m, rss_size_re);
        if (!m.empty()) {
            rss_kB = std::stoll(m[1]);
            continue;
        }
    }

    return {vm_kB, rss_kB};
}

Result<bool> FileSource::load() noexcept {
    struct stat status;
    if (lstat(path.c_str(), &status) != 0) {
        return Error("file not found");
    }
    auto t = std::chrono::system_clock::from_time_t(status.st_mtime);
    if (t == mtime) {
        return false;
    }
    // mapping empty file throws exception, but is allowed.
    if (stat(path.c_str(), &status) == 0 && status.st_size == 0) {
        return true;
    }
    try {
        file = boost::iostreams::mapped_file_source(path);
        mtime = t;
    } catch (...) {
        return Error(std::current_exception());
    }
    return true;
}
boost::iostreams::mapped_file_source FileSource::get() const noexcept {
    return file;
}

void FileSource::setVersion() noexcept{
    MaybeSetVersionFromSymbolicLink(path.c_str(), &_version_ptr);
}

Versions FileSource::versions() const noexcept {
    return {{path, *_version_ptr.get()}};
}

std::size_t FileSource::getSize() const noexcept {
    return file.size();
}

std::string FileSource::getType() const noexcept {
    return "FileSource";
}

void FileSource::release() noexcept {
    file.close();
}

Result<bool> StrictlyConsistentWDModelSource::load() noexcept {
    std::vector<std::chrono::system_clock::time_point> tmp_mtimes(depend_mtimes.size());
    for (size_t i=0; i<depend_files.size(); ++i) {
        std::string check_path = depend_files[i];
        struct stat status;
        if (lstat(check_path.c_str(), &status) != 0) {
            std::string err_msg = check_path + " file not found";
            if ((check_path == path) && (loaded == 1)) {
                // skip the check
                continue;
            }
            return Error(err_msg);
        }
        auto t = std::chrono::system_clock::from_time_t(status.st_mtime);
        if ((check_path == path) && (loaded == 1)) {
            // will skip the check
            tmp_mtimes[i] = t;
            continue;
        }
        if (t == depend_mtimes[i]) {
            return false;
        }
        tmp_mtimes[i] = t;
    }
    // switch mtime
    depend_mtimes = tmp_mtimes;
    // we only store param path as the version
    MaybeSetVersionFromSymbolicLink(path.c_str(), &_version_ptr);
    return true;
}
std::string StrictlyConsistentWDModelSource::get() const noexcept {
    return path;
}

void StrictlyConsistentWDModelSource::setVersion() noexcept{
    MaybeSetVersionFromSymbolicLink(path.c_str(), &_version_ptr);
    loaded = 1;
}

Versions StrictlyConsistentWDModelSource::versions() const noexcept {
    return {{path, *_version_ptr.get()}};
}

std::size_t StrictlyConsistentWDModelSource::getSize() const noexcept {
    return path.size();
}

std::string StrictlyConsistentWDModelSource::getType() const noexcept {
    return "StrictlyConsistentWDModelSource";
}

void StrictlyConsistentWDModelSource::release() noexcept {
    return;
}

Result<bool> FilePathSource::load() noexcept {
    struct stat status;
    if (lstat(path.c_str(), &status) != 0) {
        return Error("file not found");
    }
    auto t = std::chrono::system_clock::from_time_t(status.st_mtime);
    if (t == mtime) {
        return false;
    }
    mtime = t;
    MaybeSetVersionFromSymbolicLink(path.c_str(), &_version_ptr);
    return true;
}
std::string FilePathSource::get() const noexcept {
    return path;
}

void FilePathSource::setVersion() noexcept{
    MaybeSetVersionFromSymbolicLink(path.c_str(), &_version_ptr);
}

Versions FilePathSource::versions() const noexcept {
    return {{path, *_version_ptr.get()}};
}

std::size_t FilePathSource::getSize() const noexcept {
    return path.size();
}

std::string FilePathSource::getType() const noexcept {
    return "FilePathSource";
}

void FilePathSource::release() noexcept {
    return;
}

Result<bool> ModelSource::load() noexcept {
    struct stat p_status, f_status, s_status;
    if (lstat(_params_path.c_str(), &p_status) != 0 ||
        lstat(_feature_path.c_str(), &f_status) != 0 ||
        lstat(_symbol_path.c_str(), &s_status) != 0) {
        return Error("missing files");
    }
    auto t = std::chrono::system_clock::from_time_t(p_status.st_mtime);
    if (t == mtime) {
        return false;
    }
    mtime = t;
    this->setVersion();
    return true;
}

std::string ModelSource::get() const noexcept {
    return _params_path;
}

void ModelSource::setVersion() noexcept{
    MaybeSetVersionFromSymbolicLink(_params_path.c_str(), &_params_version);
    MaybeSetVersionFromSymbolicLink(_feature_path.c_str(), &_feature_version);
    MaybeSetVersionFromSymbolicLink(_symbol_path.c_str(), &_symbol_version);
}

Versions ModelSource::versions() const noexcept {
    Versions versions;
    versions[_params_path] = *_params_version.get();
    versions[_feature_path] = *_feature_version.get();
    versions[_symbol_path] = *_symbol_version.get();
    return versions;
}

std::size_t ModelSource::getSize() const noexcept {
    return model_name.size();
}

std::string ModelSource::getType() const noexcept {
    return "ModelSource";
}

void ModelSource::release() noexcept {
    return;
}

Result<bool> TFModelSource::load() noexcept {
    struct stat p_status, f_status, s_status;
    if (lstat(_params_path.c_str(), &p_status) != 0 ||
        lstat(_feature_path.c_str(), &f_status) != 0 ||
        lstat(_symbol_path.c_str(), &s_status) != 0) {
        return Error("missing files");
    }
    auto t = std::chrono::system_clock::from_time_t(p_status.st_mtime);
    if (t == mtime) {
        return false;
    }
    mtime = t;
    this->setVersion();
    return true;
}

std::string TFModelSource::get() const noexcept {
    return model_name;
}

void TFModelSource::setVersion() noexcept{
    MaybeSetVersionFromSymbolicLink(model_name.c_str(), &_model_version);
    ALOG(info, "TFModelSource version:%s", _model_version.get()->c_str());
}

Versions TFModelSource::versions() const noexcept {
    Versions versions;
    versions[model_name] = *_model_version.get();
    return versions;
}

std::size_t TFModelSource::getSize() const noexcept {
    return model_name.size();
}

std::string TFModelSource::getType() const noexcept {
    return "TFModelSource";
}

void TFModelSource::release() noexcept {
    return;
}

Result<bool> LocalTableSource::load() noexcept {
    struct stat meta_status;
    std::string error_msg;
    if (lstat(_meta_path.c_str(), &meta_status) != 0) {
        std::string error_msg("meta file missing");
        error_msg.append(":");
        error_msg.append(_meta_path);
        return Error(error_msg);
    }
    auto t = std::chrono::system_clock::from_time_t(meta_status.st_mtime);
    if (t == mtime) {
        return false;
    }
    mtime = t;
    this->setVersion();
    return true;
}

std::string LocalTableSource::get() const noexcept {
    return table_name;
}

void LocalTableSource::setVersion() noexcept{
    MaybeSetVersionFromSymbolicLink(table_name.c_str(), &_table_version);
    ALOG(info, "LocalTableSource version:%s", _table_version.get()->c_str());
}

Versions LocalTableSource::versions() const noexcept {
    Versions versions;
    versions[table_name] = *_table_version.get();
    return versions;
}

std::size_t LocalTableSource::getSize() const noexcept {
    return table_name.size();
}

std::string LocalTableSource::getType() const noexcept {
    return "LocalTableSource";
}

void LocalTableSource::release() noexcept {
    return;
}

Result<bool> RedisSource::load() noexcept {
    auto now = clk::now();
    if (now < mtime + timeout) {
        return false;
    }
    auto r = RedisHandler(db).Exec(cmd.data());
    if (!r.ok()) {
        return r.error();
    }
    reply = r;
    mtime = now;
    return true;
}
RedisReply RedisSource::get() const noexcept {
    return reply;
}

void RedisSource::setVersion() noexcept{
}

Versions RedisSource::versions() const noexcept {
    using namespace std::chrono;
    auto st = duration_cast<seconds>(mtime.time_since_epoch()).count();
    return {{cmd, std::to_string(st)}};
}

std::size_t RedisSource::getSize() const noexcept {
    return reply.getSize();
}

std::string RedisSource::getType() const noexcept {
    return "RedisSource";
}

void RedisSource::release() noexcept {
    return;
}

Result<bool> RegularSource::load() noexcept {
    auto now = clk::now();
    if (now < mtime + timeout) {
        return false;
    }
    mtime = now;
    return true;
}
bool RegularSource::get() const noexcept {
    return true;
}
Versions RegularSource::versions() const noexcept {
    using namespace std::chrono;
    auto st = duration_cast<seconds>(mtime.time_since_epoch()).count();
    return {{resource, std::to_string(st)}};
}

void RegularSource::setVersion() noexcept {
}

std::size_t RegularSource::getSize() const noexcept {
    return 0;
}

std::string RegularSource::getType() const noexcept {
    return "RegularSource";
}

void RegularSource::release() noexcept {
    return;
}

} // namespace base
