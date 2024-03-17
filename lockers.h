#ifndef LOCKER_H
#define LOCKER_H

#include<unistd.h>
#include<pthread.h>
#include<exception>
#include<semaphore.h>


//线程同步机制封装类

//互斥锁
class locker {
public: 
    locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0){
            throw std::exception();
            }
    }
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    //获得锁
    pthread_mutex_t* get() {
        return &m_mutex;
    }



private:
    pthread_mutex_t m_mutex;
} ;


//条件变量
class cond{
public: 
    cond(){
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&m_cond);
    }
    //阻塞线程
    bool wait(pthread_mutex_t *mutex) {
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }
    bool timewait(pthread_mutex_t *mutex, struct timespec t) {
        return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
    }
    //唤醒一个线程
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    //将所有线程唤醒
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;

};

//信号量类

class sem{
public:
    sem(){
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(int n) {
        if (sem_init(&m_sem, 0, n) != 0) {
            throw std::exception();
        }
    }
    ~sem(){
        sem_destroy(&m_sem);
    }
    //p操作 信号量 - 1 如果信号量<0 阻塞等待
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
    //v操作 信号量 + 1， 如果信号量 <= 0 代表还有等待进程，由于已经有一个进程结束，且还有等待进程，则唤醒一个进程
    bool post(){
        return sem_post(&m_sem) == 0;
    }


private:
    sem_t m_sem;

};


#endif