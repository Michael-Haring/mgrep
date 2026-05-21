/*
*
* */
#define CATCH_CONFIG_MAIN

#include "catch.hpp"
#include "threads.hpp"

/*
TEST_CASE("")
{
    REQUIRE();
}
* */

TEST_CASE("ThreadPool Construction")
{
    ThreadPool tp;
    REQUIRE(tp.m_workers.size() > 1);
    REQUIRE(tp.m_w_stats[2].ID == 2);
    REQUIRE(tp.m_workers.size() == tp.m_w_stats.size());
}

TEST_CASE("Work Allocation")
{
    ThreadPool tp;
    tp.m_tasks.push([&]() {
        std::lock_guard<std::mutex> lock(tp.m_cout_mtx);
        std::cout << "I am work - Work Allocation test work\n";
    });
    sleep(1);
    size_t max_completed = 0;
    for (size_t i = 0; i < tp.m_workers.size(); ++i) {
        if (tp.m_w_stats[i].tasks_completed > max_completed)
        max_completed =  tp.m_w_stats[i].tasks_completed;
    }
    REQUIRE(max_completed == 1);
}



