/**
 * @file
 * Ethernet Interface for standalone applications (without RTOS)
 *
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"
#include "netif/etharp.h"
#include "lwip/err.h"
#include "ethernetif.h"
#include "enet_drv.h"
#include "gd32f4xx_enet.h"

#include <string.h>
#include <rtthread.h>


#define ETHERNETIF_INPUT_TASK_STACK_SIZE          (1024)
#define ETHERNETIF_INPUT_TASK_PRIO                (10)
#define ETHERNETIF_INPUT_TASK_TICK                (20)
#define LOWLEVEL_OUTPUT_WAITING_TIME              ((rt_int32_t )250)
/* The time to block waiting for input */
// #define LOWLEVEL_INPUT_WAITING_TIME               ((rt_int32_t )100)
#define LOWLEVEL_INPUT_WAITING_TIME               RT_WAITING_FOREVER


/* network interface name */
#define IFNAME0 'M'
#define IFNAME1 'H'

/* ENET RxDMA/TxDMA descriptor */
extern enet_descriptors_struct  rxdesc_tab[ENET_RXBUF_NUM], txdesc_tab[ENET_TXBUF_NUM];

/* ENET receive buffer  */
extern uint8_t rx_buff[ENET_RXBUF_NUM][ENET_RXBUF_SIZE]; 

/* ENET transmit buffer */
extern uint8_t tx_buff[ENET_TXBUF_NUM][ENET_TXBUF_SIZE]; 

/*global transmit and receive descriptors pointers */
extern enet_descriptors_struct  *dma_current_txdesc;
extern enet_descriptors_struct  *dma_current_rxdesc;

/* preserve another ENET RxDMA/TxDMA ptp descriptor for normal mode */
enet_descriptors_struct  ptp_txstructure[ENET_TXBUF_NUM];
enet_descriptors_struct  ptp_rxstructure[ENET_RXBUF_NUM];

static struct netif *low_netif = RT_NULL;
rt_sem_t g_rx_semaphore = RT_NULL;

void ethernetif_input(void * pvParameters);

/**
 * In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 *
 * @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
static void low_level_init(struct netif *netif)
{
#ifdef CHECKSUM_BY_HARDWARE
    uint32_t i; 
#endif /* CHECKSUM_BY_HARDWARE */
    /* set MAC hardware address length */
    netif->hwaddr_len = ETHARP_HWADDR_LEN;

    /* set MAC hardware address */
    set_mac_address(&(netif->hwaddr[0]),
                    &(netif->hwaddr[1]),
                    &(netif->hwaddr[2]),
                    &(netif->hwaddr[3]),
                    &(netif->hwaddr[4]),
                    &(netif->hwaddr[5]));
    
    /* set netif maximum transfer unit */
    netif->mtu = 1500;

    /* accept broadcast address and ARP traffic */
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    low_netif = netif;

    /* create binary semaphore used for informing ethernetif of frame reception */
    if (g_rx_semaphore == RT_NULL){
        g_rx_semaphore = rt_sem_create("lwip_input_semt", 0, RT_IPC_FLAG_FIFO);
        LWIP_ASSERT("g_rx_semaphore != RT_NULL", (g_rx_semaphore != RT_NULL));
        rt_sem_trytake(g_rx_semaphore);
    }

    /* initialize MAC address in ethernet MAC */ 
    enet_mac_address_set(ENET_MAC_ADDRESS0, netif->hwaddr);

    /* initialize descriptors list: chain/ring mode */
#ifdef SELECT_DESCRIPTORS_ENHANCED_MODE
    enet_ptp_enhanced_descriptors_chain_init(ENET_DMA_TX);
    enet_ptp_enhanced_descriptors_chain_init(ENET_DMA_RX);
#else

    enet_descriptors_chain_init(ENET_DMA_TX);
    enet_descriptors_chain_init(ENET_DMA_RX);
    
//    enet_descriptors_ring_init(ENET_DMA_TX);
//    enet_descriptors_ring_init(ENET_DMA_RX);

#endif /* SELECT_DESCRIPTORS_ENHANCED_MODE */

    /* enable ethernet Rx interrrupt */
    {   int i;
        for(i=0; i<ENET_RXBUF_NUM; i++){ 
           enet_rx_desc_immediate_receive_complete_interrupt(&rxdesc_tab[i]);
        }
    }

#ifdef CHECKSUM_BY_HARDWARE
    /* enable the TCP, UDP and ICMP checksum insertion for the Tx frames */
    for(i=0; i < ENET_TXBUF_NUM; i++){
        enet_transmit_checksum_config(&txdesc_tab[i], ENET_CHECKSUM_TCPUDPICMP_FULL);
    }
#endif /* CHECKSUM_BY_HARDWARE */

    /* create the task that handles the ETH_MAC */
    rt_thread_t  tid = rt_thread_create("ETHERNETIF_INPUT", 
                                        ethernetif_input, 
                                        (void *)low_netif, 
                                        ETHERNETIF_INPUT_TASK_STACK_SIZE, 
                                        ETHERNETIF_INPUT_TASK_PRIO, 
                                        ETHERNETIF_INPUT_TASK_TICK);

    LWIP_ASSERT("tid != RT_NULL", (tid != RT_NULL));

    rt_thread_startup(tid);

    /* note: TCP, UDP, ICMP checksum checking for received frame are enabled in DMA config */

    /* enable MAC and DMA transmission and reception */
    enet_enable();
}

