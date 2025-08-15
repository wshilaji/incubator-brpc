#ifndef BASE_MULTI_THREAD_LOADING
#define BASE_MULTI_THREAD_LOADING
#include <boost/iostreams/device/mapped_file.hpp>
#include "bounded_timed_blocking_queue.h"
namespace base {
template<typename ResultMap, typename KVPair>
class MultithreadLoading {
private:
    typedef std::function<void (boost::string_view&, std::vector<KVPair>*)> ParseFunction;
    typedef std::function<void (std::vector<KVPair>& infos, ResultMap* result)> MergeFunction;
    bool first_time_loading = true;
public:
    MultithreadLoading() = default;

    bool FirstTimeLoading() const {
        return first_time_loading;
    }

    void load(const boost::iostreams::mapped_file_source &src, \
                    ResultMap& result_map, ParseFunction parse_func, size_t parser_num, \
                    MergeFunction merge_func) {
        // parse_func and merge_func should be user defined
        assert(parse_func != NULL && merge_func != NULL);
        if (first_time_loading) {
            ALOG(info, fmt::format("parallel loading start======="));
            Merger merger(merge_func, parser_num, result_map);
            std::vector<std::unique_ptr<ParseThread>> parsers;
            for(size_t i = 0; i < parser_num; i++) {
                parsers.emplace_back(new ParseThread(merger, parse_func));
            }
            auto reader = std::make_shared<ReadThread>(src, parsers);
            merger.start();
            first_time_loading = false;
            ALOG(info, fmt::format("parallel loading end======="));
        } else {
            std::vector<KVPair> infos;
            infos.reserve(1);
            base::Splitter splitter(src.data(), src.size(), '\n');
            for (auto line : splitter) {
                parse_func(line, &infos);
                if(infos.empty()) {
                    continue;
                }
                merge_func(infos, &result_map);
                infos.clear();
            }
        }
    }
private:
    class ParseThread;
    class Merger;

    class ReadThread {
        public:
            ReadThread(const boost::iostreams::mapped_file_source &src,
                        std::vector<std::unique_ptr<ParseThread>>& parsers);
            ~ReadThread() { thread_.join(); }
            ReadThread(const ReadThread&) = delete;
            ReadThread& operator=(const ReadThread&) = delete;
        private:
            const boost::iostreams::mapped_file_source& src_;
            std::vector<std::unique_ptr<ParseThread>>& parsers_;
            std::thread thread_;
    };

    class ParseThread {
        public:
            ParseThread(Merger& merger, ParseFunction parse_func);
            ~ParseThread() { thread_.join(); }
            ParseThread(const ParseThread&) = delete;
            ParseThread& operator=(const ParseThread&) = delete;
        private:
            friend class ReadThread;
            Merger& merger_;
            BoundedTimedBlockingQueue<std::vector<boost::string_view>> lines_queue_ {500};
            ParseFunction parse_func_;
            std::thread thread_;
    };

    class Merger {
        public:
            Merger(MergeFunction merge_func, size_t parser_num, ResultMap& result) :
                result_(result), parser_num_(parser_num), merge_func_(merge_func) {};
            ~Merger() {}
            Merger(const Merger&) = delete;
            Merger& operator=(const Merger&) = delete;
            void start();
        private:
            friend class ParseThread;
            ResultMap& result_;
            const size_t parser_num_;
            BoundedTimedBlockingQueue<std::vector<KVPair>> infos_queue_ {10000};
            MergeFunction merge_func_;
    };
};

template<typename ResultMap, typename KVPair>
MultithreadLoading<ResultMap, KVPair>::ReadThread::ReadThread(
                        const boost::iostreams::mapped_file_source &src,
                        std::vector<std::unique_ptr<ParseThread>>& parsers) :
                        src_(src), parsers_(parsers), thread_([this]() {
                            uint64_t total_line = 0;
                            size_t round = 0;
                            size_t idx_in_round = 0;
                            const size_t kMaxLinesPerRound = 5000;
                            std::vector<boost::string_view> lines;
                            lines.resize(kMaxLinesPerRound);
                            base::Splitter splitter(src_.data(), src_.size(), '\n');
                            for (auto line : splitter) {
                                lines[idx_in_round++] = line;
                                if (idx_in_round == kMaxLinesPerRound) {
                                    std::vector<boost::string_view> new_lines;
                                    lines.swap(new_lines);
                                    parsers_[round++ % parsers_.size()]->lines_queue_.Enqueue(std::move(new_lines));
                                    lines.resize(kMaxLinesPerRound);
                                    idx_in_round = 0;
                                    if (round * kMaxLinesPerRound % 1000000 == 0) {
                                        ALOG(info, fmt::format("parse step: {}", round * kMaxLinesPerRound));
                                    }
                                }
                            }
                            if (idx_in_round > 0) {
                                lines.resize(idx_in_round);
                                parsers_[round++ % parsers_.size()]->lines_queue_.Enqueue(std::move(lines));
                            }
                            // empty lines to indicate that it reaches the end
                            for(std::unique_ptr<ParseThread>& parser : parsers_) {
                                parser->lines_queue_.Enqueue(std::move(std::vector<boost::string_view>()));
                            }
                        }) {}

template <typename ResultMap, typename KVPair>
MultithreadLoading<ResultMap, KVPair>::ParseThread::ParseThread(Merger& merger, ParseFunction parse_func) :
    merger_(merger), parse_func_(std::move(parse_func)), thread_([this]() {
        while (true) {
            std::vector<boost::string_view> lines;
            lines_queue_.Dequeue(&lines);
            if (lines.empty()) {
                // received empty string, reaches end, no more data
                break;
            }
            std::vector<KVPair> infos;
            infos.reserve(lines.size());
            for (boost::string_view& line : lines) {
                parse_func_(line, &infos);
            }
            if (!infos.empty()) {
                merger_.infos_queue_.Enqueue(std::move(infos));
            }
        }
        // put an empty vector to indicate it reaches the end
        merger_.infos_queue_.Enqueue(std::move(std::vector<KVPair>()));
    }) {}

template<typename ResultMap, typename KVPair>
void MultithreadLoading<ResultMap, KVPair>::Merger::start() {

    size_t finished_parsers = 0;
    size_t lines = 0;
    while(true) {
        std::vector<KVPair> infos;
        infos_queue_.Dequeue(&infos);
        if (infos.empty() && ++finished_parsers == parser_num_) {
            break;
        }
        lines += infos.size();
        merge_func_(infos, &result_);
        if (lines % 1000000 == 0) {
            ALOG(info, fmt::format("merge step: {}", lines));
        }
    }
}
} // end of namespace
#endif
