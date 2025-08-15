#ifndef BASE_ALLOCATOR_H
#define BASE_ALLOCATOR_H

#include <atomic>
#include <deque>
#include <scoped_allocator>
#include <sys/mman.h>
#include <tbb/concurrent_queue.h>

namespace base {
constexpr size_t aligned_size(size_t size, size_t align) {
    return (align - size % align) % align + size;
}
class Page {
public:
    Page() = default;
    Page(size_t size, size_t align)
        : size_(size), offset_(0), align_(align),
          ptr_(mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) {}
    void *allocate(size_t size) noexcept {
        size = aligned_size(size, align_);
        if (size + offset_ > size_) {
            return nullptr;
        }
        auto ret = static_cast<char *>(ptr_) + offset_;
        offset_ += size;
        return ret;
    }
    void remap(void *addr) {
        mremap(ptr_, size_, size_, MREMAP_MAYMOVE | MREMAP_FIXED, addr);
        ptr_ = addr;
    }
    void clear() noexcept {
        offset_ = 0;
    }
    void deletePage() {
        munmap(ptr_, size_);
    }

private:
    size_t size_;
    size_t offset_;
    size_t align_;
    void *ptr_;
};

class ProtectedPage : public Page {
public:
    ProtectedPage(Page p)
        : Page{std::move(p)} {}

    Page unprotect() {
        return *static_cast<Page *>(this);
    }

    // not copyable
    ProtectedPage(const ProtectedPage &) = delete;
    ProtectedPage &operator=(const ProtectedPage &) = delete;
    ProtectedPage(ProtectedPage &&) = default;
    ProtectedPage &operator=(ProtectedPage &&) = default;
};

class PageAllocator {
public:
    explicit PageAllocator(size_t pageSize, size_t align)
        : align_(align), total_(0) {
        pageSize_ = aligned_size(pageSize, align);
    }
    ~PageAllocator() {
        for (auto it = pages_.unsafe_begin(); it != pages_.unsafe_end(); ++it) {
            it->deletePage();
        }
    }
    void preAllocate(size_t size) {
        auto s = size / pageSize_ + 1;
        while (s--) {
            total_.fetch_add(1, std::memory_order_relaxed);
            pages_.push({pageSize_, align_});
        }
    }
    ProtectedPage allocate() {
        Page ret;
        if (!pages_.try_pop(ret)) {
            total_.fetch_add(1, std::memory_order_relaxed);
            return Page{pageSize_, align_};
        }
        return ret;
    }
    void deallocate(ProtectedPage &&p) {
        p.clear();
        pages_.push(p.unprotect());
    }
    size_t pageSize() const {
        return pageSize_;
    }
    size_t totalSize() const {
        return total_.load(std::memory_order_relaxed);
    }
    size_t allocated() const {
        return totalSize() - pages_.unsafe_size();
    }

private:
    size_t pageSize_;
    size_t align_;
    tbb::concurrent_queue<Page> pages_;
    std::atomic<size_t> total_;
};

class ArenaRawAllocator {
public:
    explicit ArenaRawAllocator(PageAllocator *p) noexcept
        : pages_(p) {
        span_.emplace_back(pages_->allocate());
    }
    ~ArenaRawAllocator() noexcept {
        for (auto &p : span_) {
            pages_->deallocate(std::move(p));
        }
        for (auto &p : large_) {
            operator delete[](p);
        }
    }
    // not copyable or movable
    ArenaRawAllocator(const ArenaRawAllocator &) = delete;
    ArenaRawAllocator &operator=(const ArenaRawAllocator &) = delete;
    ArenaRawAllocator(ArenaRawAllocator &&) = delete;
    ArenaRawAllocator &operator=(ArenaRawAllocator &&) = delete;
    void *allocate(size_t size) noexcept {
        byteAllocated_ += size;
        if (size > pages_->pageSize()) {
            large_.emplace_back(operator new[](size));
            return large_.back();
        }
        auto ret = span_[offset_].allocate(size);
        if (ret != nullptr) {
            return ret;
        }
        ++offset_;
        if (span_.size() <= offset_) {
            span_.emplace_back(pages_->allocate());
        }
        return span_.back().allocate(size);
    }
    void deallocate(void *, size_t = 0) noexcept {} // ignore deallocate
    void clear() noexcept {
        offset_ = 0;
        byteAllocated_ = 0;
        for (auto &p : span_) {
            p.clear();
        }
        for (auto &p : large_) {
            operator delete[](p);
        }
    }
    size_t byteAllocated() const noexcept {
        return byteAllocated_;
    }

private:
    PageAllocator *pages_;
    size_t offset_ = 0;
    std::deque<ProtectedPage> span_;
    std::deque<void *> large_;
    size_t byteAllocated_ = 0;
};

template <typename T>
class ArenaAllocator {
public:
    ArenaAllocator(ArenaRawAllocator *alloc) noexcept
        : alloc_(alloc) {}
    template <typename U>
    ArenaAllocator(const ArenaAllocator<U> &x) noexcept
        : alloc_(x.alloc_) {}
    using value_type = T;
    T *allocate(std::size_t n) noexcept {
        return static_cast<T *>(alloc_->allocate(sizeof(T) * n));
    }
    void deallocate(T *t, std::size_t n) noexcept {
        return alloc_->deallocate(t, n);
    }
    ArenaRawAllocator *rawAllocator() const noexcept {
        return alloc_;
    }
    template <typename U>
    friend class ArenaAllocator;

private:
    ArenaRawAllocator *alloc_;
};
template <typename T, typename U>
bool operator==(const ArenaAllocator<T> &x, const ArenaAllocator<U> &y) noexcept {
    return x.rawAllocator() == y.rawAllocator();
}
template <typename T, typename U>
bool operator!=(const ArenaAllocator<T> &x, const ArenaAllocator<U> &y) noexcept {
    return x.rawAllocator() != y.rawAllocator();
}
} // namespace base

#endif // BASE_ALLOCATOR_H