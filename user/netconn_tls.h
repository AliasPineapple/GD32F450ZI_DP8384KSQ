#ifndef __NETCONN_TLS_H__
#define __NETCONN_TLS_H__

#include "lwip/netif.h"

#define TCP_CLIENT_RX_BUFSIZE	1500	//根据经验，电梯项目平台发送的一条消息不会超过1500
#define MAX_SERVER              3

//LWIP回调函数使用的结构体
typedef struct tcp_client_struct
{
	char state;             //当前连接状
	struct pbuf *p;         //指向接收/或传输的pbuf
    ip_addr_t remote_ip;    // dns解析出来的服务器IP地址
    int socket_id;
    uint8_t conn_id;
}tcp_client_t; 

struct enet_status_s {
    uint32_t enet_finish_init: 1;
    uint32_t enet_network_ok: 1;
    // uint32_t phy_finish_init: 1;
    // uint32_t link_status: 1;
};

typedef struct {
    char ip[16];
    char mask_sn[16];
    char gateway[16];
    char mac[32];
    int  current_socket;
    struct enet_status_s enet_status;
    tcp_client_t *client_info;
    uint32_t connect_fail[MAX_SERVER];
}enet_info_t;


//tcp服务器连接状态
enum tcp_client_states
{
	ES_TCPCLIENT_NONE = 0,		//没有连接
    ES_TCPCLIENT_GETREMOTE_IP,
	ES_TCPCLIENT_CONNECTED,		//连接到服务器了
    ES_TCPCLIENT_CLOSING,		//关闭连接
    ES_TCPCLIRNT_RECV_DATA,	    //
    ES_TCPCLIENT_SEND_FINISH, 
}; 

/*-----------------------------------------------------------------------*/
enet_info_t *get_enet_info(void);
void set_ntp_server(const char *ntp_server_url);
int enet_tcp_connect(uint8_t conn_id, const char *url, int port, uint8_t timeout);
int enet_tcp_send_data(const uint8_t conn_id, const uint8_t *buff, size_t size);
int enet_tcp_disconnect(int conn_id);

int enet_http_post(char *url, uint16_t data_len, char *data, uint8_t timeout);
int enet_http_get(char *url);
#if LWIP_HTTPC_POST_FILE
int enet_http_post_file(char *url, int32_t file_len, char *deviceid, uint32_t start_at, uint32_t end_at, char *send_name, const char *file_name, uint32_t logfile_size);
#endif
#endif

