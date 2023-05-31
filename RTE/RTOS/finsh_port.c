/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 */

#include <rthw.h>
#include <rtconfig.h>
#include "gd32f4xx.h"

#ifndef RT_USING_FINSH
#error Please uncomment the line <#include "finsh_config.h"> in the rtconfig.h 
#endif

#ifdef RT_USING_FINSH

RT_WEAK char rt_hw_console_getchar(void)
{
    /* Note: the initial value of ch must < 0 */
    int ch = -1;
    
    if (RESET != usart_flag_get(USART0, USART_FLAG_RBNE))
    {
        ch = usart_data_receive(USART0);
    }
    else
    {
        rt_thread_mdelay(10);
    }
    return ch;
}

#endif /* RT_USING_FINSH */