/**
 * This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *         an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become availale since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    static rt_sem_t s_tx_semaphore = RT_NULL;
    struct pbuf *q;
    int framelength = 0;
    uint8_t *buffer;
    ErrStatus reval = ERROR;

    SYS_ARCH_DECL_PROTECT(sr);

    if (s_tx_semaphore == RT_NULL){
        s_tx_semaphore = rt_sem_create("s_tx_sem", 1, RT_IPC_FLAG_FIFO);
    }

    if (rt_sem_take(s_tx_semaphore, LOWLEVEL_OUTPUT_WAITING_TIME) == RT_EOK)
    {
        SYS_ARCH_PROTECT(sr);

        while((uint32_t)RESET != (dma_current_txdesc->status & ENET_TDES0_DAV)){
        }  
        buffer = (uint8_t *)(enet_desc_information_get(dma_current_txdesc, TXDESC_BUFFER_1_ADDR));
    
        /* copy frame from pbufs to driver buffers */
        for(q = p; q != RT_NULL; q = q->next){ 
            rt_memcpy((uint8_t *)&buffer[framelength], q->payload, q->len);
            framelength = framelength + q->len;
        }
    
        /* note: padding and CRC for transmitted frame 
        are automatically inserted by DMA */

        /* transmit descriptors to give to DMA */ 
#ifdef SELECT_DESCRIPTORS_ENHANCED_MODE
        reval= ENET_NOCOPY_PTPFRAME_TRANSMIT_ENHANCED_MODE(framelength, RT_NULL);
#else
    
        reval = ENET_NOCOPY_FRAME_TRANSMIT(framelength);
#endif /* SELECT_DESCRIPTORS_ENHANCED_MODE */

        SYS_ARCH_UNPROTECT(sr);

        rt_sem_release(s_tx_semaphore);
    }

    if(SUCCESS == reval){
        return ERR_OK;
    }else{
        return ERR_TIMEOUT;
    }
}

/**
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return a pbuf filled with the received packet (including MAC header)
 *         RT_NULL on memory error
 */
static struct pbuf * low_level_input(struct netif *netif)
{
    struct pbuf *p = RT_NULL, *q = RT_NULL;
    uint32_t l =0;
    u16_t len;
    uint8_t *buffer;

    /* obtain the size of the packet and put it into the "len" variable. */
    len = enet_desc_information_get(dma_current_rxdesc, RXDESC_FRAME_LENGTH);
    buffer = (uint8_t *)(enet_desc_information_get(dma_current_rxdesc, RXDESC_BUFFER_1_ADDR));
    
    if (len > 0) {
        /* we allocate a pbuf chain of pbufs from the Lwip buffer pool */
        p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    }
    
    /* copy received frame to pbuf chain */
    if (p != RT_NULL){
        for (q = p; q != RT_NULL; q = q->next){ 
            rt_memcpy((uint8_t *)q->payload, (u8_t*)&buffer[l], q->len);
            l = l + q->len;
        }    
    }
  
#ifdef SELECT_DESCRIPTORS_ENHANCED_MODE
    ENET_NOCOPY_PTPFRAME_RECEIVE_ENHANCED_MODE(RT_NULL);
  
#else
    
    ENET_NOCOPY_FRAME_RECEIVE();
#endif /* SELECT_DESCRIPTORS_ENHANCED_MODE */

    return p;
}

/**
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface. Then the type of the received packet is determined and
 * the appropriate input function is called.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
#if 1
void ethernetif_input(void * pvParameters)
{
    struct pbuf *p;
    struct netif *netif = (struct netif *)pvParameters;
    
    SYS_ARCH_DECL_PROTECT(sr);

    while(1)
    {
        if (RT_EOK == rt_sem_take(g_rx_semaphore, LOWLEVEL_INPUT_WAITING_TIME))
        {
            SYS_ARCH_PROTECT(sr);
            /* move received packet into a new pbuf */
            p = low_level_input(netif);
            SYS_ARCH_UNPROTECT(sr);
          
            if (p != RT_NULL){
                /* entry point to the LwIP stack */
                SYS_ARCH_PROTECT(sr);
                if (ERR_OK != netif->input(p, netif)){
                    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
                    pbuf_free(p);
                    p = RT_NULL;
                }
                SYS_ARCH_UNPROTECT(sr);
            }
        }
    }
}
#else
err_t ethernetif_input(struct netif *netif)
{
    err_t err;
    struct pbuf *p;

    SYS_ARCH_DECL_PROTECT(sr);
    
    SYS_ARCH_PROTECT(sr);
    /* move received packet into a new pbuf */
    p = low_level_input(netif);
    SYS_ARCH_UNPROTECT(sr);

    /* no packet could be read, silently ignore this */
    if (p == NULL) return ERR_MEM;

    /* entry point to the LwIP stack */
    SYS_ARCH_PROTECT(sr);
    err = netif->input(p, netif);
    SYS_ARCH_UNPROTECT(sr);
    
    if (err != ERR_OK){
        LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
        pbuf_free(p);
        p = NULL;
    }
    return err;
}
#endif

/**
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t ethernetif_init(struct netif *netif)
{
    LWIP_ASSERT("netif != RT_NULL", (netif != RT_NULL));
  
#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

    netif->name[0] = IFNAME0;
    netif->name[1] = IFNAME1;
 
#if LWIP_IPV4
#if LWIP_ARP || LWIP_ETHERNET
#if LWIP_ARP
    netif->output = etharp_output;
#else
    netif->output = low_level_output_arp_off;
#endif /* LWIP_ARP */
#endif /* LWIP_ARP || LWIP_ETHERNET */
#endif /* LWIP_IPV4 */

netif->linkoutput = low_level_output;

#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */

    /* initialize the hardware */
    low_level_init(netif);

    return ERR_OK;
}



