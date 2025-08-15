#include "string_util.h"

namespace base {
Splitter::Iterator Splitter::begin() const {
    if (size == 0) {
        return end();
    }
    return {ptr, ptr + size, delm, cnt};
}
Splitter::Iterator Splitter::end() const {
    return {nullptr, ptr + size, delm, cnt};
}
void Splitter::transform(std::vector<boost::string_view> &v) const {
    v.resize(0);
    for (const auto &s : *this) {
        v.push_back(s);
    }
}
void Splitter::transform(std::unordered_set<std::string> &set, size_t reserve_size) const {
  set.clear();
  set.reserve(reserve_size);
  for (const auto &s : *this) {
    set.emplace(s.to_string());
  }
}
Splitter::Iterator &Splitter::Iterator::operator++() {
    if (nxt == ed) {
        st = nullptr;
        return *this;
    }
    if (nxt != ed) {
        ++nxt;
    }
    st = nxt;
    while (nxt != ed && (*nxt != delm || cnt == 0)) {
        ++nxt;
    }
    --cnt;
    return *this;
}
boost::string_view Splitter::Iterator::operator*() {
    return boost::string_view(st, nxt - st);
}
bool Splitter::Iterator::operator==(const Iterator &x) const {
    return st == x.st;
}
bool Splitter::Iterator::operator!=(const Iterator &x) const {
    return st != x.st;
}
Splitter::Iterator::Iterator(const char *st, const char *ed, char delm, size_t c)
    : st(st), nxt(st), ed(ed), delm(delm), cnt(c) {
    while (nxt != nullptr && (nxt != ed && (*nxt != delm || cnt == 0))) {
        ++nxt;
    }
    --cnt;
}
} // namespace base