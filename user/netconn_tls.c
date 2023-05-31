#include <string.h>
#include <rtthread.h>
#include "gd32f4xx.h"
#include "netconn_tls.h"
#include "lwip/apps/http_client.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include "lwip/altcp_tls.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "cert.c"

static tcp_client_t tcp_client[MAX_SERVER] = {0};
static enet_info_t gs_enet_info; // attention please!!! it is inited in function enet_task

#if LWIP_DNS
/** DNS callback
 * If ipaddr is non-NULL, resolving succeeded and the request can be sent, otherwise it failed.
 */
static void
tcp_dns_found(const char* hostname, const ip_addr_t *ipaddr, void *arg)
{
    tcp_client_t* req = (tcp_client_t*)arg;

    LWIP_UNUSED_ARG(hostname);

    if (ipaddr != NULL) 
    {
        if (&req->remote_ip != ipaddr) 
        {
            /* fill in remote addr if called externally */
            req->remote_ip = *ipaddr;
        }
        req->state = ES_TCPCLIENT_GETREMOTE_IP;
    }
    else 
    {
        rt_kprintf("tcp dns: failed to resolve hostname: %s\n", hostname);
        req->state = ES_TCPCLIENT_NONE;
    }
}
#endif /* LWIP_DNS */

/*---------------------------------------------------------------------------*/
enet_info_t *get_enet_info(void)
{
    return &gs_enet_info;
}

/*--------------------------------------------------------------------------*/
void set_ntp_server(const char *ntp_server_url)
{
#if SNTP_SERVER_DNS
    sntp_setservername(0, ntp_server_url);
#endif
}
/*--------------------------------------------------------------------------*/
static void tcp_socket_recv_task(void *param);
int enet_tcp_connect(uint8_t conn_id, const char *url, int port, uint8_t timeout)
{   
    struct sockaddr_in client_addr;

    if (conn_id >= MAX_SERVER || url == RT_NULL || port < 0 || port > 65535)
    {
        rt_kprintf("enet_tcp_connect input param wrong\r\n");
        return -1;
    }

    rt_memset(&(tcp_client[conn_id]), 0, sizeof(tcp_client_t));

    {
        // rt_kprintf("start dns for url:%s\r\n", url);
        err_t err = dns_gethostbyname(url, &(tcp_client[conn_id].remote_ip), \
                tcp_dns_found, (void *)&(tcp_client[conn_id]));
        if (err != ERR_OK && err != ERR_INPROGRESS)
        {
            rt_kprintf("do dns fail! errno = %d, exit tcp_%d connect\r\n", err, conn_id);
            return -1;
        }
        else  if (err == ERR_INPROGRESS)
        {
            uint8_t count = 0;
            while (tcp_client[conn_id].state == ES_TCPCLIENT_NONE)
            {
                count++;
                if (count > timeout)
                {
                    rt_kprintf("DNS parse remote server IP timeout! exit tcp_%d!\r\n", conn_id);
                    goto fail_exit;
                }
                rt_thread_mdelay(1000);
            }
        }
    }

#if 0
    rt_kprintf("\r\n%s -- %d.%d.%d.%d \r\n", url, \
        ip4_addr1_16(&(tcp_client[conn_id].remote_ip)), \
        ip4_addr2_16(&(tcp_client[conn_id].remote_ip)), \
        ip4_addr3_16(&(tcp_client[conn_id].remote_ip)), \
        ip4_addr4_16(&(tcp_client[conn_id].remote_ip)));
#endif

    tcp_client[conn_id].socket_id = socket(AF_INET, SOCK_STREAM, 0);
    // rt_kprintf("tcp_%d sockeid = %d\r\n", conn_id, tcp_client[conn_id].socket_id);
    if (tcp_client[conn_id].socket_id < 0)
    {
        rt_kprintf("tcp_%d create socket error\n", conn_id);
        goto fail_exit;
    }

    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port);
    client_addr.sin_addr.s_addr = tcp_client[conn_id].remote_ip.addr;
    // client_addr.sin_addr.s_addr = inet_addr("101.200.58.212");
    rt_memset(&(client_addr.sin_zero), 0, sizeof(client_addr.sin_zero));

    if (connect(tcp_client[conn_id].socket_id, (struct sockaddr *)&client_addr, sizeof(struct sockaddr)) == -1)
    {
        rt_kprintf("Connect tcp_%d to %s:%d failed!\n", conn_id, url, port);
        closesocket(tcp_client[conn_id].socket_id);
        goto fail_exit;
    }
    rt_kprintf("tcp_%d connect %s success, start recv task now!\r\n", conn_id, url);
    gs_enet_info.connect_fail[conn_id] = 0;

    {
        char tname[RT_NAME_MAX] = {0};
        snprintf(tname, RT_NAME_MAX, "tcp%d_recv", conn_id);
        sys_thread_new(tname, tcp_socket_recv_task, (void *)&conn_id, 1024, 21);
        rt_thread_mdelay(3000);
    }
    return 0;

