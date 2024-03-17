#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<unistd.h>
#include<pthread.h>
#include<exception>
#include<semaphore.h>
#include<stdio.h>
#include<list>
#include "lockers.h"
#include "log.h"


template<typename T>
class threadpool {
public:
    threadpool(int thread_num = 8, int max_request = 100000);
    ~threadpool();
    //添加任务
    bool append(T* request);  
private:    
    static void* worker(void* arg);
    void run();

private:
    //线程数量
    int m_threadnum;
    
    //线程数组  大小为线程数量
    pthread_t *m_threads;

    //请求队列中 允许最多的等待处理的请求数量
    int m_max_requests;

    //请求队列
    std::list<T*> m_workqueue;

    //互斥锁
    locker m_queuelock;

    //信号量 判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;


};

template<typename T> 
threadpool<T>::threadpool(int thread_num, int max_request):m_threadnum(thread_num), m_max_requests(max_request), m_stop(false), m_threads(NULL)
{   
    if (thread_num <= 0 || max_request <= 0) {
        LOG_ERROR("初始化threadpool");
        throw "std::runtime_error(54\n)";
    } 
    
    //创建线程数组
    m_threads = new pthread_t[m_threadnum];
    
    if (!m_threads) {
        LOG_ERROR("创建线程数组");
        throw "std::runtime_error(59)";
    }
    //创建线程 thread_num个，并设置线程脱离
    for (int i = 0; i < thread_num; i++) {
       // printf("creat the %d dth thread\n",i);

        int ret = pthread_create(m_threads + i, NULL, worker, this);
        if (ret != 0) {
            delete [] m_threads;
            LOG_ERROR("创建线程");
            throw "std::runtime_error(68)";
        }
        
        //设置线程分离
        ret = pthread_detach(m_threads[i]);
        if (ret != 0) {
            delete m_threads;
            LOG_ERROR("线程分离");
            throw "std::runtime_error(74)";
        }
    }
}

template<typename T> 
threadpool<T> :: ~threadpool() {
    delete[] m_threads;
    m_stop = true; 
}

template<typename T> 
bool threadpool<T> :: append(T* request){
    m_queuelock.lock();
    if (m_workqueue.size() > m_max_requests) {
        m_queuelock.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelock.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T> :: worker(void* arg) {
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T> :: run() {
    while(!m_stop) {
        m_queuestat.wait();
        m_queuelock.lock();
        if (m_workqueue.empty()) {
            m_queuelock.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelock.unlock();
        if (!request) {
            continue;
        }
       request->process();
    }
}









#endif