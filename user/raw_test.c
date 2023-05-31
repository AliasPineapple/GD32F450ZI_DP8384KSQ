#include "gd32f4xx.h"
#include <rtthread.h>
#include "lwip/dns.h"


err_t tcp_client_connect_callback(void *arg, struct tcp_pcb *tpcb, err_t err);
err_t tcp_client_recv(void *arg,struct tcp_pcb *tpcb,struct pbuf *p,err_t err);
void  tcp_client_error(void *arg,err_t err);
err_t tcp_client_poll(void *arg, struct tcp_pcb *tpcb);
err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
void  tcp_client_senddata(struct tcp_pcb *tpcb, struct tcp_client_struct * es);
void  tcp_client_connection_close(struct tcp_pcb *tpcb, struct tcp_client_struct * es);


static void lwip_dns_callback (const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    if (callback_arg == RT_NULL)
    {
        uint8_t ip[4];

        ip[0] = (((const u8_t*)(&(ipaddr)->addr))[0]);
        ip[1] = (((const u8_t*)(&(ipaddr)->addr))[1]);
        ip[2] = (((const u8_t*)(&(ipaddr)->addr))[2]);
        ip[3] = (((const u8_t*)(&(ipaddr)->addr))[3]);

        rt_kprintf("%s ====> %d.%d.%d.%d\r\n", name, ip[3], ip[2], ip[1], ip[0]);
    }
    else
    {
        *((char *)callback_arg) = ES_TCPCLIENT_GETREMOTE_IP;
    }
}

static int ipv4_verify_address(const char *ip, ip_addr_t *ipaddr)
{
    int a,b,c,d;
    char t;

    if (4 == sscanf(ip, "%d.%d.%d.%d%c", &a, &b, &c, &d, &t))
    {
        if (a >= 0 && a <= 255 &&\
            b >= 0 && b <= 255 &&\
            c >= 0 && c <= 255 &&
            d >= 0 && d <= 255)
        {
            // (((u8_t*)(&(ipaddr)->addr))[0]) = a;
            // (((u8_t*)(&(ipaddr)->addr))[1]) = b;
            // (((u8_t*)(&(ipaddr)->addr))[2]) = c;
            // (((u8_t*)(&(ipaddr)->addr))[3]) = d;

            IP4_ADDR(ipaddr, a, b, c, d);
            return 0;
        }
    }
    return -1;
}

int enet_tcp_connect(uint8_t socket_id, const char *url, int port, uint8_t timeout)
{
	uint8_t count = 0;
    if (socket_id >= MAX_SERVER || url == RT_NULL || port < 0 || port > 65535)
    {
        rt_kprintf("enet_tcp_connect input param wrong\r\n");
        return -1;
    }

    rt_memset(&(tcp_client[socket_id]), 0, sizeof(tcp_client_t));

    if (ipv4_verify_address(url, &(tcp_client[socket_id].remote_ip)) != 0)
    {
        rt_kprintf("start dns for url:%s\r\n", url);
        if (ERR_OK != dns_gethostbyname_addrtype(url, \
                                                 &(tcp_client[socket_id].remote_ip), \
                                                 lwip_dns_callback, \
                                                 (void *)&(tcp_client[socket_id].state), \
                                                 LWIP_DNS_ADDRTYPE_IPV4))
        {
            // Get remote ip failed, Use default value
//            rt_kprintf("DNS get remote ip from %s failed, use default value %d.%d.%d.%d\r\n", url, \
//                                                                                              IP_S_ADDR0, \
//                                                                                              IP_S_ADDR1, \
//                                                                                              IP_S_ADDR2, \
//                                                                                              IP_S_ADDR3);

            // IP4_ADDR(&(tcp_client[socket_id].remote_ip), IP_S_ADDR0, IP_S_ADDR1, IP_S_ADDR2, IP_S_ADDR3);
            rt_kprintf("DNS get remote ip from %s failed, exit task\r\n", url);
			return -1;
        }
    }
	else
	{
		tcp_client[socket_id].state = ES_TCPCLIENT_GETREMOTE_IP;
	}
	while (tcp_client[socket_id].state == ES_TCPCLIENT_NONE)
	{
		count++;
		if (count > timeout)
		{
			rt_kprintf("DNS parse remote server IP failed! exit task!\r\n");
			return -1;
		}
		rt_thread_mdelay(1000);
	}

    rt_kprintf("start tcp create\r\n");

    /* create a new TCP control block */
    tcp_client[socket_id].pcb = tcp_new();

    if (tcp_client[socket_id].pcb != RT_NULL)
    {
        if (ERR_OK == tcp_connect(tcp_client[socket_id].pcb, &(tcp_client[socket_id].remote_ip), port, tcp_client_connect_callback))
        {
			tcp_client[socket_id].socket_id = socket_id;
            tcp_arg(tcp_client[socket_id].pcb, &(tcp_client[socket_id]));
            return 0;
        }
    }

    return -1;
}