fail_exit:
    rt_memset(&(tcp_client[conn_id]), 0, sizeof(tcp_client_t));
    gs_enet_info.connect_fail[conn_id]++;
    if (gs_enet_info.connect_fail[conn_id] >= 2)
    {
        gs_enet_info.enet_status.enet_network_ok = 0;
        rt_kprintf("--------> Attention Please !!! <--------- ethernet no network!!!\r\n");
    }
    return -1;
}

int enet_tcp_send_data(const uint8_t conn_id, const uint8_t *buff, size_t size)
{
    if (conn_id >= MAX_SERVER)
        return -1;
    return write(tcp_client[conn_id].socket_id, buff, size);
}

int enet_tcp_disconnect(int conn_id)
{
    if (conn_id >= MAX_SERVER)
        return -1;
    return closesocket(tcp_client[conn_id].socket_id);
}

/* must be start after tcp connected */
static void tcp_socket_recv_task(void *param)
{
	int recv_data_len;
    uint8_t conn_id = *((uint8_t *)(param));
	char *recv_buf = RT_NULL;

    if (conn_id >= MAX_SERVER)
    {
        rt_kprintf("start tcp recv task fail, beacuse of conn_id(%u) not right\r\n", conn_id);
        return;
    }
    else if (conn_id < MAX_SERVER && tcp_client[conn_id].socket_id < 0)
    {
        rt_kprintf("start tcp recv task fail, because of invalid sockeid = %d\r\n", tcp_client[conn_id].socket_id);
        return;
    }
	while (1)
	{
        recv_buf = (char *)rt_malloc(TCP_CLIENT_RX_BUFSIZE);
        rt_memset(recv_buf, 0, TCP_CLIENT_RX_BUFSIZE);  //clear tcp recv buffer
		recv_data_len = recv(tcp_client[conn_id].socket_id, recv_buf, TCP_CLIENT_RX_BUFSIZE, 0);

		if (recv_data_len <= 0)
        {
            rt_kprintf("tcp_%d recv error!!!, will disconnect right now !!!\r\n", conn_id);
            rt_free(recv_buf);
            closesocket(tcp_client[conn_id].socket_id);
            break;
        }

        rt_kprintf("tcp_%d recv %d bytes\n", conn_id, recv_data_len);
	}
}

