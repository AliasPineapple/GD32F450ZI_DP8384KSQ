#include "gd32f4xx.h"
#include <rtthread.h>

#include "lwip/opt.h"

#include "lwip/sys.h"
#include "lwip/api.h"
static struct netconn *conn;
static void client(void *thread_param)
{
	int ret;
	ip4_addr_t ipaddr;

	uint8_t send_buf[]= "This is a TCP Client test...\n";

	while (1)
	{
		conn = netconn_new(NETCONN_TCP);            
		if (conn == NULL)
		{
			rt_kprintf("create conn failed!\n");
			rt_thread_mdelay(1000);
			continue;
		}

		IP4_ADDR(&ipaddr, 192,168,1,98);            

		ret = netconn_connect(conn, &ipaddr, 8001);   
		if (ret == -1)
		{
			rt_kprintf("Connect failed!\n");
			netconn_close(conn);                    
			rt_thread_mdelay(1000);
			continue;
		}

		rt_kprintf("Connect to http server successful!\n");

		while (1)
		{
			ret = netconn_write(conn,send_buf,sizeof(send_buf),0); 

			rt_thread_mdelay(1000);
		}
	}
}

static void client_recv(void *param)
{
	struct netbuf *buf;
	void *data;
	u16_t len;
	

	while ((netconn_recv(conn, &buf)) == ERR_OK)
	{
		rt_kprintf("netconn Recved data\n");
		do
		{
			netbuf_data(buf, &data, &len); 
			rt_kprintf("\r\n%s\r\n", data);            
		}
		while (netbuf_next(buf) >= 0);                     
		netbuf_delete(buf);                         
	}
}

#if 1
#include "lwip/apps/http_client.h"

void httpc_result_cb(void *arg, httpc_result_t httpc_result, u32_t rx_content_len, u32_t srv_res, err_t err)
{
	rt_kprintf("The remote host closed the connection. result = %d, rx_content_len = %d, srv ret = %d, return code = %d\r\n", \
							httpc_result, rx_content_len, srv_res, err);
    rt_kprintf("Now I'm closing the connection.\n");
}

err_t httpc_headers_done_cb(httpc_state_t *connection, void *arg, struct pbuf *hdr, u16_t hdr_len, u32_t content_len)
{
	rt_kprintf("Number of headers %d, header len = %d, content len = %d\r\n", pbuf_clen(hdr), hdr_len, content_len);

	if (hdr != NULL) {
        rt_kprintf("Contents of pbuf :\r\n%s\r\n", (char *)hdr->payload);
		pbuf_free(hdr);
    }

	return ERR_OK;
}
httpc_connection_t http_setting;

err_t httpc_recv_callback(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err)
{ 
    // err_t ret_err;
	
    if (p != NULL) {
        rt_kprintf("Number of pbufs %d\n", pbuf_clen(p));
		// struct pbuf *q;
		// for (q = p; q != RT_NULL; q = q->next)  //遍历完整个pbuf链表
		{
			rt_kprintf("Contents of pbuf (%d):\r\n%s\r\n", p->len, (char *)p->payload);
		}

		pbuf_free(p);
		return ERR_OK;
    }


	return ERR_BUF;
} 

#include "lwip/dns.h"
#include "lwip/altcp_tls.h"
#include "cert.c"