err_t tcp_client_connect_callback(void *arg, struct tcp_pcb *tpcb, err_t err)
{
	struct tcp_client_struct *es = (struct tcp_client_struct *)arg;  
	
    if(err == ERR_OK)   
	{
		rt_kprintf("tcp_%d connect success\r\n", es->socket_id);
		if(es != RT_NULL)
		{
 			es->state = ES_TCPCLIENT_CONNECTED;//状态为连接成功  
			es->p = NULL; 

			tcp_recv(tpcb, tcp_client_recv);  	//初始化LwIP的tcp_recv回调功能   
			tcp_err(tpcb,  tcp_client_error); 	//初始化tcp_err()回调函数
			tcp_sent(tpcb, tcp_client_sent);    //初始化LwIP的tcp_sent回调功能
			tcp_poll(tpcb, tcp_client_poll, 10); 	//初始化LwIP的tcp_poll回调功能

            // maybe I should use tcp_client to instead net_status!!!!!!!  
            gs_enet_info.net_status[es->socket_id].has_connect_server = ES_TCPCLIENT_CONNECTED; //标记连接到服务器了
			err = ERR_OK;
		}
	}
    else
	{
		tcp_client_connection_close(tpcb, RT_NULL);//关闭连接
	}
	return err;
}

err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{ 
	rt_int32_t data_len = 0;
	struct tcp_client_struct *es;
	err_t ret_err; 
	char *recv_buf = RT_NULL;

	LWIP_ASSERT("arg != NULL", arg != NULL);
	es = (struct tcp_client_struct *)arg; 

	if (p == NULL)//如果从服务器接收到空的数据帧就关闭连接
	{
		es->state = ES_TCPCLIENT_CLOSING; //需要关闭TCP 连接了 (在poll回调中进行关闭)
 		es->p = p; 
		ret_err = ERR_OK;
	}
    else if (err != ERR_OK)//当接收到一个非空的数据帧,但是err!=ERR_OK
	{ 
		if (p)
		{
            pbuf_free(p);//释放接收pbuf
		}
		ret_err = err;
	}
    else if (es->state == ES_TCPCLIENT_CONNECTED)	//当处于连接状态时
	{
		if (p != NULL)//当处于连接状态并且接收到的数据不为空时
		{
			recv_buf = (char *)rt_malloc(TCP_CLIENT_RX_BUFSIZE);
			LWIP_ASSERT("recv_buf != RT_NULL", recv_buf != RT_NULL);
			rt_memset(recv_buf, 0, TCP_CLIENT_RX_BUFSIZE);  //数据接收缓冲区清零
#if 0
			struct pbuf *q;
			for (q = p; q != RT_NULL; q = q->next)  //遍历完整个pbuf链表
			{
				//判断要拷贝到TCP_CLIENT_RX_BUFSIZE中的数据是否大于TCP_CLIENT_RX_BUFSIZE的剩余空间，如果大于
				//的话就只拷贝TCP_CLIENT_RX_BUFSIZE中剩余长度的数据，否则的话就拷贝所有的数据
				if(q->len > (TCP_CLIENT_RX_BUFSIZE - data_len)) 
                {
					rt_memcpy(recv_buf + data_len, q->payload, (TCP_CLIENT_RX_BUFSIZE - data_len));//拷贝数据
				}
				else
				{ 
                    rt_memcpy(recv_buf + data_len, q->payload, q->len);
				}

				data_len += q->len;  	
				
                if(data_len >= TCP_CLIENT_RX_BUFSIZE) 
                {
					break; //超出TCP客户端接收数组,跳出	
				}
			}
#else
			data_len = ((p->len > TCP_CLIENT_RX_BUFSIZE) ? TCP_CLIENT_RX_BUFSIZE : p->len);
			rt_memcpy(recv_buf, p->payload, data_len);
#endif
			tcp_recv_msg_t msg;
			msg.connectid = es->socket_id;
			msg.data_size = data_len;
			msg.data_ptr  = recv_buf;

			extern rt_mq_t tcp_recv_mq;

			if(RT_EOK != rt_mq_send_wait(tcp_recv_mq, &msg, sizeof(tcp_recv_msg_t), 5))
			{
				rt_kprintf("\r\n\n----------->tcp data recv mq overflow.\r\n\n");
				rt_free(recv_buf);
			}

 			tcp_recved(tpcb, p->tot_len);	//用于获取接收数据,通知LWIP可以获取更多数据
			pbuf_free(p);  					//释放内存
			ret_err = ERR_OK;
		}
	}
    else  //接收到数据但是连接已经关闭,
	{ 
		tcp_recved(tpcb, p->tot_len);	//用于获取接收数据,通知LWIP可以获取更多数据
		es->p = NULL;
		pbuf_free(p); 					//释放内存
		ret_err = ERR_OK;
	}
	return ret_err;
} 

