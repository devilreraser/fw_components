/* *****************************************************************************adapter_if
 * File:   drv_socket.h
 * Author: Dimitar Lilov
 *
 * Created on 2022 06 18
 * 
 * Description: ...
 * 
 **************************************************************************** */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */


/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include <sdkconfig.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_interface.h"
#include "drv_stream_if.h"
#include "lwip/sockets.h"

    
/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define DRV_SOCKET_DEFAULT_URL  "www.ivetell.com"
#define DRV_SOCKET_DEFAULT_IP   "84.40.115.3"
#define DRV_SOCKET_MAX_CLIENTS  CONFIG_SOCKET_SERVER_MAX_CLIENTS

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */
#if CONFIG_ESP_IF_WIFI_STA == 1
#define DRV_SOCKET_IF_DEFAULT   ESP_IF_WIFI_STA
#elif CONFIG_ESP_IF_WIFI_AP == 1
#define DRV_SOCKET_IF_DEFAULT   ESP_IF_WIFI_AP
#elif CONFIG_ESP_IF_ETH == 1
#define DRV_SOCKET_IF_DEFAULT   ESP_IF_ETH
#else
#define DRV_SOCKET_IF_DEFAULT   ESP_IF_WIFI_STA
#endif

#if CONFIG_BKP_IF_WIFI_STA == 1
#define DRV_SOCKET_IF_BACKUP   ESP_IF_WIFI_STA
#elif CONFIG_BKP_IF_WIFI_AP == 1
#define DRV_SOCKET_IF_BACKUP   ESP_IF_WIFI_AP
#elif CONFIG_BKP_IF_ETH == 1
#define DRV_SOCKET_IF_BACKUP   ESP_IF_ETH
#else
#define DRV_SOCKET_IF_BACKUP   ESP_IF_WIFI_STA
#endif


#if 0
used #include "lwip/sockets.h"
#define AF_UNSPEC       0
#define AF_INET         2
#if LWIP_IPV6
#define AF_INET6        10
#else /* LWIP_IPV6 */
#define AF_INET6        AF_UNSPEC
#endif /* LWIP_IPV6 */
#define PF_INET         AF_INET
#define PF_INET6        AF_INET6
#define PF_UNSPEC       AF_UNSPEC

#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#if LWIP_IPV6
#define IPPROTO_IPV6    41
#define IPPROTO_ICMPV6  58
#endif /* LWIP_IPV6 */
#define IPPROTO_UDPLITE 136
#define IPPROTO_RAW     255

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#endif

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */
typedef enum
{
    DRV_SOCKET_AF_UNSPEC = AF_UNSPEC,
    DRV_SOCKET_AF_INET = AF_INET,
    DRV_SOCKET_AF_INET6 = AF_INET6,
}drv_socket_address_family_t;

typedef enum
{
    DRV_SOCKET_PF_UNSPEC = PF_UNSPEC,
    DRV_SOCKET_PF_INET = PF_INET,
    DRV_SOCKET_PF_INET6 = PF_INET6,
}drv_socket_protocol_family_t;

typedef enum
{
    DRV_SOCKET_IPPROTO_IP = IPPROTO_IP,
    DRV_SOCKET_IPPROTO_ICMP = IPPROTO_ICMP,
    DRV_SOCKET_IPPROTO_TCP = IPPROTO_TCP,
    DRV_SOCKET_IPPROTO_UDP = IPPROTO_UDP,
#if LWIP_IPV6
    DRV_SOCKET_IPPROTO_IPV6 = IPPROTO_IPV6,
    DRV_SOCKET_IPPROTO_ICMPV6 = IPPROTO_ICMPV6,
#endif
    DRV_SOCKET_IPPROTO_UDPLITE = IPPROTO_UDPLITE,
    DRV_SOCKET_IPPROTO_RAW = IPPROTO_RAW,
}drv_socket_protocol_t;

typedef enum
{
    DRV_SOCKET_SOCK_STREAM = SOCK_STREAM,       /* TCP */
    DRV_SOCKET_SOCK_DGRAM = SOCK_DGRAM,         /* UDP */
    DRV_SOCKET_SOCK_RAW = SOCK_RAW,             /* RAW */
}drv_socket_protocol_type_t;

