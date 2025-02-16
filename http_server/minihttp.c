#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <error.h>
#include<errno.h>

#define SERVER_PORT 80

static int debug = 1;

int get_line(int sock,char *buf,int size);

void do_http_request(int client_sock);

void do_http_responsel(int client_sock);

void do_http_response(int client_sock,const char *path);

void headers(int client_sock,FILE *file);

void cat(int client_sock,FILE *file);

void not_found(int client_sock);

void inner_error(int client_sock);

int main(int argc,char*argv[])
{
    int sock;//信箱

    struct sockaddr_in server_addr;//服务器地址

    sock=socket(AF_INET,SOCK_STREAM,0);//创建信箱,第一个是IPV4，第二个表示面向连接的套接字，第三个表示使用默认协议

    bzero(&server_addr,sizeof(server_addr));//清空地址，因为是局部变量

    server_addr.sin_family=AF_INET;//协议家族,IPV4

    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);//ip地址,htonl将主机字节序转换为网络字节序,INADDR_ANY表示任意ip地址

    server_addr.sin_port=htons(SERVER_PORT);//端口号,htons将主机字节序转换为网络字节序
    bind(sock,(struct sockaddr*)&server_addr,sizeof(server_addr));//绑定地址给socket

    listen(sock,64);//监听socket,64表示最大连接数，同一时刻可以连接64个客户端

    printf("等待客户端连接\n");

    while(1){
        struct sockaddr_in client;//客户端地址
        int client_sock, len;
        char client_ip[64];
        char buf[256];

        socklen_t client_addr_len;//客户端地址长度
        client_addr_len = sizeof(client);//获取客户端地址长度
        client_sock = accept(sock,(struct sockaddr *)&client,&client_addr_len);//接受客户端连接，返回一个新的socket,client_addr_len表示客户端地址长度

        printf("client ip: %s\t port : %d\n",inet_ntop(AF_INET,&client.sin_addr.s_addr,client_ip,sizeof(client_ip)),ntohs(client.sin_port));//打印客户端地址和端口号
        //处理http请求，读取客户端发送的数据
        do_http_request(client_sock);
        close(client_sock);
    }

    close(sock);
    return 0;
}

void do_http_request(int client_sock)
{
    int len = 0;
    char buf[256];
    char method[64];
    char url[256];
    char path[512];

    struct stat st;
    //读取客户端发送的http请求，一行一行
    //1.读取请求行

         len = get_line(client_sock,buf,sizeof(buf));
         
         if(len > 0 )
         {
            //读到请求行
            int i = 0,j=0;
            while(!isspace(buf[j]) && (i<sizeof(method)-1))
            {
                method[i]=buf[j];
                i++;
                j++;
            }
            method[i] = '\0';
            if(debug) printf("request method:%s\n",method);

            if(strncasecmp(method,"GET",i) == 0)
            {
                //只处理get请求
                if(debug) printf("method = GET\n");

                //获取url
                while(isspace(buf[j++]));//跳过白空格,调用ctype.h库里的函数
                i = 0;

                while(!isspace(buf[j]) && (i<sizeof(url)-1))
                {
                    url[i]=buf[j];
                    i++;
                    j++;
                }

                url[i] = '\0';

                if(debug) printf("url: %s\n",url);

                do{
                    len = get_line(client_sock, buf, sizeof(buf));
                    if(debug) printf("read: %s\n",buf);
                }while(len>0);

                //定位本地的html文件
                //处理url中的?

                    char *pos = strchr(url,'?');
                    if(pos)
                    {
                        *pos = '\0';
                        printf("real url: %s\n",url);
                    } 
                sprintf(path,"./html_docs/%s",url);
                if(debug) printf("path: %s\n",path);

                //执行http相应
                //判断文件是否存在，如果存在就响应200 OK，如果不存在就显示404
                if(stat(path,&st) == -1)
                {
                    fprintf(stderr, "stat failed,reason:%s\n",strerror(errno));
                    not_found(client_sock);
                }else{
                        //文件存在
                    if(S_ISDIR(st.st_mode))
                    {
                        strcat(path,"/index.html");//如果是目录，就加上index.html
                    }
                    do_http_response(client_sock,path);
                }
            }else{
                //非GET请求,读取http头部，并相应客户端501
                fprintf(stderr,"other request [%s]",method);
                do{
                    len = get_line(client_sock, buf, sizeof(buf));
                    if(debug) printf("read: %s\n",buf);
                }while(len>0);

                //unimplemented(client_sock);

            }


        }else {
            //请求格式有问题
            //bad_request(client_sock);
        }

}

void do_http_response(int client_sock,const char *path)
{
    FILE *file = NULL;
    file = fopen(path,"r");//打开文件
    if(file == NULL){
        not_found(client_sock);
        return ;
    }
    //1.发送http头部
    headers(client_sock,file);
    //2。发送文件内容
    cat(client_sock,file);

    fclose(file);
}

