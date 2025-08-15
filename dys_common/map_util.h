#ifndef BASE_MAP_UTIL_H
#define BASE_MAP_UTIL_H

#include <boost/optional.hpp>
#include <nlohmann/json.hpp>

namespace base {
/**
 * Given a map and a key, return the value corresponding to the key in the map,
 * or a given default value if the key doesn't exist in the map.
 */
template <typename Map, typename Key = typename Map::key_type>
typename Map::mapped_type get_default(const Map &map, const Key &key) {
    auto it = map.find(key);
    if (it == std::end(map)) {
        return typename Map::mapped_type{};
    }
    return it->second;
}
template <
    class Map,
    typename Key = typename Map::key_type,
    typename Value = typename Map::mapped_type>
typename Map::mapped_type get_default(const Map &map, const Key &key, Value &&dflt) {
    auto it = map.find(key);
    if (it == std::end(map)) {
        return typename Map::mapped_type(std::forward<Value>(dflt));
    }
    return it->second;
}
template <typename Value>
Value get_default(nlohmann::json map, const std::string &key, Value &&dflt) {
    auto it = map.find(key);
    if (it == std::end(map)) {
        return std::forward<Value>(dflt);
    }
    return it.value().get<Value>();
}

/**
 * Given a map and a key, return a boost::optional<V> if the key exists and None if the
 * key does not exist in the map.
 */
template <class Map, typename Key = typename Map::key_type>
boost::optional<typename Map::mapped_type> get_optional(const Map &map, const Key &key) {
    auto it = map.find(key);
    if (it == std::end(map)) {
        return boost::none;
    }
    return it->second;
}

/**
 * Given a map and a key, return a reference to the value corresponding to the
 * key in the map, or the given default reference if the key doesn't exist in
 * the map.
 */
template <class Map, typename Key = typename Map::key_type>
const typename Map::mapped_type &get_ref_default(
    const Map &map,
    const Key &key,
    const typename Map::mapped_type &dflt) {
    auto it = map.find(key);
    if (it == std::end(map)) {
        return dflt;
    }
    return it->second;
}

/**
 * Passing a temporary default value returns a dangling reference when it is
 * returned. Lifetime extension is broken by the indirection.
 * The caller must ensure that the default value outlives the reference returned
 * by get_ref_default().
 */
template <class Map, typename Key = typename Map::key_type>
const typename Map::mapped_type &get_ref_default(
    const Map &map,
    const Key &key,
    typename Map::mapped_type &&dflt) = delete;

template <class Map, typename Key = typename Map::key_type>
const typename Map::mapped_type &get_ref_default(
    const Map &map,
    const Key &key,
    const typename Map::mapped_type &&dflt) = delete;

/**
 * Given a map and a key, return a boost::optional<const V &> if the key exists 
 * and None if the key does not exist in the map.
 */
template <class Map, typename Key = typename Map::key_type>
boost::optional<const typename Map::mapped_type &>
get_ref_optional(const Map &map, const Key &key) {
    auto it = map.find(key);
    if (it == std::end(map)) {
        return boost::none;
    }
    return it->second;
}

/**
 * Given a map and a key, return a pointer to the value corresponding to the
 * key in the map, or nullptr if the key doesn't exist in the map.
 */
template <class Map, typename Key = typename Map::key_type>
const typename Map::mapped_type *get_ptr(const Map &map, const Key &key) {
    auto it = map.find(key);
    if (it == std::end(map)) {
        return nullptr;
    }
    return &it->second;
}

/**
 * Non-const overload of the above.
 */
template <class Map, typename Key = typename Map::key_type>
typename Map::mapped_type *get_ptr(Map &map, const Key &key) {
    auto it = map.find(key);
    if (it == std::end(map)) {
        return nullptr;
    }
    return &it->second;
}
} // namespace base

#endif // BASE_MAP_UTIL_H