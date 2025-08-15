#include "result.h"

namespace base {
std::string toString(const std::exception_ptr &e) {
    std::string ret = "exception: ";
    try {
        if (e) {
            std::rethrow_exception(e);
        }
        ret += "null exception_ptr";
    } catch (const std::exception &e) {
        ret += e.what();
    } catch (const std::string &e) {
        ret += e;
    } catch (const char *e) {
        ret += e;
    } catch (...) {
        ret += "unknown";
    }
    return ret;
}
} // namespace base