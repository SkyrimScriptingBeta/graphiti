/*
 * CONCURRENT ACCESS STRESS TEST
 *
 * We added a mutex to Graphiti::Impl to serialize all public method calls.
 * This test hammers it from multiple threads to verify:
 *   - No crashes
 *   - No data corruption
 *   - Mutex actually works
 *
 * Requires OPENAI_API_KEY.
 */

#include "shared.h"

#include <atomic>
#include <chrono>
#include <format>
#include <thread>
#include <vector>

using namespace graphiti;

int main() {
    auto config = stress::make_config();

    auto now = std::chrono::system_clock::now();

    // ====================================================================
    stress::separator("CONCURRENT READS (search)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Seed some data first
        auto r = g.add_episode("seed", "Alice works at Acme Corp. Bob is the CTO.",
            "test", now, EpisodeType::message, "g");
        stress::test("seed data succeeds", r.has_value());
        g.build_indices();

        // Launch 8 threads all searching concurrently
        constexpr int NUM_THREADS = 8;
        constexpr int SEARCHES_PER_THREAD = 5;
        std::atomic<int> success_count{0};
        std::atomic<int> error_count{0};

        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < SEARCHES_PER_THREAD; ++i) {
                    auto result = g.search(
                        (i % 2 == 0) ? "Alice" : "Bob", "g");
                    if (result.has_value()) {
                        success_count.fetch_add(1);
                    } else {
                        error_count.fetch_add(1);
                    }
                }
            });
        }

        for (auto& t : threads) t.join();

        int total_expected = NUM_THREADS * SEARCHES_PER_THREAD;
        std::cout << std::format("    -> {}/{} succeeded, {} errors\n",
            success_count.load(), total_expected, error_count.load());
        stress::test(
            std::format("concurrent reads: {}/{} succeeded", success_count.load(), total_expected),
            success_count.load() == total_expected);
    }

    // ====================================================================
    stress::separator("CONCURRENT WRITES (add_episode)");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        constexpr int NUM_THREADS = 4;
        std::atomic<int> success_count{0};
        std::atomic<int> error_count{0};

        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&, t]() {
                auto result = g.add_episode(
                    std::format("thread-{}", t),
                    std::format("Person_{} works at Company_{} in City_{}.", t, t, t),
                    "concurrent test",
                    now + std::chrono::seconds(t),
                    EpisodeType::message,
                    "concurrent_group",
                    std::format("agent-{}", t));

                if (result.has_value()) {
                    success_count.fetch_add(1);
                } else {
                    error_count.fetch_add(1);
                    std::cerr << std::format("    Thread {} error: {}\n",
                        t, result.error().message);
                }
            });
        }

        for (auto& t : threads) t.join();

        std::cout << std::format("    -> {}/{} succeeded, {} errors\n",
            success_count.load(), NUM_THREADS, error_count.load());
        stress::test(
            std::format("concurrent writes: {}/{} succeeded",
                success_count.load(), NUM_THREADS),
            success_count.load() == NUM_THREADS);

        // Verify all data is there
        auto search = g.search("Person", "concurrent_group", 20);
        if (search.has_value()) {
            std::cout << std::format("    -> search found {} edges after concurrent writes\n",
                search.value().size());
            stress::test("data persisted after concurrent writes",
                !search.value().empty());
        }
    }

    // ====================================================================
    stress::separator("MIXED READ/WRITE CONCURRENT ACCESS");
    // ====================================================================
    {
        Graphiti g(stress::make_config());
        g.build_indices();

        // Seed data
        g.add_episode("seed", "Alice is an engineer.", "test", now,
            EpisodeType::message, "mixed");
        g.build_indices();

        std::atomic<int> read_ok{0};
        std::atomic<int> write_ok{0};
        std::atomic<int> errors{0};

        std::vector<std::thread> threads;

        // 4 reader threads
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < 3; ++i) {
                    auto r = g.search("Alice", "mixed");
                    if (r.has_value()) read_ok.fetch_add(1);
                    else errors.fetch_add(1);
                }
            });
        }

        // 2 writer threads
        for (int t = 0; t < 2; ++t) {
            threads.emplace_back([&, t]() {
                auto r = g.add_episode(
                    std::format("mixed-{}", t),
                    std::format("Bob_{} works at TechCo.", t),
                    "test",
                    now + std::chrono::seconds(t + 10),
                    EpisodeType::message, "mixed");
                if (r.has_value()) write_ok.fetch_add(1);
                else errors.fetch_add(1);
            });
        }

        for (auto& t : threads) t.join();

        std::cout << std::format("    -> reads ok: {}, writes ok: {}, errors: {}\n",
            read_ok.load(), write_ok.load(), errors.load());
        stress::test("mixed concurrent access: no crashes", true);
        stress::test("mixed concurrent reads all succeeded", read_ok.load() == 12);
        stress::test("mixed concurrent writes all succeeded", write_ok.load() == 2);
    }

    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
