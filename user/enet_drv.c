#include "enet_drv.h"
#include <rtthread.h>
#include "netconf.h"
#include "netconn_tls.h"
#include "lwip/sys.h"
#ifndef USE_TRNG
#ifdef USE_MD5_GENERIC_MAC
#include "mbedtls/md5.h"
#else
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy_poll.h"
#endif
#endif


#ifdef USE_ENET_INTERRUPT
/*!
    \brief      configures the nested vectored interrupt controller
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void nvic_configuration(void)
{
    // nvic_vector_table_set(NVIC_VECTTAB_FLASH, 0x0);
    nvic_priority_group_set(NVIC_PRIGROUP_PRE2_SUB2);
    nvic_irq_enable(ENET_IRQn, 0, 0);
}
#endif /* USE_ENET_INTERRUPT */

/*!
    \brief      configures the different GPIO ports
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void enet_gpio_config(void)
{
    rcu_periph_clock_enable(RCU_SYSCFG);

#ifndef USE_EXTERNAL_OSC
    // 备份设计，如果PHY没有外接晶振，则使用MCU的CK_OUT输出PHY需要的时钟
    gpio_af_set(GPIOA, GPIO_AF_0, GPIO_PIN_8);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_8);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_8);

    /* choose DIV2 to get 50MHz from 200MHz on CKOUT0 pin (PA8) to clock the PHY */
    rcu_ckout0_config(RCU_CKOUT0SRC_PLLP, RCU_CKOUT0_DIV4);
#endif
    syscfg_enet_phy_interface_config(SYSCFG_ENET_PHY_RMII);

    /* PA1: ETH_RMII_REF_CLK */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_1);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_1);

    /* PA2: ETH_MDIO */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_2);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_2);

    /* PA7: ETH_RMII_CRS_DV */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_7);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_7);

    gpio_af_set(GPIOA, GPIO_AF_11, GPIO_PIN_1);
    gpio_af_set(GPIOA, GPIO_AF_11, GPIO_PIN_2);
    gpio_af_set(GPIOA, GPIO_AF_11, GPIO_PIN_7);

    /* PB11: ETH_RMII_TX_EN */
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_11);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_11);

    /* PB12: ETH_RMII_TXD0 */
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_12);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_12);

    /* PB13: ETH_RMII_TXD1 */
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_13);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_13);

    gpio_af_set(GPIOB, GPIO_AF_11, GPIO_PIN_11);
    gpio_af_set(GPIOB, GPIO_AF_11, GPIO_PIN_12);
    gpio_af_set(GPIOB, GPIO_AF_11, GPIO_PIN_13);

    /* PC1: ETH_MDC */
    gpio_mode_set(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_1);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_1);

    /* PC4: ETH_RMII_RXD0 */
    gpio_mode_set(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_4);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_4);

    /* PC5: ETH_RMII_RXD1 */
    gpio_mode_set(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_5);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_5);

    gpio_af_set(GPIOC, GPIO_AF_11, GPIO_PIN_1);
    gpio_af_set(GPIOC, GPIO_AF_11, GPIO_PIN_4);
    gpio_af_set(GPIOC, GPIO_AF_11, GPIO_PIN_5);
}

/*!
    \brief      configures the ethernet interface
    \param[in]  none
    \param[out] none
    \retval     ErrStatus
*/
static ErrStatus enet_mac_dma_config(void)
{
    ErrStatus reval_state = ERROR;

    /* enable ethernet clock  */
    rcu_periph_clock_enable(RCU_ENET);
    rcu_periph_clock_enable(RCU_ENETTX);
    rcu_periph_clock_enable(RCU_ENETRX);

    /* reset ethernet on AHB bus */
    enet_deinit();

    reval_state = enet_software_reset();
    if(ERROR == reval_state) {
       rt_kprintf("init enet mac DMA fail\r\n");
       return reval_state;
    }

    /* configure the parameters which are usually less cared for enet initialization */
    enet_initpara_config(HALFDUPLEX_OPTION, ENET_CARRIERSENSE_ENABLE|ENET_RECEIVEOWN_ENABLE|ENET_RETRYTRANSMISSION_DISABLE|ENET_BACKOFFLIMIT_10|ENET_DEFERRALCHECK_DISABLE);
    enet_initpara_config(DMA_OPTION, ENET_FLUSH_RXFRAME_ENABLE|ENET_SECONDFRAME_OPT_ENABLE|ENET_NORMAL_DESCRIPTOR);

#ifdef CHECKSUM_BY_HARDWARE
    reval_state = enet_init(ENET_AUTO_NEGOTIATION, ENET_AUTOCHECKSUM_DROP_FAILFRAMES, ENET_BROADCAST_FRAMES_PASS);
#else
    reval_state = enet_init(ENET_AUTO_NEGOTIATION, ENET_NO_AUTOCHECKSUM, ENET_BROADCAST_FRAMES_PASS);
#endif /* CHECKSUM_BY_HARDWARE */
    return reval_state;
}

