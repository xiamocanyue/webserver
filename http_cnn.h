#ifndef HTTP_CNN_H
#define HTTP_CNN_H


#include<sys/epoll.h>
#include<iostream>
#include<stdio.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<stdlib.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<errno.h>
#include "lockers.h"
#include <sys/uio.h>
#include<cstring>
#include<stdarg.h>




class http_cnn {
public:
    static int m_epollfd;  //所有的socket上的事件都被注册到一个epoll
    static int m_user_count; //统计用户数量
    static const int READ_BUFF_SIZE = 2048;  //读缓冲区的大小
    static const int WRITE_BUFF_SIZE = 2048;  //写缓冲区的大小
    static const int FILENAME_LEN = 200; // 文件名的最大长度

    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };




    http_cnn(){}
    ~http_cnn(){}
    //初始化新进入的链接
    void init(int sockfd, const sockaddr_in & addr);

    //处理客户端的请求
    void process();

    //关闭链接
    void close_cnn();

    //非阻塞 的读
    bool read(); 

    //非阻塞的写
    bool write();


   
    


private:
    int m_sockfd; //该http链接的socket
    sockaddr_in m_address; //通信的socket的地址
    char m_read_buf[READ_BUFF_SIZE];  //读缓冲区
    int m_read_index; //标识读缓冲区中已经读入的客户端的最后一个字节的下一个位置
    char m_write_buf[WRITE_BUFF_SIZE];
    int m_checked_index;  //当前正在分析的字符在缓存取得位置
    int m_start_line;     //当前正在解析的行的起始位置

//解析请求的参数
    char* m_url;   //请求的url
    char* m_version;  // http版本
    METHOD m_method;  //请求方法  只支持get
    char* m_host ; //主机名
    bool m_keepconnect; //http是否要保持连接  即参数 keepconnect 的值
    char m_real_file[FILENAME_LEN];   //客户端请求的完整路径
    int m_content_length;  //http请求的消息总长度



    CHECK_STATE m_check_state; //主状态机的状态

//状态机的函数
    void init();  //初始化连接的其他信息
    LINE_STATUS parse_line();     //获取一行，交给下面解析
    HTTP_CODE process_read();    //解析http请求

    bool process_write(HTTP_CODE ret);   // 响应http请求

    HTTP_CODE parse_request_line(char * text);   // 解析请求首行
    HTTP_CODE parse_headers_line(char * text);    // 解析请求头
    HTTP_CODE parse_content_line(char * text);    //解析请求体
    char* getline() {     //获得一行数字
        return m_read_buf + m_start_line;
    }
    HTTP_CODE do_request();   //


// 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

    int m_write_index;                    //写缓存中待发送的字节数
    char* m_file_address;                 //客户请求的文件被mmap到内存的起始位置
    struct stat m_file_stat;              //目标文件的状态， 是否存在，可读，是否为目录，并获得文件大小
    struct iovec m_iv[2];                 //用writev来写 有两块内存一个是m_file_address, 一个是m_write_buf;
    int m_iv_count;                       //表示被写内存块的数量




};
















#endif