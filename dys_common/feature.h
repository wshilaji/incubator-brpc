#ifndef BASE_FEATURE_H
#define BASE_FEATURE_H

#include "string_util.h"
#include "map_util.h"
#include <algorithm>
#include <iterator>
#include <string>
#include <vector>
#include <utility>
#include <type_traits>
#include <boost/utility/string_view.hpp>
#include <cinttypes>
#include <string.h>

namespace base {

std::vector<boost::string_view> getTerms(const std::string& term_str, char sep = ' ');

template <typename InputIter, typename OutputIter>
void crossMatch(InputIter first1, InputIter last1,
                InputIter first2, InputIter last2,
                OutputIter inter,
                OutputIter diff_1,
                OutputIter diff_2) {
    while (first1 != last1 || first2 != last2) {
        if (first1 == last1) {
            std::copy(first2, last2, diff_2);
            return;
        } else if (first2 == last2) {
            std::copy(first1, last1, diff_1);
            return;
        } else {
            if (*first1 < *first2) {
                *diff_1++ = *first1++;
            } else if (*first1 > *first2) {
                *diff_2++ = *first2++;
            } else {
                *inter++ = *first1;
                first1++;
                first2++;
            }
        }
    }
}

template <typename T>
void crossMatch(std::vector<T> &from, 
                std::vector<T> &to, 
                std::vector<T> &inter, 
                std::vector<T> &diffFrom, 
                std::vector<T> &diffTo) {
    size_t n = from.size() + to.size();
    inter.reserve(n);
    diffFrom.reserve(n);
    diffTo.reserve(n);
    crossMatch(from.begin(), from.end(), to.begin(), to.end(), 
               std::back_inserter(inter), std::back_inserter(diffFrom), std::back_inserter(diffTo));
}

namespace impl {

// int    
template <typename T, typename V, std::enable_if_t<std::is_same<
                                    std::remove_cv_t<std::remove_reference_t<V>>, 
                                    int>::value, int> = 0> int appendV (T&& fs, V&& value) {
    return fs.appendf("%d", value);
}

//uint64
template <typename T, typename V, std::enable_if_t<std::is_same<
                                    std::remove_cv_t<std::remove_reference_t<V>>, 
                                    uint64_t>::value, int> = 0> int appendV (T&& fs, V&& value) {
    return fs.appendf("%" PRIu64, value);
}

//uint32
template <typename T, typename V, std::enable_if_t<std::is_same<
                                    std::remove_cv_t<std::remove_reference_t<V>>, 
                                    uint32_t>::value, int> = 0> int appendV (T&& fs, V&& value) {
    return fs.appendf("%" PRIu32, value);
}

// string
template <typename T, typename V, std::enable_if_t<std::is_same<
                                    std::remove_cv_t<std::remove_reference_t<V>>, 
                                    std::string>::value, int> = 0> int appendV (T&& fs, V&& value) {
    return fs.appendString(value.data(), value.size()); 
}

// boost::string_view
template <typename T, typename V, std::enable_if_t<std::is_same<
                                    std::remove_cv_t<std::remove_reference_t<V>>, 
                                    boost::string_view>::value, int> = 0> int appendV (T&& fs, V&& value) {
    return fs.appendString(value.data(), value.size());
}

// const char*
template <typename T, typename V, std::enable_if_t<std::is_same<
                                    typename std::decay<V>::type, const char*>::value, 
                                    int> = 0> int appendV (T&& fs, V&& value) {
    return fs.appendString(value, strlen(value));
}
} // namespace impl

template <typename T, typename K, typename V>
int appendKV(T&& fs, K&& key, V&& value, const std::string& key_prefix = "", const std::string& value_prefix = "") {
    int ret = 0;
    ret += impl::appendV(fs, key_prefix);
    ret += impl::appendV(fs, key);
    ret += impl::appendV(fs, " ");
    ret += impl::appendV(fs, value_prefix);
    ret += impl::appendV(fs, value);
    return ret;
}

template <typename T, typename K, typename V>
int appendKV(T&& fs, K&& key, boost::optional<V>& value, const std::string& key_prefix = "", const std::string& value_prefix = "") {
    if (value != boost::none) {
        return appendKV(fs, key, value.get(), key_prefix, value_prefix);
    }
    return 0;
}

template <typename T, typename K, typename V>
int appendKV(T&& fs, K&& key, boost::optional<V>&& value, const std::string& key_prefix = "", const std::string& value_prefix = "") {
    if (value != boost::none) {
        return appendKV(fs, key, value.get(), key_prefix, value_prefix);
    }
    return 0;
}

template <typename T, typename F, typename K, typename V>
int appendKRepeatV(T&& fs, F&& op, K&& key, V&& value, const std::string& key_prefix = "", 
                   const std::string& value_prefix = "", const std::string& sep = " ") {
    int ret = 0;
    ret += impl::appendV(fs, key_prefix);
    ret += impl::appendV(fs, key);
    bool first = true;
    for (auto& v : value) {
        if (!op(v)) {
            continue;
        }
        ret += impl::appendV(fs, sep);
        if (!first) {
            ret += impl::appendV(fs, v);
        } else {
            ret += impl::appendV(fs, value_prefix);
            ret += impl::appendV(fs, v);
            first = false;
        }
    }
    return ret;
}

} // namespace base

#endif 