/* -------------------------------------------------------------------------------------------- */
static int http_url_parser(char *data, char **main_url, int *port, char **uri, int *protocal)
{
    char *msg_ptr  = RT_NULL;
    char *save_ptr = RT_NULL;

    msg_ptr = strtok_r(data, "://", &save_ptr);
    if (msg_ptr == RT_NULL)
    {
        rt_kprintf("strtok_r return fail\r\n");
        return -1;
    }
    // rt_kprintf("http protocal is %s\r\n", msg_ptr);
    if (rt_strcmp(msg_ptr, "https") == 0)
    {
        *protocal = 1;
    }
    else if (rt_strcmp(msg_ptr, "http") == 0)
    {
        *protocal = 0;
    }
    else
    {
        *protocal = -1;
    }

    if (rt_strstr(save_ptr, "//"))
        save_ptr += rt_strlen("//");
    msg_ptr = strtok_r(RT_NULL, ":", &save_ptr);
    if (msg_ptr == RT_NULL)
    {
        rt_kprintf("strtok_r return fail\r\n");
        return -1;
    }
    *main_url = msg_ptr;
    // rt_kprintf("url is %s\r\n", *main_url);
    
    msg_ptr = strtok_r(RT_NULL, "/", &save_ptr);
    if (msg_ptr == RT_NULL)
    {
        rt_kprintf("strtok_r return fail\r\n");
        return -1;
    }    
    *port = atoi(msg_ptr);
    // rt_kprintf("port is %d(%s)\r\n", *port, msg_ptr);
    
    *(save_ptr - 1) = '/';
    *(save_ptr - 2) = '0';
    *uri = save_ptr - 1;
    // rt_kprintf("uri is %s\r\n", *uri);
    
    return 0;
}

typedef struct http_param_cb_s{
    void *user_param;
    int http_result;
} http_param_cb_t;

void httpc_result_cb(void *arg, httpc_result_t httpc_result, u32_t rx_content_len, u32_t srv_res, err_t err)
{   
	rt_kprintf("The remote host closed the connection. result = %d, rx_content_len = %d, srv ret = %d, return code = %d\r\n", \
							httpc_result, rx_content_len, srv_res, err);
    rt_kprintf("Now I'm closing the connection.\r\n\n");
    if(arg != RT_NULL)
    {
        http_param_cb_t *http_param = RT_NULL;
        http_param = (http_param_cb_t *)arg;
        http_param->http_result = httpc_result;
    }
}

err_t httpc_headers_done_cb(httpc_state_t *connection, void *arg, struct pbuf *hdr, u16_t hdr_len, u32_t content_len)
{
	rt_kprintf("Number of headers %d, header len = %d, content len = %d\r\n", pbuf_clen(hdr), hdr_len, content_len);

	if (hdr != NULL) {
        char *headr = rt_malloc(hdr_len+1); // 这个值不能太大了
        if (headr != RT_NULL)
        {
            rt_memcpy(headr, (char *)hdr->payload, hdr_len);
            rt_kprintf("\r\n%s\r\n\n", headr);
            rt_free(headr);
        }
		pbuf_free(hdr);
    }

	return ERR_OK;
}

err_t httpc_get_recv_callback(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err)
{ 
    char *resp = rt_malloc(p->len+1); // 这个值不能太大了
    if (resp != RT_NULL)
    {
        rt_memcpy(resp, (char *)p->payload, p->len);
        rt_kprintf("\r\n%s\r\n\n", resp);
        rt_free(resp);
    }
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);

	return ERR_OK;
} 

