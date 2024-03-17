#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "blockqueue.h"
using namespace std;

class Log{

//单列模式
public:
    static Log *get_instance() {
        static Log instance;      
        return &instance;
    }
private:
    Log();
    virtual ~Log();



public:
    //初始化，并打开文件
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    //flush 确保所有待写的数据都被立即写入文件，而不是留在缓冲区中等待以后写入
    void flush(void);

    static void *flush_log_thread(void* args) {
        Log::get_instance()->write_log();
    }

private:
    void* write_log() {
        string single_log;
        while (m_log_queue->pop(single_log)) {
           fprintf(m_fp, "%s", "这是一条日志信息");
        }
    }

private:
    FILE *m_fp;        //打开log的文件指针
    block_queue<string> *m_log_queue;     //阻塞队列
    char *m_buf;              //缓冲区

    long long m_count = 0;         //日志行数记录
    int m_close_log;         //关闭日志标志
    int m_log_buf_size;      //日志缓冲区大小
    int m_split_lines;       //日志最大行数
    int m_today;             //当前是哪一天

    char log_name[128];      //log文件名
    char dir_name[128];        //地址名

    locker m_mutex;
};

#define LOG_DEBUG(format, ...) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...)  {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...)  {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}


#endif