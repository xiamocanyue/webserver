#include"http_cnn.h"
#include<sys/epoll.h>


int http_cnn::m_epollfd = -1;
int http_cnn::m_user_count = 0; 

//http响应的一些状态
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";


//根目录
const char* doc_root = "/home/xiamo/linux/resources" ;


//设置文件描述符非阻塞
int setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}



//向epoll中添加文件描述符
void addfd(int fd, int epollfd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //设置文件描述符非阻塞
    setnonblocking(fd);

}

//epoll 中删除文件描述符
void removdfd(int fd, int epollfd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//epoll中修改文件描述符
void modfd(int fd, int epollfd, int en) {
    epoll_event event;
    event.data.fd = fd;
    event.events = en | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd , &event);
}

//初始化链接
void http_cnn::init(int sockfd, const sockaddr_in & addr) {
    m_sockfd = sockfd;
    m_address = addr;

    //设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //添加到epoll对象中
    addfd(sockfd, m_epollfd, true);
    //总用户数 + 1
    m_user_count++;
    init();

}

void http_cnn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;  //初始状态为解析请求首行
    m_checked_index = 0;  
    m_start_line = 0;
    m_read_index = 0;
    m_url = 0;
    m_version = 0;
    m_method = GET;
    m_keepconnect = false;

    m_content_length = 0;


    bzero(m_read_buf, READ_BUFF_SIZE);
    bzero(m_write_buf, WRITE_BUFF_SIZE);
    bzero(m_real_file, FILENAME_LEN);

}
//关闭链接
void http_cnn::close_cnn() {
    if(m_sockfd != -1) {
        removdfd(m_sockfd, m_epollfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//循环读取客户数据，直到无数据可读
bool http_cnn::read() {
  if (m_read_index >= READ_BUFF_SIZE) {
        return false;
    }
    //已经读取到的字节
    int bytes_read = 1;
    while(true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUFF_SIZE - m_read_index, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                //表示没有数据
                break;
            }
            return  bytes_read;
        } else if (bytes_read == 0) {
            //对方关闭
            return false;
        } 
        m_read_index += bytes_read;
        
    }
    printf("读取到了数据 %s \n", m_read_buf);
    return  true;

}

bool http_cnn::write() {
    //std::cout << "进入写函数"  << "*****************************" << std::endl ;
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = m_write_index;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(m_keepconnect) {
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            } else {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
    
    
}

//解析http请求, 主状态机
http_cnn::HTTP_CODE http_cnn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    
    char *text = 0;
    
    // 解析到了请求体  或者  解析到一行完整的数据
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || (line_status = parse_line()) == LINE_OK) {
        //获得一行数据
        text = getline();
        m_start_line = m_checked_index;
        //printf("获得一行: %s的语句\n", text);
    switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE : {
            ret = parse_request_line(text);
            
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER : {
           
           // std::cout << "请求头"<< std::endl;
            ret = parse_headers_line(text);
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }else if (ret == GET_REQUEST) {
                return do_request();
            }
            break;

        }
        case CHECK_STATE_CONTENT : {
            //std::cout << "解析请求体" << std::endl;
            ret = parse_content_line(text);
            if (ret == GET_REQUEST) {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;

        }
        default : {
            std::cout << "m_check_state == default" << std::endl;
            return INTERNAL_ERROR;
        }
        }
    }
        return NO_REQUEST;
}   

// 解析请求首行 , 获得请求方法， 目标url， http版本
http_cnn::HTTP_CODE http_cnn::parse_request_line(char * text) {

    //获得数据 GET /index.heml http/1.1

    m_url = strpbrk(text, " \t");    // m_url -> \t/index.heml http/1.1
    if (! m_url) { 
        return BAD_REQUEST;
    }

    *m_url++ = '\0';     // ->\0index.heml http/1.1

    char* method = text;    // text 变成 GET\0
    if (strcasecmp(method, "GET") == 0) {
        m_method == GET;
    } else {
        return BAD_REQUEST;
    }

    
    m_version = strpbrk(m_url, " \t");//m_url == /index.heml http/1.1    m_version == \thttp/1.1

    if (!m_version) return BAD_REQUEST;

    *m_version++ = '\0';    // 先将 \n 变成 \0 ,之后指针向后移动一位  m_versiont == http/1.1

    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    //http://192.168.138.141:10000/index/xml
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;  // m_url -> 182.168.141:10000/index.xml
        m_url = strchr(m_url, '/');  // m_url -> /index.xml
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    //请求首行已经解析完毕， 状态转变成 解析请求头
    m_check_state = CHECK_STATE_HEADER;

    return NO_REQUEST;
} 

