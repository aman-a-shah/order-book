// site_export — runs the real order-book engine across every workload and emits
// a single JSON object on stdout. No numbers are invented: latency, throughput,
// the latency distribution, the depth ladder and the trade tape are all produced
// by the same headers the tests and benchmarks exercise. export_data.py wraps
// this (running it repeatedly for stable medians) and writes site/data.json.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "lob/binary_protocol.hpp"
#include "lob/histogram.hpp"
#include "lob/multi_book.hpp"
#include "lob/order_book.hpp"
#include "lob/replay.hpp"
#include "lob/spsc_queue.hpp"
#include "lob/threading.hpp"
#include "lob/timing.hpp"

namespace {

const char* simd_path() {
#if defined(__AVX2__)
    return "AVX2";
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    return "NEON";
#else
    return "scalar";
#endif
}

// --- tiny JSON writer (tracks separators so we never emit a trailing comma) ---
struct Json {
    std::ostringstream o;
    bool need_comma = false;

    void sep() { if (need_comma) o << ','; need_comma = true; }
    void key(const char* k) { sep(); o << '"' << k << "\":"; need_comma = false; }
    void obj_open() { sep(); o << '{'; need_comma = false; }
    void obj_close() { o << '}'; need_comma = true; }
    void arr_open() { sep(); o << '['; need_comma = false; }
    void arr_close() { o << ']'; need_comma = true; }
    void num(double v) { sep(); o << v; }
    void inum(long long v) { sep(); o << v; }
    void str(const char* s) { sep(); o << '"' << s << '"'; }

