/*!
    \file    netconf.c
    \brief   network connection configuration

    \version 2016-08-15, V1.0.0, firmware for GD32F4xx
    \version 2018-12-12, V2.0.0, firmware for GD32F4xx
    \version 2020-09-30, V2.1.0, firmware for GD32F4xx
    \version 2022-03-09, V3.0.0, firmware for GD32F4xx
*/

/*
    Copyright (c) 2022, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/apps/sntp.h"
#include "netif/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/opt.h"
#include "ethernetif.h"
#include "stdint.h"
#include "netconf.h"
#include <stdio.h>
#include "lwip/priv/tcp_priv.h"
#include "lwip/timeouts.h"
#include "enet_drv.h"
#include "netconn_tls.h"

#ifdef USE_HTTPD
#include "lwip/apps/httpd.h"
#endif


#define MAX_DHCP_TRIES        3

typedef enum {
    DHCP_START = 0,
    DHCP_WAIT_ADDRESS,
    DHCP_ADDRESS_ASSIGNED,
    DHCP_TIMEOUT
} dhcp_state_enum;

#ifdef USE_DHCP
uint32_t dhcp_fine_timer = 0;
uint32_t dhcp_coarse_timer = 0;
dhcp_state_enum dhcp_state = DHCP_START;
#endif /* USE_DHCP */

#ifdef USE_DNS
uint32_t dns_timer = 0;
#endif // USE_DNS

struct netif g_mynetif;
uint32_t tcp_timer = 0;
uint32_t arp_timer = 0;
ip_addr_t ipaddr = {0};
ip_addr_t netmask = {0};
ip_addr_t gw = {0};

void lwip_dhcp_process_handle(void);

/*!
    \brief      initializes the LwIP stack
    \param[in]  none
    \param[out] none
    \retval     none
*/
void lwip_stack_init(void)
{
    tcpip_init(NULL, NULL);

    /* IP addresses initialization */
    /* USER CODE BEGIN 0 */
#ifdef USE_DHCP
    ip_addr_set_zero_ip4(&ipaddr);
    ip_addr_set_zero_ip4(&netmask);
    ip_addr_set_zero_ip4(&gw);
#else
    IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
    IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
    IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
    {
        enet_info_t *enet_info = get_enet_info();
        if (enet_info == RT_NULL) 
        {
            rt_kprintf("get enet pointer fail\r\n");
        }
        else
        {
            enet_info->enet_status.enet_network_ok = 1;
        }
    }
#endif /* USE_DHCP */

    /* Initilialize the LwIP stack without RTOS */
    /* add the network interface (IPv4/IPv6) without RTOS */
    netif_add(&g_mynetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &tcpip_input);
    /* registers the default network interface */
    netif_set_default(&g_mynetif);

#ifdef USE_DHCP
    dhcp_start(&g_mynetif);
#endif /* USE_DHCP */
    /* when the netif is fully configured this function must be called */
    if (netif_is_link_up(&g_mynetif))
    {
        netif_set_up(&g_mynetif);
        netif_set_link_callback(&g_mynetif, netif_link_callback); // 网线拔掉了不能触发这个回调函数，不能用于检测网线掉了的情况
        netif_set_status_callback(&g_mynetif, netif_status_callback);
    }
    else
    {
        netif_set_down(&g_mynetif);
    }

#ifdef USE_HTTPD
    httpd_init();
#endif

#ifdef USE_SNTP
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
#if LWIP_DHCP
    sntp_servermode_dhcp(1); /* get SNTP server via DHCP */
#else /* LWIP_DHCP */
#if LWIP_IPV4
    sntp_setserver(0, netif_ip_gw4(netif_default));
#endif /* LWIP_IPV4 */
#endif /* LWIP_DHCP */    
    sntp_init();   
#endif

}

/*!
    \brief      LwIP periodic tasks
    \param[in]  localtime the current LocalTime value
    \param[out] none
    \retval     none
*/
void lwip_periodic_handle(__IO uint32_t localtime)
{
#if LWIP_TCP
    /* TCP periodic process every 250 ms */
    if(localtime - tcp_timer >= TCP_TMR_INTERVAL) {
        tcp_timer =  localtime;
        tcp_tmr();
    }

#endif /* LWIP_TCP */

    /* ARP periodic process every 5s */
    if((localtime - arp_timer) >= ARP_TMR_INTERVAL) {
        arp_timer = localtime;
        etharp_tmr();
    }

#ifdef USE_DHCP
    /* fine DHCP periodic process every 500ms */
    if(localtime - dhcp_fine_timer >= DHCP_FINE_TIMER_MSECS) {
        dhcp_fine_timer =  localtime;
        dhcp_fine_tmr();
        if((DHCP_ADDRESS_ASSIGNED != dhcp_state) && (DHCP_TIMEOUT != dhcp_state)) {
            /* process DHCP state machine */
            lwip_dhcp_process_handle();
        }
    }

    /* DHCP coarse periodic process every 60s */
    if(localtime - dhcp_coarse_timer >= DHCP_COARSE_TIMER_MSECS) {
        dhcp_coarse_timer =  localtime;
        dhcp_coarse_tmr();
    }

#endif /* USE_DHCP */

#if  USE_DNS
    if(localtime - dns_timer >= DNS_TMR_INTERVAL) {
        dns_timer =  localtime;
        dns_tmr();
    }

#endif

}