int socket_http_test(void)
{
	err_t err;
	// ip_addr_t rmtipaddr;
	httpc_state_t *http_state;
	const char url[] = "www.baidu.com";
	const char uri[] = "/";
	int port = 443;

    // {
    //     rt_kprintf("start dns for url:%s\r\n", url);
    //     if (ERR_OK != dns_gethostbyname_addrtype(url, \
    //                                              &rmtipaddr, \
    //                                              RT_NULL, \
    //                                              RT_NULL, \
    //                                              LWIP_DNS_ADDRTYPE_IPV4))
    //     {
    //         rt_kprintf("DNS get remote ip from %s failed\r\n", url);
	// 		//return -1;
    //     }
    // }	
	// rt_kprintf("%s ====> %d.%d.%d.%d\r\n", url, ip4_addr1_16(&rmtipaddr), \
    //                ip4_addr2_16(&rmtipaddr), ip4_addr3_16(&rmtipaddr), ip4_addr4_16(&rmtipaddr));

	uint16_t i, len;
	char tmp[260] = {0};
	
	rt_kprintf("ca len = %d\r\n%s\r\n", mbedtls_root_certificate_len);
	for (i = 0; i < mbedtls_root_certificate_len;)
	{
		rt_memset(tmp, 0, 260);
		len = ((mbedtls_root_certificate_len - i) > 256 ? 256 : (mbedtls_root_certificate_len - i));
		rt_memcpy(tmp, &mbedtls_root_certificate[i], len);
		i += len;
		rt_kprintf("%s", tmp);
	}
    rt_kprintf("\r\n");
    
	struct altcp_tls_config * conf = altcp_tls_create_config_client((const u8_t *)mbedtls_root_certificate, mbedtls_root_certificate_len);

	http_setting.headers_done_fn = httpc_headers_done_cb;
	http_setting.result_fn = httpc_result_cb;
	ip_addr_set_zero_ip4(&http_setting.proxy_addr);
	http_setting.proxy_port = 0;
	http_setting.use_proxy = 0;

	http_setting.altcp_allocator = (altcp_allocator_t *)rt_malloc(sizeof(altcp_allocator_t));
	http_setting.altcp_allocator->alloc = altcp_tls_alloc;
	http_setting.altcp_allocator->arg = conf;

	err = httpc_get_file_dns(url, port, uri, &http_setting, httpc_recv_callback, RT_NULL, &http_state);
	// err = httpc_get_file(&rmtipaddr, 80, "/forum.php", &http_setting, httpc_recv_callback, RT_NULL, &http_state);
	if (err !=ERR_OK)
	{
		rt_kprintf("http client get fail = %d\r\n", err);
	}
	else
	{
		rt_kprintf("http client get success\r\n");
	}
    rt_thread_mdelay(5000);
    rt_kprintf("free resource\r\n");
    rt_thread_mdelay(2000);
	// altcp_tls_free_config(conf);
	rt_free(http_setting.altcp_allocator);
    return 0;
}

MSH_CMD_EXPORT(socket_http_test, http test);
#endif


/*----------------------------------*/
#include <lwip/sockets.h>
int sock = -1;
static void socket_client(void *thread_param)
{
	struct sockaddr_in client_addr;

	uint8_t socket_send_buf[]= "This is a socket TCP Client test...\n";

	while (1)
	{
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0)
		{
			rt_kprintf("Socket error\n");
			rt_thread_mdelay(10);
			continue;
		}

		client_addr.sin_family = AF_INET;
		client_addr.sin_port = htons(8002);
		client_addr.sin_addr.s_addr = inet_addr("192.168.1.98");
		rt_memset(&(client_addr.sin_zero), 0, sizeof(client_addr.sin_zero));

		if (connect(sock,
					(struct sockaddr *)&client_addr,
					sizeof(struct sockaddr)) == -1)
		{
			rt_kprintf("Connect failed!\n");
			closesocket(sock);
			rt_thread_mdelay(10);
			continue;
		}

		rt_kprintf("Connect to socket http server successful!\n");

		while (1)
		{
			if (write(sock, socket_send_buf, sizeof(socket_send_buf)) < 0)
				break;

			rt_thread_mdelay(1000);
		}

		closesocket(sock);
	}

}

static void socket_recv(void *param)
{
	int recv_data_len;
	char recv_data[250] = {0};

	while (1)
	{
		recv_data_len = recv(sock, recv_data, 250, 0);

		if (recv_data_len <= 0)
			break;

		rt_kprintf("recv %d len data: %s\n",recv_data_len, recv_data);

		write(sock, recv_data, recv_data_len);
	}
}

int netconn_test(void)
{
    sys_thread_new("netconn_client", client, NULL, 1024, 21);
	sys_thread_new("netconn_recv", client_recv, NULL, 1024, 21);
	sys_thread_new("socket_client", socket_client, NULL, 1024, 21);
	sys_thread_new("socket_recv", socket_recv, NULL, 2048, 21);
	return 0;
}
MSH_CMD_EXPORT(netconn_test, netconn tcp test);