#ifdef USE_ENET_INTERRUPT
/*!
    \brief      this function handles ethernet interrupt request
    \param[in]  none
    \param[out] none
    \retval     none
*/
extern rt_sem_t g_rx_semaphore;
void ENET_IRQHandler(void)
{    
    rt_enter_critical();
    /* clear the enet DMA Rx interrupt pending bits */
    enet_interrupt_flag_clear(ENET_DMA_INT_FLAG_RS_CLR);
    enet_interrupt_flag_clear(ENET_DMA_INT_FLAG_NI_CLR);
    rt_exit_critical();
    rt_sem_release(g_rx_semaphore);
}
#endif /* USE_ENET_INTERRUPT */

ErrStatus check_phy_link_state(void)
{
    uint16_t phy_value = 0U;
    uint32_t timeout = 0;
    
    do {
        enet_phy_write_read(ENET_PHY_READ, PHY_ADDRESS, PHY_REG_BSR, &phy_value);
        phy_value &= PHY_LINKED_STATUS;
        timeout++;
    } while((RESET == phy_value) && (timeout < PHY_READ_TO));
    /* return ERROR due to timeout */
    if(PHY_READ_TO == timeout) {
        return ERROR;
    }
    return SUCCESS;
}

#ifndef USE_TRNG
#ifndef USE_MD5_GENERIC_MAC
int mbedtls_hardware_poll( void *Data, unsigned char *Output, size_t Len, size_t *oLen )
{
    uint32_t index;

    uint32_t uid[3];

    uid[0] = *(__I uint32_t *)(0x1FFF7A10 + 0x0);
    uid[1] = *(__I uint32_t *)(0x1FFF7A10 + 0x4);
    uid[2] = *(__I uint32_t *)(0x1FFF7A10 + 0x8);

    for (index = 0; index < Len/4; index++)
    {
        *oLen += 4;
        rt_memset(&(Output[index * 4]), (int)uid[index % 3], 4);
    }
  return 0;
}
#else
int mbedtls_md5_encode_string(const unsigned char *input_string, int input_len, unsigned char *output_md5) 
{
    mbedtls_md5_context md5_ctx;

    mbedtls_md5_init(&md5_ctx);
    mbedtls_md5_starts(&md5_ctx);
    mbedtls_md5_update(&md5_ctx, input_string, input_len);
    mbedtls_md5_finish(&md5_ctx, output_md5);
    if (output_md5[0] == '0')
        return -1;
    return 0;
}
#endif
#endif
// set MAC hardware address
int set_mac_address(u8_t *hwaddress0, 
                    u8_t *hwaddress1, 
                    u8_t *hwaddress2, 
                    u8_t *hwaddress3, 
                    u8_t *hwaddress4, 
                    u8_t *hwaddress5)
{
    uint32_t uid[3];
    int ret = -1;

    uid[0] = *(__I uint32_t *)(0x1FFF7A10 + 0x0);
    uid[1] = *(__I uint32_t *)(0x1FFF7A10 + 0x4);
    uid[2] = *(__I uint32_t *)(0x1FFF7A10 + 0x8);
    rt_kprintf("read mcu uid: %08X-%08X-%08X\r\n", uid[2], uid[1], uid[0]);

    *hwaddress0 = MAC_ADDR0;
    *hwaddress1 = MAC_ADDR1;
    *hwaddress2 = MAC_ADDR2;

#ifdef USE_TRNG
    uint32_t rand_num;
    extern int get_trang_data(uint32_t *random_data);


    if (get_trang_data(&rand_num) == 0)
    {
        *hwaddress3 = (rand_num & 0xFF); 
        *hwaddress4 = (rand_num & 0xFF00) >> 8;
        *hwaddress5 = (rand_num & 0xFF0000) >> 16;
        ret = 0;
    }
#else
    char personal_string[30] = {0};
    uint8_t data_buf[20] = {0}; // md5 len = 16
    int len;

    len = rt_snprintf(personal_string, 30, "%x%x%x", uid[2], uid[1], uid[0]);
    rt_kprintf("personal string len = %d: %s\r\n", len, personal_string);

#ifndef USE_MD5_GENERIC_MAC
    mbedtls_entropy_context *entropy = RT_NULL;
    mbedtls_ctr_drbg_context *ctr_drbg = RT_NULL;

    entropy = (mbedtls_entropy_context *)rt_malloc(sizeof(mbedtls_entropy_context));
    ctr_drbg = (mbedtls_ctr_drbg_context *)rt_malloc(sizeof(mbedtls_ctr_drbg_context));

    if (entropy == RT_NULL || ctr_drbg == RT_NULL)
    {
        ret = -1;
        rt_kprintf("set_mac_address malloc failed\r\n");
    }
    else
    {
        mbedtls_ctr_drbg_init(ctr_drbg);
        mbedtls_entropy_init(entropy);

        if((mbedtls_ctr_drbg_seed(ctr_drbg, mbedtls_entropy_func, entropy,
                               (const unsigned char *)personal_string, len)) == 0) 
        {
            if (( mbedtls_ctr_drbg_random(ctr_drbg, data_buf, 3)) == 0) 
            {
                *hwaddress3 = data_buf[0];
                *hwaddress4 = data_buf[1];
                *hwaddress5 = data_buf[2];
                ret = 0;
            }
            else
            {
                rt_kprintf( "mbedtls_ctr_drbg_random return failed %d\n", ret );
            }
        }
        else
        {
            rt_kprintf("mbedtls_ctr_drbg_seed returned %d\n", ret);
        }
        mbedtls_ctr_drbg_free(ctr_drbg);
        mbedtls_entropy_free(entropy);
        rt_free(entropy);
        rt_free(ctr_drbg);
    }
#else
    if (mbedtls_md5_encode_string((const unsigned char *)personal_string, len, data_buf) == 0)
    {
        *hwaddress3 = data_buf[0];
        *hwaddress4 = data_buf[1];
        *hwaddress5 = data_buf[2];
        ret = 0;
    }
#endif
#endif

    if (ret == -1)
    {
        *hwaddress3 = (uid[0] & 0xFF); 
        *hwaddress4 = (uid[0] & 0xFF00) >> 8;
        *hwaddress5 = (uid[0] & 0xFF0000) >> 16;
    }
    rt_kprintf("set mac: %02X:%02X:%02X:%02X:%02X:%02X\r\n", \
                *hwaddress0, *hwaddress1, *hwaddress2, *hwaddress3, *hwaddress4, *hwaddress5);

    enet_info_t *enet_info = get_enet_info();
    if (enet_info != RT_NULL) 
    {
        rt_snprintf(enet_info->mac, 32, "%02X:%02X:%02X:%02X:%02X:%02X", \
                *hwaddress0, *hwaddress1, *hwaddress2, *hwaddress3, *hwaddress4, *hwaddress5);
    }
    return 0;
}