void headers(int client_sock,FILE *file)
{
    struct stat st;
    int fileid = 0;
    char tmp[64];
    char buf[1024] = {0};
    strcpy(buf,"HTTP/1.1 200 OK\r\n");
    strcat(buf,"Server:Fujun Server\r\n");
    strcat(buf,"Content-Type:text/html\r\n");
    strcat(buf,"Connection:close\r\n");
    
    fileid = fileno(file);//获取文件描述符

    if(fstat(fileid,&st) == -1){
        inner_error(client_sock);
    }//获取文件状态

    snprintf(tmp,64,"Content-Length:%ld\r\n\r\n",st.st_size);//获取文件大小
    strcat(buf,tmp);//将文件大小添加到头部

    if(debug) fprintf(stdout,"header: %s\n",buf);

    if(send(client_sock,buf,strlen(buf),0)<0){
        fprintf(stderr,"send header failed. reason: %s\n",strerror(errno));
    }//发送头部
}


//将html文件按行读取给客户端
void cat(int client_sock,FILE *file)
{
    char buf[1024];

    fgets(buf,1024,file);//按行读取文件

    while(!feof(file)){
        int len = write(client_sock,buf,strlen(buf));
        if(debug) fprintf(stdout,"%s",buf);
        if(len <= 0){
            fprintf(stderr,"send body failed. reason: %s\n",strerror(errno));
            break;
        }
        if(debug) fprintf(stdout,"%s",buf);

        fgets(buf,1024,file);//按行读取文件
    }

}

void do_http_responsel(int client_sock)
{
    const char *main_header = "HTTP/1.1 200 OK\r\nServer:Fujun Server\r\nContent-Type:text/html\r\nConnection:close\r\n";//固定的头部
    const char * welcome_content = "<html><head><title>Welcome to fujun server</title></head><body><h1>Welcome to fujun server</h1></body></html>";//固定的内容(后期要改)

    //1.发送头部
    int len = write(client_sock,main_header,strlen(main_header));

    if(debug) fprintf(stdout,"...do_http_response...\n");
    if(debug) fprintf(stdout,"write[%d]: %s",len,main_header);
    //2.生成 Content-Length并发送
    char send_buf[64];
    int wc_len = strlen(welcome_content);
    len = sprintf(send_buf, "Content-Length:%d\r\n", wc_len);
    len = write(client_sock,send_buf,len);

    if(debug) fprintf(stdout,"write[%d]: %s",len,send_buf);


    //3.发送内容
    len = write(client_sock,welcome_content,wc_len);
    if(debug) fprintf(stdout,"write[%d]: %s",len,welcome_content);
    //4.关闭sock
}

int get_line(int sock,char *buf,int size)
{
    int count = 0;//计数器，当前读了多少字符
    char ch = '\0';
    int len = 0;
    
    while((count<size-1) && (ch != '\n'))
    {
        len = read(sock,&ch,1);//每次从sock里读取一个字节，传递到ch里,并返回读取的长度len

        if(len == 1){
            if(ch == '\r'){
                continue;
            }else if(ch == '\n'){
                break;
            }

            //这里处理一般的字符
            buf[count] = ch;
            count++;
        }else if(count == -1){
            //读取出错
            perror("read failed");
            count = -1;//计时也表示出错
            break;
        }else {
            //read返回0，客户端sock关闭
            fprintf(stderr,"client close.\n");
        }
    }

    if(count >= 0) buf[count] = '\0';

    return count;
}

void not_found(int client_sock)
{
    // 打印调试信息
    if (debug) fprintf(stdout, "...not_found...\n");

    // 打开 404.html 文件
    FILE *file = fopen("./html_docs/404.html", "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open 404.html: %s\n", strerror(errno));
        return;
    }

    // 获取文件大小
    struct stat st;
    if (fstat(fileno(file), &st) == -1) {
        fprintf(stderr, "Failed to get file stats: %s\n", strerror(errno));
        fclose(file);
        return;
    }

    // 发送 HTTP 头部
    char buf[1024];
    snprintf(buf, sizeof(buf), 
        "HTTP/1.1 404 Not Found\r\n"
        "Server: Fujun Server\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n", 
        st.st_size);
    
    int len = send(client_sock, buf, strlen(buf), 0);
    if (len < 0) {
        fprintf(stderr, "Failed to send header: %s\n", strerror(errno));
        fclose(file);
        return;
    }

    // 发送文件内容
    while (fgets(buf, sizeof(buf), file) != NULL) {
        len = send(client_sock, buf, strlen(buf), 0);
        if (len < 0) {
            fprintf(stderr, "Failed to send file content: %s\n", strerror(errno));
            break;
        }
    }

    fclose(file);
}


void inner_error(int client_sock)
{

    const char *reply="HTTP/1.1 500 INTERNAL SERVER ERROR\r\nServer:Fujun Server\r\nContent-Type:text/html\r\nConnection:close\r\n\
    \r\n\
    <HTML>\r\n\
    <HEAD>\r\n\
    <TITLE>Method Not Implemented</TITLE>\r\n\
    </HEAD>\r\n\
    <BODY>\r\n\
        <P>服务器内部出错.\r\n\
    </BODY>\r\n\
    </HTML>";  

    int len = write(client_sock,reply,strlen(reply));
    if(debug) fprintf(stdout,"%s",reply);
    
    if(len <= 0){
        fprintf(stderr,"send reply failed. reason: %s\n",strerror(errno));
    }

}