static int httpc_connect(char *url, httpc_connection_t *http_setting, altcp_recv_fn recv_fn, http_param_cb_t* http_param, u8_t timeout) // just for ota
{
    err_t err;
    char *main_url = RT_NULL;
    char *url_copy = RT_NULL;
	char *uri = RT_NULL;
	int port = -1;
    int protocol = -1;
    httpc_state_t *http_state;
    altcp_allocator_t altcp_alloc;
	struct altcp_tls_config *conf = RT_NULL;

    if (url == RT_NULL || recv_fn == RT_NULL || http_setting == RT_NULL)
    {
        rt_kprintf("input param wrong !!!\r\n");
        return -1;
    }
    
    url_copy = rt_strdup(url); // free in http finish callback
    if (url_copy == RT_NULL)
    {
        rt_kprintf("rt_strdup failed \r\n");
        return -1;
    }

    if (http_url_parser(url_copy, &main_url, &port, &uri, &protocol) != 0)
    {
        rt_free(url_copy);
        rt_kprintf("parser http url failed\r\n");
        return -1;
    }

    if (protocol == 1)
    {
        conf = altcp_tls_create_config_client((const u8_t *)mbedtls_root_certificate, \
                                                mbedtls_root_certificate_len); 
        if (conf == RT_NULL)
        {
            rt_free(url_copy);
            rt_kprintf("create tls config fail\r\n");
            return -1;
        }

        altcp_alloc.alloc = altcp_tls_alloc;
        altcp_alloc.arg = conf;

	    http_setting->altcp_allocator = &altcp_alloc;
    }
    else
    {
        http_setting->altcp_allocator = RT_NULL;
    }

    http_param->http_result = -1;
    // rt_kprintf("protocal is %d, %s:%d%s\r\n", protocol, main_url, port, uri);
    err = httpc_get_file_dns(main_url, port, uri, http_setting, recv_fn, (void *)http_param, &http_state);

	if (err !=ERR_OK)
	{
		rt_kprintf("http client get/post fail = %d\r\n", err);
	}
	else
	{
        rt_kprintf("http client execuse get/post success\r\n");

        while (http_param->http_result == -1) // 待优化
        {
            rt_thread_mdelay(1000);
            err++;
            if (err > timeout)
            {
                rt_kprintf("http failed, error = %d\r\n", http_param->http_result);
                break;
            }
        }
	}

    rt_free(url_copy);

    if (http_param->http_result == HTTPC_RESULT_OK)
    {
        rt_kprintf("http client finish get/post\r\n");
        return 0;
    }
    rt_kprintf("http return fail = %d\r\n", http_param->http_result);
    return -1;
}
/*----------------------------------------------------------------------------------------------*/
int enet_http_get(char *url) 
{
    httpc_connection_t http_setting;
    http_param_cb_t http_param;
    
    if (url == RT_NULL)
    {
        rt_kprintf("input url NULL !!!\r\n");
        return -1;
    }

    http_param.http_result = -1;
    http_param.user_param = RT_NULL;

	http_setting.headers_done_fn = httpc_headers_done_cb;
	http_setting.result_fn = httpc_result_cb;
	http_setting.proxy_port = 0;
	http_setting.use_proxy = 0;
#if LWIP_HTTPC_SUPPORT_POST
    http_setting.use_post = 0;
    http_setting.datalen = 0;
    http_setting.user_headers = RT_NULL;
    http_setting.datalen = 0;
    http_setting.data = RT_NULL;
#if LWIP_HTTPC_POST_FILE
    http_setting.post_file = 0;
#endif
#endif
    ip_addr_set_zero_ip4(&http_setting.proxy_addr);

    return httpc_connect(url, &http_setting, httpc_get_recv_callback, (void *)&http_param, 30);
}