//lwIP tcp_err函数的回调函数
void tcp_client_error(void *arg, err_t err)
{  
    struct tcp_client_struct *es;
    es = (struct tcp_client_struct *)arg;

    rt_kprintf("TCP CLIENT ERROR: %d\r\n", err);
    
    tcp_client_connection_close(es->pcb, es);
} 

err_t tcp_client_poll(void *arg, struct tcp_pcb *tpcb)
{
	err_t ret_err;
	struct tcp_client_struct *es; 
	es = (struct tcp_client_struct*)arg;
	if(es != NULL)  //连接处于空闲可以发送数据
	{
        if(es->state == ES_TCPCLIENT_CLOSING)
		{ 
 			tcp_client_connection_close(tpcb, es);//关闭TCP连接
		} 
		ret_err = ERR_OK;
	}
	return ret_err;
} 

int enet_tcp_send_data(const uint8_t socket_id, const uint8_t *buff, size_t size)
{
	struct tcp_client_struct *es; 

	es = (struct tcp_client_struct*)(&(tcp_client[socket_id]));
	if(es != NULL)
	{
		es->p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_POOL);	//申请内存 
		pbuf_take(es->p, (char*)buff, size);					//将tcp_client_sentbuf[]中的数据拷贝到es->p_tx中
		tcp_client_senddata(es->pcb, es);						//将tcp_client_sentbuf[]里面复制给pbuf的数据发送出去
		if (es->p)
		{
			pbuf_free(es->p);	//释放内存
		}
		return ERR_OK;
	}
	return ERR_ARG;
} 

