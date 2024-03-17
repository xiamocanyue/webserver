#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include<iostream>
#include<stdlib.h>
#include<pthread.h>
#include<sys/time.h>
#include"lockers.h"

using namespace std;

template<class T>
class block_queue{
public:

    block_queue(int max_size = 1000, int size = 0, int front = -1, int back = -1);
    ~block_queue();

    //清空队列
    void clear();
    //判断队列是否已满
    bool full();
    //判断是否为空
    bool empty();
    //获得队首元素， 保存到value
    bool front(T &value);
    //获得队尾元素
    bool back(T& value);
    //获得队列当前元素个数
    int size();
    //获取队列最大元素个数
    int max_size();

    // 生产者产生了日志，加入队列中
    bool push(T& item);

    //消费者 将日志弹出，打印
    bool pop(T& item);

    
private:
    locker m_mutex;    //互斥锁
    cond m_cond;      //条件变量

    T* m_array;
    //队列包含元素的最大数量
    int m_max_size;
    //当前队列元素个数
    int m_size;
    //指向头元素
    int m_front;
    //指向尾元素
    int m_back;
};


template<typename T>
block_queue<T> :: block_queue(int max_size, int size, int front, int back)
                            : m_max_size(max_size), m_size(size), m_front(front), m_back(back) {
    if (max_size <= 0) {
            exit(-1);
        }

    m_array = new T[max_size];
      
}

template<typename T>
block_queue<T> :: ~block_queue() {
        m_mutex.lock();

        if (m_array != NULL) {
            delete[] m_array;
        }

        m_mutex.unlock();
    }

template<typename T>
void block_queue<T> :: clear() {
        m_mutex.lock();

        m_size = 0;
        m_front = -1;
        m_back = -1;

        m_mutex.unlock();
    }

template<typename T>
bool block_queue<T> :: full() {
        m_mutex.lock();

        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }

        m_mutex.unlock();
        return false;
    }

template<typename T>
bool block_queue<T> :: empty() {
        m_mutex.lock();

        if (m_size == 0) {
            m_mutex.unlock();
            return true;
        }

        m_mutex.unlock();
        return false;
    }

template<typename T>
bool block_queue<T> :: front(T &value) {
        m_mutex.lock();
       
        if (m_size == 0) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[(m_front + 1) % m_max_size];
       
        m_mutex.unlock();
        return true;
    }

template<typename T>
bool block_queue<T> :: back(T& value) {
        m_mutex.lock();

        if (size == 0) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        
        m_mutex.unlock();
        return true;
    }

template<typename T>
int block_queue<T> :: size() {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }

template<typename T>
int block_queue<T> :: max_size() {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }

template<typename T>
bool block_queue<T> :: push(T& item) {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;

        m_cond.broadcast();
        m_mutex.unlock();

        return true;
    }

template<typename T>
bool block_queue<T> :: pop(T& item) {
        m_mutex.lock();
        while (m_size <= 0) {
            if (!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }



#endif