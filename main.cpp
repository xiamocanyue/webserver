#include<iostream>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<signal.h>
#include"lockers.h"
#include"threadpool.h"
#include"http_cnn.h"
#include "blockqueue.h"
#include <assert.h>
#include "log.h"
#include "lst_timer.h"


#define MAX_FD 65534 // 最大的文件描述符个数
#define MAX_EVENT_NUM 10000 // 一次监听的最大数量
#define TIMESLOT 1 //定时器默认时间

static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;

//设置文件描述符非阻塞
int setnonblocking(int fd);

//添加信号捕捉
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction( sig, &sa, NULL );
}

//信号捕捉的回调函数
void sig_handler( int sig ) {
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

void addsig( int sig )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask );
    sigaction( sig, &sa, NULL );
}

// 定时器回调函数，它删除非活动连接socket上的注册事件。
void cb_func( client_data* user_data )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0 );
}

//
void timer_handler()
{
    // 定时处理任务，实际上就是调用tick()函数
    timer_lst.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。
    alarm(TIMESLOT);
}

//添加文件描述符到epoll中
extern void addfd(int fd, int epollfd, bool one_shot);
//从epoll中删除文件描述符
extern void removdfd(int fd, int epollfd);
// 修改文件描述符
extern void modfd(int fd, int epollfd, int en);


int main (int argc, char* argv[]) {
    if (argc <= 1) {
        printf("按照如下格式运行: %s port_numer \n",basename(argv[0]));
        exit(-1);
    }

    //初始化日志系统
    Log::get_instance()->init("./ServerLog", 0, 2000, 800000, 800);

    //获取端口号  atoi将字符转换成数字
    int port = atoi(argv[1]);

    //对SIGIPE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池，并初始化
    threadpool<http_cnn>* pool = NULL;
    
    try{
       // printf("创建线程池\n");
        pool = new threadpool<http_cnn>;
       // printf("创建成功\n");
    }catch(...) {
        LOG_ERROR("创建失败\n");
        exit(-1);
    }

    //创建数组保存所有的客户信息
    http_cnn * customer = new http_cnn[MAX_FD];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    //设置端口复用 一定在绑定之前设置
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
    bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    listen(listenfd, 5);

    //创建epoll对象 ， 创建epoll事件数组
    epoll_event events[MAX_EVENT_NUM];
    epollfd =  epoll_create(5);
    http_cnn :: m_epollfd = epollfd;    

    //将监听的文件描述符添加到epoll中
    addfd(listenfd, epollfd, false);

    //创建管道
    socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);

    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0],true);   //将读添加到epoll中

    // 设置信号处理函数
    addsig(SIGALRM);
    addsig(SIGTERM);
    bool stop_server = false;

    client_data* users = new client_data[MAX_FD]; 
    bool timeout = false;
    alarm(TIMESLOT);  // 定时,5秒后产生SIGALARM信号


    while (!stop_server) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
        if (num < 0 && errno != EINTR) {
            LOG_ERROR("epoll failure\n");
            break;
        }
        for (int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t len = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*) &client_address, &len);
                if (http_cnn::m_user_count >= MAX_FD) {
                    //说明目前最大连接数已经满了
                    //向客户端 回写等待
                    close(connfd);
                    continue;
                }
                customer[connfd].init(connfd, client_address);

                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;

                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;

                time_t cur = time(NULL);
                timer->expire = cur + 2 * TIMESLOT;
                users[connfd].timer = timer;
                timer_lst.add_timer(timer);

            } else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                // 处理信号
                int sig;
                char signals[1024];
                int ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if (ret == -1) {
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for (int i = 0; i < ret; i++) {
                        switch (signals[i])
                        {
                        case SIGALRM : {
                            // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                            // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                             cout << " *********** 执行 时间变化 ***********" << endl;
                            timer_handler();
                            timeout = false;
                            break;
                        }
                        case SIGTERM:{
                                stop_server = true;
                            }

                        }
                    }
                }
            } else if (events[i].events & EPOLLIN) {
                //一次性读出所有
                util_timer* timer = users[sockfd].timer;

                if (customer[sockfd].read()) {
                     std::cout << "开始读"  << "+++++++++++++++++++++++++++" << std::endl ;
                    pool->append(customer + sockfd);
                } else {// 发生错误，关闭连接，移除其对应的定时器
                    cb_func( &users[sockfd] );
                    if( timer ) {
                        timer_lst.del_timer(timer);
                        }
                    customer[sockfd].close_cnn();
                } 

            } else if (events[i].events & EPOLLOUT) {
                std::cout << "开始写"  << "*****************************" << std::endl ;
                //一次性写所有
                if (!customer[sockfd].write()) {
                    cout << "关闭写++" <<endl;
                    customer[sockfd].close_cnn();
                }
            }
        }
        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if( timeout ) {
            cout << " *********** 执行 时间变化 ***********" << endl;
            timer_handler();
            timeout = false;
        }

    }

    close(epollfd);
    close(listenfd);
    close( pipefd[1] );
    close( pipefd[0] );
    delete [] users;
    delete pool;
    delete [] customer;

    return 0;
}