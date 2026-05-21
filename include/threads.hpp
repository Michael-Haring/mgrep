/*
* @file     threads.hpp
* @author   Michael Haring
* @date     3/30/26
*
*
*
* */

#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <queue>


struct WorkerContext {
    size_t ID = 0;
    //std::string logBuffer;
    size_t tasks_completed = 0;
};

class ThreadPool {
public:
    ThreadPool();
    ThreadPool(size_t num_threads);
    ~ThreadPool();

    void push_task(std::function<void()> task);
    size_t create_workers();

    void work_loop(struct WorkerContext &w_stat);

    void wait_for_all();

    void notify_and_join();

    bool write_logs();

    void fake_work();

    size_t count_tasks_completed();
    
    std::vector<std::thread> m_workers;
    std::vector<struct WorkerContext> m_w_stats;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mtx;
    std::mutex m_cout_mtx;
    std::condition_variable m_cv;

    std::condition_variable m_done_cv;
    size_t m_unfinished_tasks = 0;

    bool m_done = false;

private:


};
