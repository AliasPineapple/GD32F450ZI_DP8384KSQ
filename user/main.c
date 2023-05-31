/*!
    \file    main.c
    \brief   enet demo

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

#include "gd32f4xx.h"
#include "netconf.h"
#include "enet_drv.h"
#include "netconn_tls.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"


static int start_tcp_test(void *arg);

void debug_com_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);
 
    /* configure the USART0 TX pin and USART0 RX pin */
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_9);
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_10);

    /* configure USART0 TX as alternate function push-pull */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);

    /* configure USART0 RX as alternate function push-pull */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_10);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_10);
    
    /* USART configure */
    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_hardware_flow_rts_config(USART0, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(USART0, USART_CTS_DISABLE);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);
}


#ifdef USE_TRNG
static ErrStatus trng_ready_check(void)
{
    uint32_t timeout = 0;
    FlagStatus trng_flag = RESET;
    ErrStatus reval = SUCCESS;

    /* check wherther the random data is valid */
    do
    {
        timeout++;
        trng_flag = trng_flag_get(TRNG_FLAG_DRDY);
    } while((RESET == trng_flag) &&(0xFFFF > timeout));

    if (RESET == trng_flag)
    {   
        /* ready check timeout */
        rt_kprintf("Error: TRNG can't ready \r\n");
        trng_flag = trng_flag_get(TRNG_FLAG_CECS);
        rt_kprintf("Clock error current status: %d \r\n", trng_flag);
        trng_flag = trng_flag_get(TRNG_FLAG_SECS);
        rt_kprintf("Seed error current status: %d \r\n", trng_flag);  
        reval = ERROR;
    }

    /* return check status */
    return reval;
}

int trng_init(void)
{
    ErrStatus reval = SUCCESS;

    /* TRNG module clock enable */
    rcu_periph_clock_enable(RCU_TRNG);

    /* TRNG registers reset */
    trng_deinit();
    trng_enable();

    return reval;
}
INIT_BOARD_EXPORT(trng_init);

int get_trang_data(uint32_t *random_data)
{
    if(SUCCESS == trng_ready_check())
    {
        *random_data = trng_get_true_random_data();
        return 0;
    }
    
    return -1;
}
#endif

/*!
    \brief      main function
    \param[in]  none
    \param[out] none
    \retval     none
*/
int main(void)
{
    int count = 0;
    enet_info_t *enet_info = get_enet_info();
    
    debug_com_init();

    enet_start();
    
    if (enet_info == RT_NULL)
    {
        return -1;
    }

    while (1)
    {
        if (enet_info->enet_status.enet_network_ok == 1 && \
            enet_info->enet_status.enet_finish_init == 1)
        {
            break;
        }
        count++;
        if (count > 30)
        {
            rt_kprintf("timeout!!!\r\n");
            return -1;
        }
        rt_thread_mdelay(1000);
    }

    start_tcp_test(RT_NULL);

    while (1)
    {
        enet_http_get("https://www.baidu.com:443");
        rt_thread_mdelay(100000);
    }
}

static void tcp_test0(void *param)
{
    int ret = 0;
    char ip[] = "192.168.1.98";
    int port = 8001;
    char send_data[] = "test----8001";
        
    ret = enet_tcp_connect(0, ip, port, 10);
    if (ret != RT_EOK)
    {
        rt_kprintf("connnect to %s:%d fail\r\n", ip, port);
        return;
    }

    while (1)
    {
        ret = enet_tcp_send_data(0, (uint8_t *)send_data, rt_strlen(send_data));
        if (ret != RT_EOK)
        {
            rt_kprintf("send data to %s:%d fail\r\n", ip, port);
        }
        rt_thread_mdelay(1000);
    }
}

static void tcp_test1(void *param)
{
    int ret = 0;
    char ip[] = "192.168.1.98";
    int port = 8002;
    char send_data[] = "test----8002";
        
    ret = enet_tcp_connect(1, ip, port, 10);
    if (ret != RT_EOK)
    {
        rt_kprintf("connnect to %s:%d fail\r\n", ip, port);
        return;
    }

    while (1)
    {
        ret = enet_tcp_send_data(1, (uint8_t *)send_data, rt_strlen(send_data));
        if (ret != RT_EOK)
        {
            rt_kprintf("send data to %s:%d fail\r\n", ip, port);
        }
        rt_thread_mdelay(1000);
    }
}

static int start_tcp_test(void *arg)
{
    rt_thread_t tcp0 = RT_NULL;
    rt_thread_t tcp1 = RT_NULL;

    tcp0 = rt_thread_create("tcp_0", tcp_test0, RT_NULL, 2048, 21, 25);
    if (tcp0 != RT_NULL)
    {
        rt_thread_startup(tcp0);
    }

    tcp1 = rt_thread_create("tcp_1", tcp_test1, RT_NULL, 2048, 21, 25);
    if (tcp1 != RT_NULL)
    {
        rt_thread_startup(tcp1);
    }
    return 0;
}

/* retarget the C library printf function to the USART */
// int fputc(int ch, FILE *f)
// {
//     usart_data_transmit(USART0, (uint8_t) ch);
//     while(RESET == usart_flag_get(USART0, USART_FLAG_TBE));
//     return ch;
// }
