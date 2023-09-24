/*
 * @Date: 2023-09-24 22:27:49
 * @LastEditors: Code Trainee 2249470506@qq.com
 * @LastEditTime: 2023-09-24 23:14:52
 */
#ifndef CPP_UTILS_STEALING_SIMPLE_THREAD_POOL_H
#define CPP_UTILS_STEALING_SIMPLE_THREAD_POOL_H

/**
 * source: https://github.com/progschj/ThreadPool.git
*/

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

namespace cpputils{

    namespace stealing{

        class SimpleThreadPool {
        public:
            SimpleThreadPool(size_t);
            template<class F, class... Args>
            auto enqueue(F&& f, Args&&... args) 
                -> std::future<typename std::result_of<F(Args...)>::type>;
            ~SimpleThreadPool();
        private:
            // need to keep track of threads so we can join them
            std::vector< std::thread > workers;
            // the task queue
            std::queue< std::function<void()> > tasks;
            
            // synchronization
            std::mutex queue_mutex;
            std::condition_variable condition;
            bool stop;
        };
        
        // the constructor just launches some amount of workers
        inline SimpleThreadPool::SimpleThreadPool(size_t threads)
            :   stop(false)
        {
            for(size_t i = 0;i<threads;++i)
                workers.emplace_back(
                    [this]
                    {
                        for(;;)
                        {
                            std::function<void()> task;

                            {  
                                std::unique_lock<std::mutex> lock(this->queue_mutex);
                                this->condition.wait(lock,
                                    [this]{ return this->stop || !this->tasks.empty(); });
                                if(this->stop && this->tasks.empty())
                                    return;
                                task = std::move(this->tasks.front());
                                this->tasks.pop();
                            }

                            task();
                        }
                    }
                );
        }

        // add new work item to the pool
        template<class F, class... Args>
        auto SimpleThreadPool::enqueue(F&& f, Args&&... args) 
            -> std::future<typename std::result_of<F(Args...)>::type>
        {
            using return_type = typename std::result_of<F(Args...)>::type;

            // 使用bind以使函数签名（形参）和tasks元素一致（void）
            auto task = std::make_shared< std::packaged_task<return_type()> >(
                    std::bind(std::forward<F>(f), std::forward<Args>(args)...)
                );
                
            std::future<return_type> res = task->get_future();
            {
                std::unique_lock<std::mutex> lock(queue_mutex);

                // don't allow enqueueing after stopping the pool
                if(stop)
                    throw std::runtime_error("enqueue on stopped SimpleThreadPool");

                // 再套一层函数（lambda）以使函数签名（返回值）和tasks元素一致（void）
                tasks.emplace([task](){ (*task)(); });
            }
            condition.notify_one();
            return res;
        }

        // the destructor joins all threads
        inline SimpleThreadPool::~SimpleThreadPool()
        {
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                stop = true;
            }
            condition.notify_all();
            for(std::thread &worker: workers)
                worker.join();
        }

    }

}

#endif