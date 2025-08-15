//
// Created by wciq1208 on 19-9-2.
//

#ifndef DEPENDENCIES_LIBS_BASE_SRC_HASH_MAP_H_
#define DEPENDENCIES_LIBS_BASE_SRC_HASH_MAP_H_

#include <boost/optional.hpp>
#include <google/dense_hash_map>
#include <google/dense_hash_set>
#include <vector>

namespace base {

template <class K, class V, class... Args>
class EasyHashMap : public google::dense_hash_map<K, V, Args...> {
    using MapType = google::dense_hash_map<K, V, Args...>;
public:
    EasyHashMap() {
        MapType::set_empty_key(empty_key_);
    }
    void safe_insert(K key, const V &val) {
        if (is_empty_key(key)) {
            return;
        }
        MapType::insert(std::make_pair(key, val));
    }
    void safe_insert(K key, V &&val) {
        safe_insert(key, val);
    }
    bool is_empty_key(const K &key) const {
        return key == empty_key_;
    }
    V get_or(K key, V val) const {
        auto it = MapType::find(key);
        if (it == MapType::end()) {
            return val;
        }
        return it->second;
    }
private:
    K empty_key_;
};

template <class K, class... Args>
class EasyHashSet : public google::dense_hash_set<K, Args...> {
    using SetType = google::dense_hash_set<K, Args...>;
public:
    EasyHashSet() {
        SetType::set_empty_key(empty_key_);
    }
    void safe_insert(K key) {
        if (is_empty_key(key)) {
            return;
        }
        SetType::insert(key);
    }
    bool is_empty_key(const K &key) const {
        return key == empty_key_;
    }
    bool exists(K key) const {
        return SetType::count(key) != 0;
    }
private:
    K empty_key_;
};

template <class K, class V, class... Args>
struct Iterator {
private:
    using M = google::dense_hash_map<K, size_t, Args...>;
public:
    Iterator(typename M::iterator mit, std::vector<V> *data) : mit_(mit), data_(data) {}
    Iterator &operator++() {
        ++mit_;
        return *this;
    }
    Iterator operator++(int) {
        Iterator tmp = *(this);
        ++mit_;
        return tmp;
    }
    bool operator==(const Iterator& other) {
        return other.mit_ == mit_;
    }
    bool operator!=(const Iterator& other) const {
        return other.mit_ != mit_;
    }
    std::pair<K, V &> operator*() const {
        return {mit_->first, (*data_)[mit_->second]};
    }
private:
    typename M::iterator mit_;
    std::vector<V> *data_;
    boost::optional<std::pair<K, V&>> cache_;
};

template <class K, class V,class... Args>
struct ConstIterator {
private:
    using M = google::dense_hash_map<K, size_t, Args...>;
public:
    ConstIterator(typename M::const_iterator mit, const std::vector<V> *data) : mit_(mit), data_(data) {}
    ConstIterator &operator++() {
        ++mit_;
        return *this;
    }
    ConstIterator operator++(int) {
        ConstIterator tmp = *(this);
        ++mit_;
        return tmp;
    }
    bool operator==(const ConstIterator& other) const {
        return other.mit_ == mit_;
    }
    bool operator!=(const ConstIterator& other) const {
        return other.mit_ != mit_;
    }
    const std::pair<K, const V &> operator*() const {
        return {mit_->first, (*data_)[mit_->second]};
    }
private:
    typename M::const_iterator mit_;
    const std::vector<V> *data_;
};

template <class K, class V, class... Args>
class LittleHashMap {
    using MapType = google::dense_hash_map<K, size_t, Args...>;
    using iterator = Iterator<K, V, Args...>;
    using const_iterator = ConstIterator<K, V, Args...>;
public:

    LittleHashMap() {
        map_.set_empty_key(empty_key_);
    }
    LittleHashMap(K empty_key) : empty_key_(empty_key) {
        map_.set_empty_key(empty_key_);
    }
    void reserve(size_t sz) {
        data_.reserve(sz);
        map_.resize(size_t(sz*1.5));
    }
    size_t count(const K &key) const {
        return map_.count(key);
    }
    bool exists(const K &key) const {
        return map_.count(key) != 0;
    }
    V &operator[] (K key) {
        auto it = map_.find(key);
        if (it == map_.end()) {
            data_.emplace_back();
            map_[key] = data_.size() - 1;
        } else {
            return data_[it->second];
        }
        return data_.back();
    }
    void insert(K key, const V &val) {
        if (key == empty_key_) {
            return;
        }
        data_.emplace_back(val);
        map_[key] = data_.size() - 1;
    }
    void insert(K key, V &&val) {
        insert(key, val);
    }
    void insert(const typename MapType::value_type &obj) {
        if (obj.first == empty_key_) {
            return;
        }
        data_.emplace_back(obj.second);
        map_[obj.first] = data_.size() - 1;
    }
    void insert(typename MapType::value_type &&obj) {
        insert(obj);
    }
    boost::optional<const V &> at(const K &key) const {
        auto it = map_.find(key);
        return (it != map_.end()) ? boost::optional<const V &>(data_[it->second]) : boost::none;
    }
    boost::optional<V &> at(const K &key) {
        auto it = map_.find(key);
        return (it != map_.end()) ? boost::optional<V &>(data_[it->second]) : boost::none;
    }
    iterator find(const K &key) {
        return iterator(map_.find(key), &data_);
    }
    const_iterator find(const K &key) const {
        return const_iterator(map_.find(key), &data_);
    }
    void clear() {
        data_.clear();
        map_.clear_no_resize();
    }
    iterator begin() {
        return iterator(map_.begin(), &data_);
    }
    iterator end() {
        return iterator(map_.end(), &data_);
    }
    const_iterator begin() const {
        return const_iterator(map_.begin(), &data_);
    }
    const_iterator end() const {
        return const_iterator(map_.end(), &data_);
    }
    size_t bucket_count() const {
        return map_.bucket_count();
    }
    size_t size() const {
        return data_.size();
    }
    size_t memory() const {
        return sizeof(V) * data_.capacity() + (sizeof(K) + sizeof(size_t)) * map_.bucket_count() + sizeof(K);
    }
    bool is_empty_key(const K &key) const {
        return key == empty_key_;
    }

private:
    MapType map_;
    K empty_key_;
    std::vector<V> data_;
};
} // namespace base

#endif //DEPENDENCIES_LIBS_BASE_SRC_HASH_MAP_H_
