#include <cstring>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <pthread.h>
#include "log.h"

using namespace std;

Log :: Log() {
    m_count = 0;
}

Log :: ~Log() {
    if (m_fp != NULL) {
        fclose(m_fp);
    }


}

bool Log :: init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    if (max_queue_size >= 1) {
        m_log_queue = new block_queue<string>(max_queue_size);//创建阻塞队列

        //创建线程，异步写入
        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_split_lines = split_lines;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);


    //创建strcut tm变量保存时间
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //用strrchr查找最后一个‘/’， 返回指针
    const char *p = strrchr(file_name, '/');

    char log_full_name[292] = {0};     //创建一个局部缓冲区对文件名命名


    /*下面是命名规则代码：日志文件命名为：/年_月_日_文件名*/
    if (p == nullptr) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    } else {
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 291, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    cout<<"日志初始化完成  创建文件\n";
    m_fp = fopen(log_full_name, "w");
    fprintf(m_fp, "%s", "日志信息");
    fclose(m_fp);

    m_fp = fopen(log_full_name, "a");
    if (m_fp == nullptr) {
        cout<<" 文件创建失败\n";
        return false;
    }
    return true;
}

void Log::write_log(int level, const char* format, ...) {
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    
    //保存错误等级
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s,"[debug]:");
        break;
    case 1:
        strcpy(s,"[info]:");
        break;
    case 2:
        strcpy(s,"[warn]:");
        break;
    case 3:
        strcpy(s,"[error]:");
        break;
    default:
        strcpy(s,"[info]:");
        break;
    }

    //日志刷新
    m_mutex.lock();
    m_count++;
    //新的一天，或日志行数上限，创建新日志
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        //新的文件名
        char new_log[257] = {0};

        //刷新缓存并关闭文件
        fflush(m_fp);
        fclose(m_fp);

        //保存 年-月-日
        char tail[16] = {0};

        snprintf(tail, 16,"%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (m_today != my_tm.tm_mday) {//新的一天
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        } else {//日志写满了
            snprintf(new_log, 257, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }

        m_fp = fopen(new_log, "a");
        if (m_fp == NULL) {
            cout<< "········打开文件失败········" <<endl;
        }
    }
    m_mutex.unlock();


    //可变参数定义初始化，在vsprintf时使用，作用：输入具体的日志内容
    va_list valst;
    va_start(valst, format);

    //产出日志 log_str
    string log_str;

    m_mutex.lock();

    //开头格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d %s ", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec , s);
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);

    //添加换行和空格
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';

    log_str = m_buf;
    m_mutex.unlock();

    /*异步写*/
    if (!m_log_queue->full()) {
        m_log_queue->push(log_str);
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}