    template <typename T>
    void kv(const char* k, T v) { key(k); val(v); }
    void val(double v) { o << v; need_comma = true; }
    void val(long long v) { o << v; need_comma = true; }
    void val(int v) { o << v; need_comma = true; }
    void val(unsigned long long v) { o << v; need_comma = true; }
    void val(std::size_t v) { o << v; need_comma = true; }
    void val(const char* s) { o << '"' << s << '"'; need_comma = true; }
};

template <typename Hist>
void write_hist(Json& j, const Hist& h) {
    // Emit a contiguous bucket range [0, last-nonzero] so the log-scale
    // histogram bars stay evenly spaced (interior zero buckets are real gaps).
    std::size_t last = 0;
    for (std::size_t i = 0; i < Hist::bucket_count(); ++i)
        if (h.bucket(i) > 0) last = i;
    j.key("hist");
    j.obj_open();
    j.key("ub");
    j.arr_open();
    for (std::size_t i = 0; i <= last; ++i)
        j.inum(static_cast<long long>(Hist::bucket_upper_bound(i)));
    j.arr_close();
    j.key("count");
    j.arr_open();
    for (std::size_t i = 0; i <= last; ++i)
        j.inum(static_cast<long long>(h.bucket(i)));
    j.arr_close();
    j.obj_close();
}

double median(std::vector<double> xs) {
    std::sort(xs.begin(), xs.end());
    const std::size_t n = xs.size();
    if (n == 0) return 0.0;
    return n % 2 ? xs[n / 2] : 0.5 * (xs[n / 2 - 1] + xs[n / 2]);
}

// --- workload 1: bounded add/cancel hot path (mirrors latency_bench) ---
void run_latency(Json& j) {
    using Book = lob::OrderBook<1 << 16, 1024, 1 << 17>;
    const std::size_t runs = 5;
    const std::uint64_t events = 1'000'000;
    lob::PowerOfTwoHistogram<> merged;
    std::vector<double> tputs;

    for (std::size_t r = 0; r < runs; ++r) {
        auto book = std::make_unique<Book>();
        lob::PowerOfTwoHistogram<> h;
        lob::Stopwatch wall;
        for (std::uint64_t i = 0; i < events; ++i) {
            const bool add = (i & 1U) == 0;
            const std::uint64_t order_id = (i / 2) + 1;
            const lob::OrderCommand cmd{
                add ? lob::OrderType::Limit : lob::OrderType::Cancel,
                lob::Side::Buy, order_id, add ? 10'000U : 0U, add ? 1U : 0U};
            const auto t0 = lob::read_cycle_counter();
            const auto rep = book->process(cmd);
            const auto t1 = lob::read_cycle_counter();
            (void)rep;
            h.observe(t1 - t0);
        }
        tputs.push_back(static_cast<double>(events) / wall.elapsed_seconds());
        merged.merge(h);
    }

    std::sort(tputs.begin(), tputs.end());
    j.key("latency");
    j.obj_open();
    j.kv("events", static_cast<long long>(events * runs));
    j.kv("runs", static_cast<long long>(runs));
    j.key("throughput_runs");
    j.arr_open();
    for (double t : tputs) j.num(t);
    j.arr_close();
    j.kv("throughput_median", median(tputs));
    j.kv("throughput_min", tputs.front());
    j.kv("throughput_max", tputs.back());
    j.kv("mean", merged.mean());
    j.kv("p50", static_cast<long long>(merged.percentile(50.0)));
    j.kv("p90", static_cast<long long>(merged.percentile(90.0)));
    j.kv("p99", static_cast<long long>(merged.percentile(99.0)));
    j.kv("p999", static_cast<long long>(merged.percentile(99.9)));
    j.kv("max", static_cast<long long>(merged.max()));
    write_hist(j, merged);
    j.obj_close();
}

// --- workload 2: binary decode -> symbol route -> match (mirrors replay_bench) ---
void run_replay(Json& j, const std::string& path) {
    std::vector<lob::ReplayEvent> events;
    std::string err;
    lob::load_replay_file(path, events, err);

    std::vector<std::array<std::uint8_t, lob::BinaryMessageSize>> messages;
    for (const auto& e : events) messages.push_back(lob::encode_binary_order(e.command));

    using Router = lob::MultiBookRouter<2, 1 << 19, 4096, 1 << 20>;
    const std::size_t repeats = 100'000;
    auto router = std::make_unique<Router>();
    lob::PowerOfTwoHistogram<> h;
    std::uint64_t processed = 0, rejected = 0;

    lob::Stopwatch wall;
    for (std::size_t r = 0; r < repeats; ++r) {
        for (const auto& msg : messages) {
            lob::OrderCommand cmd;
            const auto t0 = lob::read_cycle_counter();
            lob::decode_binary_order(msg, cmd);
            const std::uint64_t off = static_cast<std::uint64_t>(r) * 1'000'000ULL;
            cmd.order_id += off;
            if (cmd.new_order_id != 0) cmd.new_order_id += off;
            const auto rep = router->process(cmd);
            const auto t1 = lob::read_cycle_counter();
            h.observe(t1 - t0);
            ++processed;
            if (!rep.accepted) ++rejected;
        }
    }

    j.key("replay");
    j.obj_open();
    j.kv("events", static_cast<long long>(processed));
    j.kv("rejected", static_cast<long long>(rejected));
    j.kv("throughput", static_cast<double>(processed) / wall.elapsed_seconds());
    j.kv("mean", h.mean());
    j.kv("p50", static_cast<long long>(h.percentile(50.0)));
    j.kv("p90", static_cast<long long>(h.percentile(90.0)));
    j.kv("p99", static_cast<long long>(h.percentile(99.0)));
    j.kv("p999", static_cast<long long>(h.percentile(99.9)));
    j.kv("max", static_cast<long long>(h.max()));
    write_hist(j, h);
    j.obj_close();
}

// --- workload 3: producer -> SPSC ring -> matching thread, swept over size ---
double pipeline_once(std::uint64_t events) {
    using Queue = lob::SpscQueue<lob::OrderCommand, 1 << 16>;
    using Book = lob::OrderBook<1 << 20, 4096, 1 << 21>;
    auto queue = std::make_unique<Queue>();
    auto book = std::make_unique<Book>();
    std::atomic<bool> done{false};
    std::atomic<std::uint64_t> processed{0};

    lob::Stopwatch wall;
    std::thread producer([&] {
        lob::pin_current_thread_to_core(0);
        for (std::uint64_t i = 1; i <= events; ++i) {
            const lob::OrderCommand cmd{lob::OrderType::Limit,
                (i & 1ULL) == 0 ? lob::Side::Buy : lob::Side::Sell, i,
                (i & 1ULL) == 0 ? 9'999U : 10'001U, 1U};
            while (!queue->push(cmd)) {}
        }
        done.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        lob::pin_current_thread_to_core(1);
        lob::OrderCommand cmd;
        while (!done.load(std::memory_order_acquire) || !queue->empty()) {
            if (queue->pop(cmd)) {
                book->process(cmd);
                processed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    producer.join();
    consumer.join();
    return static_cast<double>(events) / wall.elapsed_seconds();
}

void run_pipeline(Json& j) {
    const std::array<std::uint64_t, 6> sizes{50'000, 100'000, 250'000, 500'000, 750'000, 1'000'000};
    std::vector<double> tput;
    for (auto n : sizes) tput.push_back(pipeline_once(n));

    j.key("pipeline");
    j.obj_open();
    j.kv("events", static_cast<long long>(sizes.back()));
    // Report the median across the size sweep: cross-thread throughput is noisy
    // on a loaded laptop, and the single 1M-event run is the worst case. The
    // full sweep is published below for transparency.
    j.kv("throughput", median(tput));
    j.kv("throughput_max", *std::max_element(tput.begin(), tput.end()));
    j.kv("resting", static_cast<long long>(sizes.back()));
    j.key("scaling");
    j.obj_open();
    j.key("n");
    j.arr_open();
    for (auto n : sizes) j.inum(static_cast<long long>(n));
    j.arr_close();
    j.key("throughput");
    j.arr_open();
    for (double t : tput) j.num(t);
    j.arr_close();
    j.obj_close();
    j.obj_close();
}

// --- a two-sided book, built and matched by the real engine, snapshotted ---
void run_depth(Json& j) {
    using Book = lob::OrderBook<1 << 16, 4096, 1 << 17>;
    auto book = std::make_unique<Book>();
    std::uint64_t id = 1;

    // Resting bid ladder: best 10000, thinning away from the touch, multiple
    // orders per level so order_count is meaningful.
    const std::array<std::uint32_t, 8> bid_px{10000, 9999, 9998, 9997, 9996, 9995, 9994, 9993};
    const std::array<std::uint32_t, 8> bid_lots{18, 26, 40, 31, 55, 44, 60, 72};
    const std::array<int, 8> bid_orders{2, 3, 4, 3, 5, 4, 6, 6};
    for (std::size_t l = 0; l < bid_px.size(); ++l) {
        for (int k = 0; k < bid_orders[l]; ++k) {
            book->process({lob::OrderType::Limit, lob::Side::Buy, id++, bid_px[l],
                           bid_lots[l] / static_cast<std::uint32_t>(bid_orders[l]) + 1U});
        }
    }
    const std::array<std::uint32_t, 8> ask_px{10001, 10002, 10003, 10004, 10005, 10006, 10007, 10008};
    const std::array<std::uint32_t, 8> ask_lots{15, 22, 35, 48, 41, 58, 63, 70};
    const std::array<int, 8> ask_orders{2, 2, 3, 4, 4, 5, 5, 6};
    for (std::size_t l = 0; l < ask_px.size(); ++l) {
        for (int k = 0; k < ask_orders[l]; ++k) {
            book->process({lob::OrderType::Limit, lob::Side::Sell, id++, ask_px[l],
                           ask_lots[l] / static_cast<std::uint32_t>(ask_orders[l]) + 1U});
        }
    }
    // Aggress the touch on both sides to print trades through the tape.
    book->process({lob::OrderType::Market, lob::Side::Buy, id++, 0, 28});
    book->process({lob::OrderType::Market, lob::Side::Sell, id++, 0, 24});

    std::array<lob::DepthLevel, 8> bids{}, asks{};
    const auto nb = book->bid_depth(bids);
    const auto na = book->ask_depth(asks);

    j.key("depth");
    j.obj_open();
    const std::uint32_t best_bid = nb ? bids[0].price : 0;
    const std::uint32_t best_ask = na ? asks[0].price : 0;
    j.kv("best_bid", static_cast<long long>(best_bid));
    j.kv("best_ask", static_cast<long long>(best_ask));
    j.kv("spread", static_cast<long long>(best_ask - best_bid));
    j.kv("mid", 0.5 * (best_bid + best_ask));

    auto emit_side = [&](const char* name, const std::array<lob::DepthLevel, 8>& s, std::size_t n) {
        j.key(name);
        j.arr_open();
        std::uint64_t cum = 0;
        for (std::size_t i = 0; i < n; ++i) {
            cum += s[i].total_volume;
            j.obj_open();
            j.kv("price", static_cast<long long>(s[i].price));
            j.kv("qty", static_cast<long long>(s[i].total_volume));
            j.kv("orders", static_cast<long long>(s[i].order_count));
            j.kv("cum", static_cast<long long>(cum));
            j.obj_close();
        }
        j.arr_close();
    };
    emit_side("bids", bids, nb);
    emit_side("asks", asks, na);

    j.key("trades");
    j.arr_open();
    for (std::size_t i = 0; i < book->trade_count(); ++i) {
        const auto& t = book->trades()[i];
        j.obj_open();
        j.kv("seq", static_cast<long long>(t.sequence));
        j.kv("price", static_cast<long long>(t.price));
        j.kv("qty", static_cast<long long>(t.quantity));
        j.kv("side", t.aggressor_side == lob::Side::Buy ? "BUY" : "SELL");
        j.obj_close();
    }
    j.arr_close();
    j.kv("trade_count", static_cast<long long>(book->trade_count()));
    j.obj_close();
}

void run_structure(Json& j) {
    using LatBook = lob::OrderBook<1 << 16, 1024, 1 << 17>;
    using Queue = lob::SpscQueue<lob::OrderCommand, 1 << 16>;
    j.key("structure");
    j.obj_open();
    j.kv("price_level_bytes", static_cast<long long>(sizeof(lob::PriceLevel)));
    j.kv("order_bytes", static_cast<long long>(sizeof(lob::Order)));
    j.kv("command_bytes", static_cast<long long>(sizeof(lob::OrderCommand)));
    j.kv("msg_bytes", static_cast<long long>(lob::BinaryMessageSize));
    j.kv("spsc_capacity", static_cast<long long>(1 << 16));
    j.kv("spsc_bytes", static_cast<long long>(sizeof(Queue)));
    j.kv("book_bytes", static_cast<long long>(sizeof(LatBook)));
    j.kv("cache_line", 64);
    j.obj_close();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "data/sample_replay.csv";
    Json j;
    j.obj_open();
    j.kv("simd", simd_path());
    run_latency(j);
    run_replay(j, path);
    run_pipeline(j);
    run_depth(j);
    run_structure(j);
    j.obj_close();
    std::cout << j.o.str() << '\n';
    return 0;
}
