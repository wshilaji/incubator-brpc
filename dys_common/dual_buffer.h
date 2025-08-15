//
// Created by wciq1208 on 19-9-2.
//

#ifndef DEPENDENCIES_LIBS_BASE_SRC_DOUBLE_BUFFER_H_
#define DEPENDENCIES_LIBS_BASE_SRC_DOUBLE_BUFFER_H_

#include <atomic>
#include <array>
#include <chrono>
#include <thread>
#include "string_util.h"
#include <boost/iostreams/device/mapped_file.hpp>

namespace base {
namespace impl{
template <class T, class Label, int Interval = 1>
class DualBuffer {
public:
    T *mutableData() {
        return &data_[reload_index()];
    }
    void store() {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (now < Interval + last_updated) {
            std::this_thread::sleep_for(std::chrono::seconds(last_updated + Interval - now));
        }
        cur_index_.store(reload_index());
        if (!inited) {
            data_[reload_index()] = data_[cur_index_];
            inited = true;
        }
        last_updated = now;
    }
    void release() {
        auto ptr = mutableData();
        *ptr = empty_;
        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (now < Interval + last_updated) {
            std::this_thread::sleep_for(std::chrono::seconds(last_updated + Interval - now));
        }
        cur_index_.store(reload_index());
        ptr = mutableData();
        *ptr = empty_;
    }
    const T &data() const {
        return data_[cur_index_];
    }
    bool isInited() {
        return inited;
    }
private:
    static std::array<T, 2> data_;
    static T empty_;
    static std::atomic<size_t> cur_index_;
    static int last_updated;
    static std::atomic_bool inited;
    static size_t reload_index() {
        return 1 - cur_index_.load();
    }
};

template <class T, class Label, int Interval>
std::array<T, 2> DualBuffer<T, Label, Interval>::data_{};

template <class T, class Label, int Interval>
T DualBuffer<T, Label, Interval>::empty_{};

template <class T, class Label, int Interval>
std::atomic<size_t> DualBuffer<T, Label, Interval>::cur_index_{0};

template <class T, class Label, int Interval>
int DualBuffer<T, Label, Interval>::last_updated = 0;
template <class T, class Label, int Interval>
std::atomic_bool DualBuffer<T, Label, Interval>::inited(false);
} // namespace impl
template <class T, class Label, int Interval=1>
class DualBuffer {
public:
    DualBuffer() : new_data_(data_[reload_index()]) {}
    T *mutableData() {
        return &data_[reload_index()];
    }
    void store() {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (now < Interval + last_updated) {
            std::this_thread::sleep_for(std::chrono::seconds(last_updated + Interval - now));
        }
        cur_index_.store(reload_index());
        if (!inited) {
            data_[reload_index()] = new_data_;
            inited = true;
        }
        last_updated = now;
    }
    const T &data() const {
        return new_data_;
    }

    T& data() {
        return new_data_;
    }

private:
    static std::array<T, 2> data_;
    static std::atomic<size_t> cur_index_;
    static int last_updated;
    static std::atomic_bool inited;
    static size_t reload_index() {
        return 1 - cur_index_.load();
    }
    T &new_data_;
};

template <class T, class Label, int Interval>
std::array<T, 2> DualBuffer<T, Label, Interval>::data_{};

template <class T, class Label, int Interval>
std::atomic<size_t> DualBuffer<T, Label, Interval>::cur_index_{0};

template <class T, class Label, int Interval>
int DualBuffer<T, Label, Interval>::last_updated = 0;
template <class T, class Label, int Interval>
std::atomic_bool DualBuffer<T, Label, Interval>::inited(false);

template <class T, size_t Inc>
class FileFit {
public:
    template <class C>
    void reserve(std::vector<C *> update_list, const boost::iostreams::mapped_file_source &src) {
        for (auto ptr : update_list) {
            reserve(ptr, src);
        }
    }

    template <class C>
    void reserve(C *obj, const boost::iostreams::mapped_file_source &src) {
        size_t lines = 0;
        for (auto _ : base::Splitter(src.data(), src.size(), '\n')) {
            ++lines;
            _.data();
        }
        if (lines > current_size_ ) {
            size_t resize_to = lines + Inc;
            obj->reserve(resize_to);
            current_size_ = resize_to;
            return;
        }
        if (obj->size() < current_size_) {
            obj->reserve(current_size_);
        }
    }

    template <class C>
    void reserve(C* obj, size_t sz) {
        if (sz > current_size_ ) {
            size_t resize_to = sz + Inc;
            obj->reserve(resize_to);
            current_size_ = resize_to;
            return;
        }
        if (obj->size() < current_size_) {
            obj->reserve(current_size_);
        }
    }

private:
    static size_t current_size_;
};

template <class T, size_t Inc>
size_t FileFit<T, Inc>::current_size_ = 0;

} // namespace base

#endif //DEPENDENCIES_LIBS_BASE_SRC_DOUBLE_BUFFER_H_