typedef enum
{
    DRV_SOCKET_ADAPTER_INTERFACE_DEFAULT,
    DRV_SOCKET_ADAPTER_INTERFACE_BACKUP,
}drv_socket_interface_t;

typedef enum
{
    DRV_SOCKET_PRIORITY_INTERFACE_DEFAULT,
    DRV_SOCKET_PRIORITY_INTERFACE_BACKUP,
}drv_socket_interface_priority_t;



/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */
typedef void (*drv_socket_on_connect_t)(int nConnectionIndex);
typedef int (*drv_socket_on_receive_t)(int nConnectionIndex, char* pData, int nMaxSize);
typedef void (*drv_socket_on_send_t)(int nConnectionIndex, char* pData, int nSize);
typedef void (*drv_socket_on_disconnect_t)(int nConnectionIndex);
typedef void (*drv_socket_on_recvfrom_t)(uint32_t,uint16_t);
typedef void (*drv_socket_on_sendto_t)(uint32_t*,uint16_t*);

typedef struct 
{
    char cAdapterInterfaceIP[16];
    char * pLastUsedHostIP;
    bool bBroadcastRxTx;
    struct sockaddr_storage adapterif_addr;   // Large enough for both IPv4 or IPv6
    struct sockaddr_storage host_addr_main; // Large enough for both IPv4 or IPv6
    struct sockaddr_storage host_addr_recv; // Large enough for both IPv4 or IPv6
    struct sockaddr_storage host_addr_send; // Large enough for both IPv4 or IPv6
    esp_interface_t adapter_if;             // the selected if

} drv_socket_runtime_t;


typedef struct
{

    int nSocketIndexPrimer[DRV_SOCKET_MAX_CLIENTS];
    int nSocketIndexServer;
    int nSocketConnectionsCount;
    bool bServerType;
    bool bActiveTask;
    bool bConnected;
    bool bSendEnable;               
    bool bSendFillEnable;               /* Used always to be able to fill send data to send stream (used outside of drv_socket.c) */
    bool bAutoSendEnable;
    bool bIndentifyForced;
    bool bIndentifyNeeded;
    bool bResetSendStreamOnConnect;
    bool bPingUse;
    bool bLineEndingFixCRLFToCR;
    bool bPermitBroadcast;
    bool bDisconnectRequest;
    bool bConnectDeny;
    bool bConnectDenyETH;
    bool bConnectDenySTA;
    bool bConnectDenyAP;
    bool bPriorityBackupAdapterInterface;
    bool bPreventOverflowReceivedData;
    #ifdef CONFIG_EXAMPLE_IPV6
    bool bIPV6;
    #endif

    size_t nPingTicks;
    size_t nPingCount;
    size_t nTimeoutSendEnable;

    int nTaskLoopCounter;


    char cName[8];
    char cHostIP[16];
    char cHostIPResolved[16];
    char cURL[32];
    uint16_t u16Port;
    esp_interface_t adapter_interface[2];

    drv_socket_address_family_t address_family;
    //drv_socket_protocol_family_t protocol_family;
    drv_socket_protocol_t protocol;
    drv_socket_protocol_type_t protocol_type;

    TaskHandle_t pTask;
    drv_stream_t * pSendStream[DRV_SOCKET_MAX_CLIENTS];
    drv_stream_t * pRecvStream[DRV_SOCKET_MAX_CLIENTS];
    drv_socket_on_connect_t onConnect;
    drv_socket_on_receive_t onReceive;
    drv_socket_on_send_t onSend;
    drv_socket_on_disconnect_t onDisconnect;
    drv_socket_on_recvfrom_t onReceiveFrom;
    drv_socket_on_sendto_t onSendTo;
    drv_socket_runtime_t* pRuntime;

} drv_socket_t;

/* *****************************************************************************
 * Function-Like Macro
 **************************************************************************** */

/* *****************************************************************************
 * Variables External Usage
 **************************************************************************** */ 

/* *****************************************************************************
 * Function Prototypes
 **************************************************************************** */
void drv_socket_list(void);
int drv_socket_get_position(const char* name);
drv_socket_t* drv_socket_get_handle(const char* name);
void drv_socket_disconnect(drv_socket_t* pSocket);
esp_err_t drv_socket_task(drv_socket_t* pSocket, int priority);
void drv_socket_init(void);



#ifdef __cplusplus
}
#endif /* __cplusplus */