void enet_task(void *param)
{
    uint32_t time_count = 0;
    ErrStatus reval_state = ERROR;

    enet_info_t *enet_info = get_enet_info();

    if (enet_info == RT_NULL) 
    {
        rt_kprintf("enet_task get enet pointer fail, exit enet task!!!\r\n");
        return;
    }

#ifdef USE_ENET_INTERRUPT
    nvic_configuration();
#endif /* USE_ENET_INTERRUPT */

    /* configure the GPIO ports for ethernet pins */
    enet_gpio_config();

    /* configure the ethernet MAC/DMA */
    reval_state = enet_mac_dma_config();
    if(ERROR == reval_state)
    {
       rt_memset(&(enet_info->enet_status), 0, sizeof(struct enet_status_s));
       rt_kprintf("init mac timeout!!! exit enet task!!!\r\n");
       return; 
    }

    enet_info->enet_status.enet_finish_init = 1;

#ifdef USE_ENET_INTERRUPT
    enet_interrupt_enable(ENET_DMA_INT_NIE);
    enet_interrupt_enable(ENET_DMA_INT_RIE);
#endif /* USE_ENET_INTERRUPT */

#ifdef SELECT_DESCRIPTORS_ENHANCED_MODE
    enet_desc_select_enhanced_mode();
#endif /* SELECT_DESCRIPTORS_ENHANCED_MODE */

    lwip_stack_init();
 
    while(1) 
    {
#if NO_SYS
#ifndef USE_ENET_INTERRUPT
        /* check if any packet received */
        if(enet_rxframe_size_get()) {
            /* process received ethernet packet */
            lwip_pkt_handle();
        }
#endif /* USE_ENET_INTERRUPT */
#endif
        time_count = sys_now();
        /* handle periodic timers for LwIP */
        lwip_periodic_handle(time_count);
        rt_thread_mdelay(10);
    }
}

int enet_start(void)
{
    rt_thread_t enet_tid = RT_NULL;
    
    enet_tid = rt_thread_create("enet_task", enet_task, RT_NULL, 4096, 21, 20);
    if (enet_tid != RT_NULL)
    {
        rt_thread_startup(enet_tid);
    }
    else
    {
        return -1;
    }

    return 0;
}
/*--------------------------------------------------------------------------*/

#if (LWIP_DEBUG == 1)
int fputc(int ch, FILE *f)
{
    usart_data_transmit(USART0, (uint8_t) ch);
    while(RESET == usart_flag_get(USART0, USART_FLAG_TBE));
    return ch;
}
#endif


void netif_link_callback(struct netif *netif)
{
    rt_kprintf("net link status changed: flag == %d\r\n", netif->flags);
}

void netif_status_callback(struct netif *netif)
{
    rt_kprintf("net status changed flag == %d \r\n", netif->flags);
}


