/*
* @file     threads.cpp
* @author   Michael Haring
* @date     3/30/26
*
*
*
*
* */

#include "threads.hpp"
#include <iostream>
#include <cmath>
#include <fstream>
#include <mutex>
#include <stdexcept>




ThreadPool::ThreadPool()
{
    create_workers();
}

ThreadPool::ThreadPool(size_t num_threads)
{

}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_done = true;
    }
    notify_and_join();
}

void ThreadPool::push_task(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_done) {
            throw std::runtime_error("push_task stopped");
        }
        m_tasks.push(std::move(task));
        ++m_unfinished_tasks;
    }
    m_cv.notify_one();
}

size_t ThreadPool::create_workers()
{
    size_t max_threads = static_cast<size_t>(std::thread::hardware_concurrency() * 0.75);
    if (max_threads <= 0) {
        max_threads = 1;
    }

    m_w_stats.reserve(max_threads);
    m_w_stats.resize(max_threads);

    for (unsigned int i = 0; i < max_threads; ++i) {
        m_w_stats[i].ID = i;
        m_workers.emplace_back(&ThreadPool::work_loop, this, std::ref(m_w_stats[i]));
    }

    return max_threads;
}

void ThreadPool::work_loop(struct WorkerContext &w_stat)
{
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(m_mtx);

            m_cv.wait(lock, [this] {
                return !m_tasks.empty() || m_done;
            });

            if (m_done && m_tasks.empty()) {
                return; // exit thread
            }

            task = std::move(m_tasks.front());
            m_tasks.pop();
        }

        // do work outside the lock

        task();
        
        ++w_stat.tasks_completed;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            --m_unfinished_tasks;
            if (m_unfinished_tasks == 0) {
                m_done_cv.notify_all();
            }
        }
    }
}

void ThreadPool::wait_for_all()
{
    std::unique_lock<std::mutex> lock(m_mtx);

    m_done_cv.wait(lock, [this] {
        return m_unfinished_tasks == 0;
    });
}

void ThreadPool::notify_and_join()
{
    m_cv.notify_all();
    for (auto& worker : m_workers) {
        worker.join();
    }
}

bool ThreadPool::write_logs()
{
    std::ofstream out("log.txt", std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open log file\n";
        return false;
    }

    for (const auto& t : m_w_stats) {
        out.write(t.logBuffer.data(),
                  static_cast<std::streamsize>(t.logBuffer.size()));
    }
    return true;
}

void ThreadPool::fake_work()
{   
    std::lock_guard<std::mutex> lock(m_cout_mtx);
    std::cout << "I am fake work\n";
}

size_t ThreadPool::count_tasks_completed()
{
    size_t total_tasks_completed = 0;
    for (size_t i = 0; i < m_w_stats.size(); ++i) {
        total_tasks_completed += m_w_stats[i].tasks_completed;
    }
    return total_tasks_completed;
}