/*-----------------------------------------------------------------------*/
err_t httpc_post_recv_callback(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err)
{ 
    if (p != NULL) {
        char *resp = rt_malloc(p->len+1); // 这个值不能太大了
        if (resp != RT_NULL)
        {
            rt_memcpy(resp, (char *)p->payload, p->len);
            rt_kprintf("\r\n%s\r\n\n", resp);
            rt_free(resp);
        }
		pbuf_free(p);
		return ERR_OK;
    }

	return ERR_BUF;
}
int enet_http_post(char *url, uint16_t data_len, char *data, uint8_t timeout)
{
    httpc_connection_t http_setting;
    http_param_cb_t http_param;
    u8_t *post_header = RT_NULL;
    int ret = 0;

    if (url == RT_NULL || data_len == 0 || data == RT_NULL)
    {
        rt_kprintf("http post input param NULL !!!\r\n");
        return -1;
    }

    post_header = (u8_t *)rt_malloc(256);
    if (post_header == RT_NULL)
    {
        rt_kprintf("http post malloc failed\r\n");
        return -1;
    }

    rt_memset(post_header, 0, 256);
    snprintf((char *)post_header, 256, "Content-Length: %d\r\n" \
                                       "Content-Type: application/json; charset=utf-8\r\n", \
                                       data_len);

    rt_kprintf("post data size = %d\r\n%s\r\n", data_len, data);
	http_setting.headers_done_fn = httpc_headers_done_cb;
	http_setting.result_fn = httpc_result_cb;
	http_setting.proxy_port = 0;
	http_setting.use_proxy = 0;
#if LWIP_HTTPC_SUPPORT_POST
    http_setting.use_post = 1;
    http_setting.user_headers = post_header;
    http_setting.datalen = data_len;
    http_setting.data = (void *)data;
#if LWIP_HTTPC_POST_FILE
    http_setting.post_file = 0;
#endif
#endif
    ip_addr_set_zero_ip4(&http_setting.proxy_addr);

    http_param.http_result = -1;
    http_param.user_param = RT_NULL;

    ret =  httpc_connect(url, &http_setting, httpc_post_recv_callback, (void *)&http_param, timeout);
    rt_free(post_header);
    return ret;
}
#if LWIP_HTTPC_POST_FILE
#include "ff.h"
int enet_http_post_file(char *url, int32_t file_len, char *deviceid, uint32_t start_at, uint32_t end_at, char *send_name, const char *file_name, uint32_t logfile_size)
{
    httpc_connection_t http_setting;
    http_param_cb_t http_param;
    u8_t *post_header = RT_NULL;
    int ret = 0;
    FIL *fil = RT_NULL; 

    if (url == RT_NULL || deviceid == RT_NULL || file_len == 0 || file_name == RT_NULL)
    {
        rt_kprintf("http post input param NULL !!!\r\n");
        return -1;
    }

    fil = (FIL *)rt_malloc(sizeof(FIL));
    if (fil == RT_NULL)
    {
        rt_kprintf("enet_http_post_file, malloc fil fail\r\n");
        return -1;
    }
    rt_memset(fil, 0, sizeof(FIL));
    ret = f_open(fil, file_name, FA_READ);
    if (ret != FR_OK)
    {
        rt_kprintf("f_open %s return fail:%d\r\n", file_name, ret);
        rt_free(fil);
        return -1;
    }
    post_header = (u8_t *)rt_malloc(256);
    if (post_header == RT_NULL)
    {
        rt_kprintf("http post malloc failed\r\n");
        f_close(fil);
        rt_free(fil);
        return -1;
    }

    rt_memset(post_header, 0, 256);
    snprintf((char *)post_header, 256, "username: %s\r\n" \
                                       "Content-Length: %u\r\n" \
                                       "Content-Type: application/octet-stream; charset=utf-8\r\n" \
                                       "startAt: %u\r\n" \
                                       "endAt: %u\r\n" \
                                       "fileName: %s\r\n" \
                                       "length: %u\r\n", \
        deviceid, logfile_size, start_at, end_at, send_name, file_len);

    rt_kprintf("post %s size = %d\r\n%s\r\n", file_name, file_len, post_header);
	http_setting.headers_done_fn = httpc_headers_done_cb;
	http_setting.result_fn = httpc_result_cb;
	http_setting.proxy_port = 0;
	http_setting.use_proxy = 0;
#if LWIP_HTTPC_SUPPORT_POST
    http_setting.use_post = 1;
    http_setting.user_headers = post_header;
    http_setting.datalen = logfile_size;
    http_setting.data = (void *)fil;
#if LWIP_HTTPC_POST_FILE
    http_setting.post_file = 1;
#endif
#endif
    ip_addr_set_zero_ip4(&http_setting.proxy_addr);

    http_param.http_result = -1;
    http_param.user_param = RT_NULL;

    ret =  httpc_connect(url, &http_setting, httpc_post_recv_callback, (void *)&http_param, 30);
    f_close(fil);
    rt_free(post_header);
    rt_free(fil);
    return ret;
}
#endif