// 解析请求头
http_cnn::HTTP_CODE http_cnn::parse_headers_line(char * text) {
    //printf("开始解析请求头\n");
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
           // std::cout << "开始解析圣体"  << "*****************************" << std::endl ;
            return NO_REQUEST;
        } 
        //m_content_length == 0,说明解析完了
        // std::cout << "解析完了"  << "*****************************" << std::endl ;
        return GET_REQUEST;

    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_keepconnect = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段

        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST; 
}  
  
   //解析请求体 
http_cnn::HTTP_CODE http_cnn::parse_content_line(char * text) {
    if ( m_read_index  >= ( m_content_length + m_checked_index ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
} 

  //获取一行,判断依据/r/n
http_cnn::LINE_STATUS http_cnn::parse_line() {
    //std::cout << "进入parse——line " << std::endl;
    char temp;
    for (; m_checked_index < m_read_index; m_checked_index++) {
        temp = m_read_buf[m_checked_index];
        if (temp == '\r') {
            if ((m_checked_index + 1) == m_read_index) {
                //std::cout << "第332 line——open" << std::endl;
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_index + 1] == '\n') {
                m_read_buf[m_checked_index++] = '\0';       //将'/r’ 更改为‘\0'
                m_read_buf[m_checked_index++] = '\0';       //将'/n' 更改为'/0' ， 同时将m_checked_index 指向\n 的下一个 ？？
                //std::cout << "337行 returnlined——ok " << std::endl;
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_index > 1 && m_read_buf[m_checked_index - 1] == '\r') {
                m_read_buf[m_checked_index-1] = '\0';      //将'/r’ 更改为‘\0'
                m_read_buf[m_checked_index++] = '\0';      //将'/n' 更改为'/0' ， 同时将m_checked_index 指向\n 的下一个 ？？
               // std::cout << "345 reutrn line——ok " << std::endl;
                return LINE_OK;
            }
           // std::cout << "348 如同让你line——bad " << std::endl;
            return LINE_BAD;
        }
        
    }
   // std::cout << "353 returnline——open " << std::endl;
    return LINE_OPEN;
}   


// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_cnn::HTTP_CODE http_cnn :: do_request() {
    //std::cout << "开始构造响应体"  << "*****************************" << std::endl ;
     // "/home/xiamo/linux/resources"
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }
   
    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    //std::cout << "内存映射陈公公"  << "*****************************" << std::endl ;
    close( fd );
    return FILE_REQUEST;
}

// 对内存映射区接触关联
void http_cnn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

//向响应报文中填写数据
bool http_cnn::add_response( const char* format, ... ) {

    if( m_write_index >= WRITE_BUFF_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_index, WRITE_BUFF_SIZE - 1 - m_write_index, format, arg_list );
    if( len >= ( WRITE_BUFF_SIZE - 1 - m_write_index ) ) {
        return false;
    }
    m_write_index += len;
    va_end( arg_list );
    return true;
}

bool http_cnn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_cnn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_keepconnect == true ) ? "keep-alive" : "close" );
}

bool http_cnn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_cnn::add_headers(int content_len) {
    
    add_content_length(content_len);
   
    add_content_type();
    
    add_linger();
    
    add_blank_line();
    
}

bool http_cnn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_cnn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_cnn::add_content( const char* content )
{
    return add_response( "%s", content );
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_cnn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
           // std::cout << "结束add——head"  << "*****************************" << std::endl ;
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_index;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            //std::cout << "return  true"  << "*****************************" << std::endl ;
            return true;
            
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_index;
    m_iv_count = 1;
    //std::cout << "返回成功"  << "*****************************" << std::endl ;
    return true;
}


//由线程池中的工作线程调用， 处理http请求
void http_cnn::process() {

    //解析http请求
    HTTP_CODE read_ret =  process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    //生成响应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_cnn();
    }
    modfd(m_sockfd, m_epollfd, EPOLLOUT);

}





