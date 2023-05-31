#ifndef __ENET_DRV_H__
#define __ENET_DRV_H__

#include "gd32f4xx.h"
#include "stdint.h"
#include "lwip/netif.h"
#include "gd32f4xx_enet.h"
#include <rtthread.h>

// #define USE_TRNG 1
#define USE_MD5_GENERIC_MAC                 1 // 如果取消定义，则打开宏MBEDTLS_ENTROPY_HARDWARE_ALT0

#define USE_DHCP       1 /* enable DHCP, if disabled static address is used */
// #define USE_EXTERNAL_OSC 1
#define USE_ENET_INTERRUPT
#define USE_HTTPD      1
#define USE_DNS        1
// #define USE_SNTP       1
// #define TIMEOUT_CHECK_USE_LWIP
/* MAC address: MAC_ADDR0:MAC_ADDR1:MAC_ADDR2:MAC_ADDR3:MAC_ADDR4:MAC_ADDR5 */
#define MAC_ADDR0   0x00
#define MAC_ADDR1   0x1A
#define MAC_ADDR2   0xC4
#define MAC_ADDR3   0x32
#define MAC_ADDR4   0x39
#define MAC_ADDR5   0x4E
 
/* static IP address: IP_ADDR0.IP_ADDR1.IP_ADDR2.IP_ADDR3 */
#define IP_ADDR0   192
#define IP_ADDR1   168
#define IP_ADDR2   1
#define IP_ADDR3   43

/* remote IP address: IP_S_ADDR0.IP_S_ADDR1.IP_S_ADDR2.IP_S_ADDR3 */
#define IP_S_ADDR0   192
#define IP_S_ADDR1   168
#define IP_S_ADDR2   1
#define IP_S_ADDR3   98

#define TCP_CLIENT_PORT 8087

/* net mask */
#define NETMASK_ADDR0   255
#define NETMASK_ADDR1   255
#define NETMASK_ADDR2   255
#define NETMASK_ADDR3   0

/* gateway address */
#define GW_ADDR0   192
#define GW_ADDR1   168
#define GW_ADDR2   1
#define GW_ADDR3   1

/* MII and RMII mode selection */
#define RMII_MODE  // user have to provide the 50 MHz clock by soldering a 50 MHz oscillator
// #define MII_MODE

/* clock the PHY from external 25MHz crystal (only for MII mode) */
#ifdef  MII_MODE
#define PHY_CLOCK_MCO
#endif

#if 0
#define USE_MD5_GENERIC_MAC                 1 // 如果取消定义，则打开宏MBEDTLS_ENTROPY_HARDWARE_ALT
#else 
#define USE_TRNG                            1
#endif

/* function declarations */
void netif_link_callback(struct netif *netif);
void netif_status_callback(struct netif *netif);

int set_mac_address(u8_t *hwaddress0, 
                    u8_t *hwaddress1, 
                    u8_t *hwaddress2, 
                    u8_t *hwaddress3, 
                    u8_t *hwaddress4, 
                    u8_t *hwaddress5);

int enet_start(void);

#endif /* MAIN_H */

