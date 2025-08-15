#include "feature.h"

namespace base {

std::vector<boost::string_view> getTerms(const std::string& term_str, char sep) {
    std::vector<boost::string_view> terms;
    terms.reserve(std::count(term_str.begin(), term_str.end(), sep) + 1);
    for (auto line : base::Splitter(term_str.data(), term_str.size(), sep)) {
        if (line.empty() || line.compare(" ") == 0) {
            continue;
        }
        terms.push_back(line);
    }
    return terms;
}

} // namespace base