//lwIP tcp_sent的回调函数(当从远端主机接收到ACK信号后发送数据)
err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
	struct tcp_client_struct *es;
	LWIP_UNUSED_ARG(len);
	es = (struct tcp_client_struct*)arg;

	if (es->p)
        tcp_client_senddata(tpcb, es);//发送数据
	return ERR_OK;
}
//此函数用来发送数据
void tcp_client_senddata(struct tcp_pcb *tpcb, struct tcp_client_struct *es)
{
	struct pbuf *ptr; 
 	err_t wr_err=ERR_OK;

	//将要发送的数据加入到发送缓冲队列中
	while ((wr_err==ERR_OK) && es->p && (es->p->len <= tcp_sndbuf(tpcb)))
	{
		ptr = es->p;
		wr_err = tcp_write(tpcb, ptr->payload, ptr->len, TCP_WRITE_FLAG_COPY);
		if (wr_err == ERR_OK)
		{  
			es->p = ptr->next;			//指向下一个pbuf
			if(es->p)
			{
				pbuf_ref(es->p);	//pbuf的ref加一
			}
			pbuf_free(ptr);				//释放ptr 
		}
		else if (wr_err == ERR_MEM)
		{
			es->p = ptr;
		}
		tcp_output(tpcb);		//将发送缓冲队列中的数据立即发送出去
	} 
} 
//关闭与服务器的连接
void tcp_client_connection_close(struct tcp_pcb *tpcb, struct tcp_client_struct * es)
{
	if (tpcb != RT_NULL)
	{
		tcp_close(tpcb);
		//移除回调
		tcp_arg(tpcb, NULL);  
		tcp_recv(tpcb, NULL);
		tcp_sent(tpcb, NULL);
		tcp_err(tpcb, NULL);
		tcp_poll(tpcb, NULL, 0);
		tcp_abort(tpcb);//终止连接,删除pcb控制块
		tpcb = RT_NULL;
	}
	if(es)
    {
        rt_memset(&(gs_enet_info.net_status[es->socket_id]), 0x0, sizeof(struct net_state_s));
		gs_enet_info.net_status[es->socket_id].has_connect_server = ES_TCPCLIENT_CLOSING;
    }
}

int enet_tcp_disconnect(int socket_id)
{
	struct tcp_client_struct *es; 

	es = (struct tcp_client_struct*)(&(tcp_client[socket_id]));
	if(es != NULL)
	{
		rt_kprintf("stop tcp_%d connect\r\n", es->socket_id);
		tcp_client_connection_close(es->pcb, es);
		return 0;
	}
	return -1;
}

/*---------------------------------------------------------------------------------------------------------*/

void enet_tcp_test_task(void *param)
{
	int ret = 0;

re_start:
	ret = enet_tcp_connect(INSTRUCT_SERVER_CONNECT_ID, "192.168.1.98", 8087, 30);
	ret = enet_tcp_connect(DELIVER_MASTER_SERVER_CONNECT_ID, "192.168.1.98", 8000, 30);
	if (ret == 0)
	{
		while (ret < 20)
		{
			enet_tcp_send_data(INSTRUCT_SERVER_CONNECT_ID, "test1-zhangxiong\r\n", 17);
			enet_tcp_send_data(DELIVER_MASTER_SERVER_CONNECT_ID, "test2-zhangxiong\r\n", 17);
			rt_thread_mdelay(5000);
			ret++;
		}
		rt_kprintf("stop tcp connect\r\n");
		enet_tcp_disconnect(INSTRUCT_SERVER_CONNECT_ID);
		enet_tcp_disconnect(DELIVER_MASTER_SERVER_CONNECT_ID);

		rt_thread_mdelay(10000);
		goto re_start;
	}
}

extern int netconn_test(void);
int enet_tcp_test(void)
{
    rt_thread_t tcp_tid = RT_NULL;
    
    tcp_tid = rt_thread_create("enet_tcp_test", enet_tcp_test_task, RT_NULL, 1024, 21, 20);
    if (tcp_tid != RT_NULL)
    {
        rt_thread_startup(tcp_tid);
    }
    else
    {
        return -1;
    }

    netconn_test();
    return 0;
}


/*--------------------http------------------------------*/
// http://edu.chengdu.gov.cn/cdedu/c113025/2023-05/16/e5765b6737d94c6483160611a568a776/files/0914a8eb90414be588be67a1894e9ef3.pdf
#define HTTPC_CLIENT_AGENT "lwIP/" //LWIP_VERSION_STRING" (http://savannah.nongnu.org/projects/lwip)"

/* GET request basic */
#define HTTPC_REQ_11 "GET %s HTTP/1.1\r\n" /* URI */\
    "User-Agent: %s\r\n" /* User-Agent */ \
    "Accept: */*\r\n" \
    "Connection: Close\r\n" /* we don't support persistent connections, yet */ \
    "\r\n"