#ifdef USE_DHCP
/*!
    \brief      lwip_dhcp_process_handle
    \param[in]  none
    \param[out] none
    \retval     none
*/
void lwip_dhcp_process_handle(void)
{
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gw;
    struct dhcp *dhcp_client;
    enet_info_t *enet_info = get_enet_info();

    switch(dhcp_state) {
    case DHCP_START:
        dhcp_start(&g_mynetif);

        dhcp_state = DHCP_WAIT_ADDRESS;
        if (enet_info != RT_NULL)
            enet_info->enet_status.enet_network_ok = 0;
        break;

    case DHCP_WAIT_ADDRESS:
        dhcp_client = netif_dhcp_data(&g_mynetif);
        /* read the new IP address */
        ipaddr.addr = g_mynetif.ip_addr.addr;
        netmask.addr = g_mynetif.netmask.addr;
        gw.addr = g_mynetif.gw.addr;

        rt_kprintf("dhcp: %s\r\n", ip4addr_ntoa(&dhcp_client->offered_ip_addr));

        if(0 != ipaddr.addr) {
            dhcp_state = DHCP_ADDRESS_ASSIGNED;
            dhcp_stop(&g_mynetif);    
            netif_set_addr(&g_mynetif, &ipaddr, &netmask, &gw);

            if (enet_info == RT_NULL) 
            {
                rt_kprintf("\r\nDHCP -- board ip : %d.%d.%d.%d \r\n", ip4_addr1_16(&ipaddr), \
                    ip4_addr2_16(&ipaddr), ip4_addr3_16(&ipaddr), ip4_addr4_16(&ipaddr));
                rt_kprintf("\r\nDHCP -- board mask : %d.%d.%d.%d \r\n", ip4_addr1_16(&netmask), \
                    ip4_addr2_16(&netmask), ip4_addr3_16(&netmask), ip4_addr4_16(&netmask));
                rt_kprintf("\r\nDHCP -- board gateway : %d.%d.%d.%d \r\n", ip4_addr1_16(&gw), \
                    ip4_addr2_16(&gw), ip4_addr3_16(&gw), ip4_addr4_16(&gw));
                rt_kprintf("get enet pointer fail\r\n");
            }
            else
            {
                rt_snprintf(enet_info->ip, 16, "%d.%d.%d.%d", ip4_addr1_16(&ipaddr), \
                    ip4_addr2_16(&ipaddr), ip4_addr3_16(&ipaddr), ip4_addr4_16(&ipaddr));
                rt_snprintf(enet_info->mask_sn, 16, "%d.%d.%d.%d", ip4_addr1_16(&netmask), \
                    ip4_addr2_16(&netmask), ip4_addr3_16(&netmask), ip4_addr4_16(&netmask));
                rt_snprintf(enet_info->gateway, 16, "%d.%d.%d.%d", ip4_addr1_16(&gw), \
                    ip4_addr2_16(&gw), ip4_addr3_16(&gw), ip4_addr4_16(&gw));

                enet_info->enet_status.enet_network_ok = 1;
            }
        } else {
            rt_kprintf("DHCP timeout %d\r\n", dhcp_client->tries);
            /* DHCP timeout */
            if(dhcp_client->tries > MAX_DHCP_TRIES) {
                dhcp_state = DHCP_TIMEOUT;
                /* stop DHCP */
                dhcp_stop(&g_mynetif);

                rt_kprintf("enet use default setting\r\n");
                /* static address used */
                IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
                IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
                IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
                netif_set_addr(&g_mynetif, &ipaddr, &netmask, &gw);

                if (enet_info != RT_NULL)
                {
                    rt_snprintf(enet_info->ip, 16, "%d.%d.%d.%d", IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
                    rt_snprintf(enet_info->mask_sn, 16, "%d.%d.%d.%d", NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
                    rt_snprintf(enet_info->gateway, 16, "%d.%d.%d.%d", GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
                    // enet_info->enet_status.enet_network_ok = 1;
                }
            }
        }
        if (enet_info != RT_NULL && enet_info->enet_status.enet_network_ok == 1)
        {
            rt_kprintf("\r\nDHCP -- board ip : %s \r\n", enet_info->ip);
            rt_kprintf("\r\nDHCP -- board mask : %s \r\n", enet_info->mask_sn);
            rt_kprintf("\r\nDHCP -- board gateway : %s \r\n", enet_info->gateway);
        }

        break;

    default:
        break;
    }
}
#endif /* USE_DHCP */

#if NO_SYS
/*!
    \brief      called when a frame is received
    \param[in]  none
    \param[out] none
    \retval     none
*/
void lwip_pkt_handle(void)
{
    /* read a received packet from the Ethernet buffers and send it to the lwIP for handling */
    ethernetif_input(&g_mynetif);
}

uint32_t sys_now(void)
{
  return rt_tick_get() * (1000 / RT_TICK_PER_SECOND);
}
#endif