#define HTTPC_REQ_11_FORMAT(uri) HTTPC_REQ_11, uri, HTTPC_CLIENT_AGENT

struct tcp_pcb *http_pcb;

int tcp_send_packet(void)
{
	err_t error;
	uint32_t len = 0;

    const char *string = "/login?redirect=http%3A%2F%2F101.200.58.212%3A8064%2F";

	char *header_str = (char *)rt_malloc(TCP_CLIENT_RX_BUFSIZE);
	
	rt_memset(header_str, 0, TCP_CLIENT_RX_BUFSIZE);

	len = rt_snprintf(header_str, TCP_CLIENT_RX_BUFSIZE, HTTPC_REQ_11_FORMAT(string));

    /* push to buffer */
    error = tcp_write(http_pcb, string, len, TCP_WRITE_FLAG_COPY);

    if (error) {
        rt_kprintf("ERROR: Code: %d (tcp_send_packet :: tcp_write)\n", error);
        return -1;
    }

    /* now send */
    error = tcp_output(http_pcb);
    if (error) {
        rt_kprintf("ERROR: Code: %d (tcp_send_packet :: tcp_output)\n", error);
        return -1;
    }
    return 0;
}

err_t connectCallback(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    rt_kprintf("HTTP Connection Established.\n");
    rt_kprintf("Now sending a packet\n");
    tcp_send_packet();
    return 0;
}

void tcpErrorHandler(void *arg, err_t err)
{  
    rt_kprintf("http ERROR: %d\r\n", err);
} 

err_t tcpRecvCallback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    rt_kprintf("Data recieved.\n");
    if (p == NULL) {
        rt_kprintf("The remote host closed the connection.\n");
        rt_kprintf("Now I'm closing the connection.\n");
        tcp_client_connection_close(tpcb, RT_NULL);
        return ERR_ABRT;
    } else {
        rt_kprintf("Number of pbufs %d\n", pbuf_clen(p));
        rt_kprintf("Contents of pbuf %s\n", (char *)p->payload);
    }

    return 0;
}

int enet_http_get_data(char *url, uint16_t port, char *buffer, uint16_t *size, uint16_t timeout)
{
    ip_addr_t rmtipaddr;
	char ip_state = 0;
	uint16_t count = 0;
	uint32_t data = 0xdeadbeef; /* dummy data to pass to callbacks*/

    if (ipv4_verify_address(url, &rmtipaddr) != 0)
    {
        rt_kprintf("start dns for url:%s\r\n", url);
        dns_gethostbyname_addrtype(url, &rmtipaddr, lwip_dns_callback, (void *)&ip_state, LWIP_DNS_ADDRTYPE_IPV4);
		while (ip_state == 0)
		{
			rt_thread_mdelay(1000);
			count++;
			if (count >= timeout)
			{
				rt_kprintf("Parse %s fail\r\n", url);
				return -1;
			}
		}
    }

	rt_kprintf("%s ====> %d.%d.%d.%d\r\n", url, ip4_addr1_16(&rmtipaddr), \
                   ip4_addr2_16(&rmtipaddr), ip4_addr3_16(&rmtipaddr), ip4_addr4_16(&rmtipaddr));

    /* create a new TCP control block */
    http_pcb = tcp_new();

    if (http_pcb != NULL)
    {
        tcp_arg(http_pcb, &data);
		/* register callbacks with the pcb */
		tcp_err(http_pcb, tcpErrorHandler);
		tcp_recv(http_pcb, tcpRecvCallback);
		// tcp_sent(http_pcb, tcpSendCallback);

        tcp_connect(http_pcb, &rmtipaddr, port, connectCallback);

		rt_thread_mdelay(5000);
    }

	return 0;  
}

int http_test(void)
{
	enet_http_get_data("101.200.58.212", 8064, RT_NULL, 0, 30);
    // IP4_ADDR(&rmtipaddr, 212, 58, 200, 101);
    
    return 0;
}

MSH_CMD_EXPORT(http_test, http test);



