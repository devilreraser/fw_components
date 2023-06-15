/* *****************************************************************************
 * File:   drv_socket.c
 * Author: Dimitar Lilov
 *
 * Created on 2022 06 18
 * 
 * Description: ...
 * 
 **************************************************************************** */

/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include "drv_socket.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
//#include "lwip/dns.h"

#include "esp_netif.h"
#include "esp_interface.h"
#include "esp_wifi.h"
#include "esp_mac.h"

//#include "drv_system_if.h"
#include "drv_eth_if.h"
#include "drv_wifi_if.h"
#include "drv_version_if.h"
#include "drv_dns_if.h"
//#include "drv_console_if.h"

/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "drv_socket"

#define DEF_PORT                        CONFIG_SOCKET_DEFAULT_PORT
#define DEF_KEEPALIVE_IDLE              CONFIG_SOCKET_DEFAULT_KEEPALIVE_IDLE
#define DEF_KEEPALIVE_INTERVAL          CONFIG_SOCKET_DEFAULT_KEEPALIVE_INTERVAL
#define DEF_KEEPALIVE_COUNT             CONFIG_SOCKET_DEFAULT_KEEPALIVE_COUNT

#define DRV_SOCKET_TASK_REST_TIME_MS    10
#define DRV_SOCKET_PING_SEND_TIME_MS    10000
#define DRV_SOCKET_RECONNECT_TIME_MS    5000

#define DRV_SOCKET_COUNT_MAX            10

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Function-Like Macros
 **************************************************************************** */

/* *****************************************************************************
 * Variables Definitions
 **************************************************************************** */
drv_socket_t * pSocketList[DRV_SOCKET_COUNT_MAX] = {NULL};
int nSocketListCount = 0;
int nSocketCountTotal = 0;

TickType_t nReconnectTimeTicks = pdMS_TO_TICKS(DRV_SOCKET_RECONNECT_TIME_MS);
TickType_t nTaskRestTimeTicks = pdMS_TO_TICKS(DRV_SOCKET_TASK_REST_TIME_MS);

uint8_t last_mac_addr_on_identification_request[6] = {0};

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */
void socket_set_options(drv_socket_t* pSocket, int nConnectionIndex);
void socket_on_connect(drv_socket_t* pSocket, int nConnectionIndex);

/* *****************************************************************************
 * Functions
 **************************************************************************** */
void drv_socket_list(void)
{
    ESP_LOGI(TAG, "Sockets in list %d. Sockets Total %d.", nSocketListCount, nSocketCountTotal);
    for (int index = 0; index < nSocketListCount; index++)
    {
        if (pSocketList[index] != NULL)
        {
            ESP_LOGI(TAG, "Success Socket[%d] Name:%16s|Port:%5d|Loop:%6d", 
                index, pSocketList[index]->cName, pSocketList[index]->u16Port, pSocketList[index]->nTaskLoopCounter);
        }
        else
        {
            ESP_LOGE(TAG, "Failure Socket[%d] Null", index);
        }
    }
}

int drv_socket_get_position(const char* name)
{
    int result = -1;
    for (int index = 0; index < nSocketListCount; index++)
    {
        if (pSocketList[index] != NULL)
        {
            if(strcmp(pSocketList[index]->cName, name) == 0)
            {
                result = index;
                break;
            }
        }
    }
    return result;
}

drv_socket_t* drv_socket_get_handle(const char* name)
{
    drv_socket_t* result = NULL;
    for (int index = 0; index < nSocketListCount; index++)
    {
        if (pSocketList[index] != NULL)
        {
            if(strcmp(pSocketList[index]->cName, name) == 0)
            {
                result = pSocketList[index];
                break;
            }
        }
    }
    return result;
}

void socket_connection_remove_from_list(drv_socket_t* pSocket, int nConnectionIndex)
{
    for (int nIndex = nConnectionIndex + 1 ; nIndex < pSocket->nSocketConnectionsCount; nIndex++)
    {
        pSocket->nSocketIndexPrimer[nIndex - 1] = pSocket->nSocketIndexPrimer[nIndex];
    }
    pSocket->nSocketConnectionsCount--;
}

void socket_connection_add_to_list(drv_socket_t* pSocket, int nSocketIndex)
{
    if (pSocket->nSocketConnectionsCount < DRV_SOCKET_MAX_CLIENTS)
    {
        pSocket->nSocketIndexPrimer[pSocket->nSocketConnectionsCount] = nSocketIndex;
        socket_set_options(pSocket, pSocket->nSocketConnectionsCount);
        socket_on_connect(pSocket, pSocket->nSocketConnectionsCount);
        pSocket->nSocketConnectionsCount++;
    }
    else
    {
        ESP_LOGE(TAG, "Connecting Failure (Max Clients Reached) client to socket %s %d", pSocket->cName, nSocketIndex);
    }
}


void socket_disconnect_connection(drv_socket_t* pSocket, int nConnectionIndex)
{
    int err;

    if (nConnectionIndex < pSocket->nSocketConnectionsCount)
    {
        ESP_LOGE(TAG, "Disconnecting client %d socket %s %d", nConnectionIndex, pSocket->cName, pSocket->nSocketIndexPrimer[nConnectionIndex]);
        if(shutdown(pSocket->nSocketIndexPrimer[nConnectionIndex], SHUT_RDWR) != 0)
        {
            err = errno;
            ESP_LOGE(TAG, "Error shutdown client %d socket %s %d: errno %d (%s)", nConnectionIndex, pSocket->cName, pSocket->nSocketIndexPrimer[nConnectionIndex], err, strerror(err));
        }
        if(close(pSocket->nSocketIndexPrimer[nConnectionIndex]) != 0)
        {
            err = errno;
            ESP_LOGE(TAG, "Error close client %d socket %s %d: errno %d (%s)", nConnectionIndex, pSocket->cName, pSocket->nSocketIndexPrimer[nConnectionIndex], err, strerror(err));     
        }
        pSocket->nSocketIndexPrimer[nConnectionIndex] = -1;
        socket_connection_remove_from_list(pSocket, nConnectionIndex);
    }
}


void socket_disconnect(drv_socket_t* pSocket)
{
    int err;

    if (pSocket->bServerType)
    {
        while (pSocket->nSocketConnectionsCount)
        {
            socket_disconnect_connection(pSocket, 0);   /* Start Removing From Socket Client Connection Index 0 */
        }
        if (pSocket->nSocketIndexServer >= 0)
        {
            ESP_LOGE(TAG, "Disconnecting server socket %s %d", pSocket->cName, pSocket->nSocketIndexServer);
            if(shutdown(pSocket->nSocketIndexServer, SHUT_RDWR) != 0)
            {
                err = errno;
                ESP_LOGE(TAG, "Error shutdown server socket %s %d: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, err, strerror(err));
            }
            if(close(pSocket->nSocketIndexServer) != 0)
            {
                err = errno;
                ESP_LOGE(TAG, "Error close server socket %s %d: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, err, strerror(err));     
            }
            pSocket->nSocketIndexServer = -1;
        }
    }
    else
    {
        while (pSocket->nSocketConnectionsCount)
        {
            socket_disconnect_connection(pSocket, 0);   /* Start Removing From Socket Client Connection Index 0 */
            if (pSocket->onDisconnect != NULL)
            {
                pSocket->onDisconnect(0);
            }
        }
        
        if (pSocket->nSocketIndexServer >= 0)
        {
            ESP_LOGE(TAG, "Disconnecting unused socket %s %d", pSocket->cName, pSocket->nSocketIndexServer);
            if(shutdown(pSocket->nSocketIndexServer, SHUT_RDWR) != 0)
            {
                err = errno;
                ESP_LOGE(TAG, "Error shutdown unused socket %s %d: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, err, strerror(err));
            }
            if(close(pSocket->nSocketIndexServer) != 0)
            {
                err = errno;
                ESP_LOGE(TAG, "Error close unused socket %s %d: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, err, strerror(err));     
            }
            pSocket->nSocketIndexServer = -1;
        }
    }


    pSocket->bConnected = false;
}

void drv_socket_disconnect(drv_socket_t* pSocket)
{
    pSocket->bDisconnectRequest = true;
}

void socket_if_get_mac(drv_socket_t* pSocket, uint8_t mac_addr[6])
{
    if (pSocket->pRuntime->adapter_if == ESP_IF_WIFI_STA)
    {
        esp_wifi_get_mac(ESP_IF_WIFI_STA, mac_addr);
        ESP_LOGI(pSocket->cName, "Adapter Interface: %s", "Wifi Station");   
    }
    else if (pSocket->pRuntime->adapter_if == ESP_IF_WIFI_AP)
    {
        esp_wifi_get_mac(ESP_IF_WIFI_AP, mac_addr);
        ESP_LOGI(pSocket->cName, "Adapter Interface: %s", "Wifi Soft-AP");   
    }
    else if (pSocket->pRuntime->adapter_if >= ESP_IF_ETH)
    {
        #if CONFIG_USE_ETHERNET
        int eth_index = pSocket->pRuntime->adapter_if - ESP_IF_ETH;
        if (drv_eth_get_netif_count() > eth_index)
        {
            drv_eth_get_mac(eth_index, mac_addr);
            ESP_LOGI(pSocket->cName, "Adapter Interface: %s", "EthernetLAN");    
        }
        else
        #endif
        {
            ESP_LOGE(pSocket->cName, "Adapter Interface: %s", "Unimplemented");    //to do fix for default if needed
        } 

    }

}

bool send_identification_answer(drv_socket_t* pSocket, int nConnectionIndex)
{   
    bool bResult = false;

    /* Get MAC */
    uint8_t mac_addr[6] = {0};
    socket_if_get_mac(pSocket, mac_addr);

    //ESP_LOGI(TAG, "!!!!!!!!!!!!!!!: %s", "?????????????");  

    #define MACSTR_U_r "%02X:%02X:%02X:%02X:%02X:%02X\r"
    ESP_LOGI(TAG, "MAC Address "MACSTR_U_r, MAC2STR(mac_addr));
    
    char cTemp[64];

    sprintf(cTemp, MACSTR_U_r "MAC:" MACSTR_U_r "Version:%d.%d.%05d\r", 
                MAC2STR(mac_addr), MAC2STR(mac_addr), 
                DRV_VERSION_MAJOR, DRV_VERSION_MINOR, DRV_VERSION_BUILD);

    int err;
    int nSocketClient = pSocket->nSocketIndexPrimer[nConnectionIndex];
    int nLength = strlen(cTemp);  
    int nLengthSent = send(nSocketClient, (uint8_t*)cTemp, nLength, 0);
    
    if (nLengthSent > 0)
    {
        if (nLengthSent != nLength)
        {
            ESP_LOGE(TAG, "Error during send id to socket %s[%d] %d: send %d/%d bytes", pSocket->cName, nConnectionIndex, nSocketClient, nLengthSent, nLength);
            //socket_disconnect(pSocket);
            socket_disconnect_connection(pSocket, nConnectionIndex);   /* Removing Socket Client Connection */
        }
        else
        {
            ESP_LOGI(TAG, "Success send id to socket %s[%d] %d: sent %d bytes", pSocket->cName, nConnectionIndex, nSocketClient, nLengthSent);
            bResult = true;
        }
    }
    else
    {
        err = errno;
        //if (err != EAGAIN)
        {
            ESP_LOGE(TAG, "Error during send id to socket %s[%d] %d: errno %d (%s)", pSocket->cName, nConnectionIndex, nSocketClient, err, strerror(err));
            //socket_disconnect(pSocket);
            socket_disconnect_connection(pSocket, nConnectionIndex);   /* Removing Socket Client Connection */
        }
    }

    return bResult;
}

bool socket_identification_answer(drv_socket_t* pSocket, int nConnectionIndex, char* pData, int size)
{
    bool bResult = false;

    if(memcmp(pData,"man mac", strlen("man mac")) == 0)
    {
        bResult = send_identification_answer(pSocket, nConnectionIndex);
        bResult = false; //indicate here that the identification answer is not complete, because after that version ask is expected
    }
    else
    if(memcmp(pData,"man ver", strlen("man ver")) == 0)
    {
        bResult = send_identification_answer(pSocket, nConnectionIndex);
    }
    else
    {
        ESP_LOGI(TAG, "stdio_auto_answer skip %d bytes", size);
        ESP_LOG_BUFFER_CHAR(TAG, pData, size);
    }

    return bResult;
}




void socket_recv(drv_socket_t* pSocket, int nConnectionIndex)
{
    int err;
    int nSocketClient;
    char sockTypeString[10];

    nSocketClient = pSocket->nSocketIndexPrimer[nConnectionIndex];
    if (pSocket->bServerType)
    {
        strcpy(sockTypeString, "client");
    }
    else
    {
        strcpy(sockTypeString, "");
    }
    #define MAX_TCP_READ_SIZE 2048

    int nLength = MAX_TCP_READ_SIZE;
    uint8_t* au8Temp;

    if (pSocket->bPreventOverflowReceivedData)
    {
        int nLengthPushSize = drv_stream_get_size(pSocket->pRecvStream[nConnectionIndex]);
        int nLengthPushFree = drv_stream_get_free(pSocket->pRecvStream[nConnectionIndex]);
        if (nLengthPushSize)
        {
            ESP_LOGW(TAG, "%s socket %s[%d] %d read buffer free %d bytes", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, nLengthPushFree);
        }
        

        if (nLengthPushFree >= 0)
        {
            if(nLength > nLengthPushFree)
            {
                if (nLengthPushSize)
                {
                    ESP_LOGW(TAG, "Limit Read from %s socket %s[%d] %d because of issuficient read buffer (%d/%d bytes)", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, nLengthPushFree, nLength);
                }
                nLength = nLengthPushFree;
            }
        }
        
    }

    if (nLength == 0)
    {
        ESP_LOGE(TAG, "Skip Read from %s socket %s[%d] %d because of full read buffer (%d bytes)", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, drv_stream_get_size(pSocket->pRecvStream[nConnectionIndex]));
        return;
    }
    
    au8Temp = malloc(nLength);

    if (au8Temp)
    {
        if (pSocket->pRuntime->bBroadcastRxTx)
        {
            socklen_t socklen = sizeof(pSocket->pRuntime->host_addr_recv);
            nLength = recvfrom(nSocketClient, au8Temp, nLength, MSG_PEEK | MSG_DONTWAIT, (struct sockaddr *)&pSocket->pRuntime->host_addr_recv, &socklen);

        }
        else
        {
            nLength = recv(nSocketClient, au8Temp, nLength, MSG_PEEK | MSG_DONTWAIT);
        }
        free(au8Temp);
        
        

        if (nLength > 0)
        {
            int nLengthPeek = nLength;

            ESP_LOGI(TAG, "01 %d bytes Peek on %s socket", nLengthPeek, pSocket->cName);

            au8Temp = malloc(nLength);

            if (au8Temp)
            {


                if (pSocket->pRuntime->bBroadcastRxTx)
                {
                    socklen_t socklen = sizeof(pSocket->pRuntime->host_addr_recv);
                    nLength = recvfrom(nSocketClient, au8Temp, nLength, MSG_DONTWAIT, (struct sockaddr *)&pSocket->pRuntime->host_addr_recv, &socklen);

                    #define IP2STR_4(u32addr) ((uint8_t*)(&u32addr))[0],((uint8_t*)(&u32addr))[1],((uint8_t*)(&u32addr))[2],((uint8_t*)(&u32addr))[3]
                    struct sockaddr_in *host_addr_recv_ip4 = (struct sockaddr_in *)&pSocket->pRuntime->host_addr_recv;
                    char* adapter_interface_address = inet_ntoa(host_addr_recv_ip4->sin_addr.s_addr);
                    ESP_LOGW(TAG, "Recv %s socket %s[%d] %d (host_addr_recv %s:%d)", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, adapter_interface_address, htons(host_addr_recv_ip4->sin_port));
                    //ESP_LOGW(TAG, "host_addr_recv " IPSTR ":%d", IP2STR_4(host_addr_recv_ip4->sin_addr.s_addr), htons(host_addr_recv_ip4->sin_port));
                    //ESP_LOGW(TAG, "host_addr_recv 0x%08X:%d", (int)(host_addr_recv_ip4->sin_addr.s_addr), htons(host_addr_recv_ip4->sin_port));

                    uint32_t u32RecvFromIP;
                    uint16_t u16RecvFromPort;

                    u32RecvFromIP = host_addr_recv_ip4->sin_addr.s_addr;
                    u16RecvFromPort = htons(host_addr_recv_ip4->sin_port);

                    if (pSocket->onReceiveFrom != NULL)
                    {
                        pSocket->onReceiveFrom(u32RecvFromIP, u16RecvFromPort);
                    }

                }
                else
                {
                    nLength = recv(nSocketClient, au8Temp, nLengthPeek, MSG_DONTWAIT);
                }
            
                

                if (nLength > 0)
                {
                    if (nLength == nLengthPeek)
                    {
                        ESP_LOG_BUFFER_CHAR(pSocket->cName, au8Temp, nLength);

                        if (pSocket->bIndentifyForced)
                        {
                            if(memcmp((char*)au8Temp,"man mac", strlen("man mac")) == 0)
                            {
                                socket_if_get_mac(pSocket, last_mac_addr_on_identification_request);
                                ESP_LOGI(TAG, "Last MAC On Identification Request %02X:%02X:%02X:%02X:%02X:%02X", MAC2STR(last_mac_addr_on_identification_request));
                                //drv_system_set_last_mac_identification_request(last_mac_addr_on_identification_request); To Do change to use this module instead drv_system
                            }
                        }

                        if (pSocket->bIndentifyNeeded)
                        {
                            //ESP_LOG_BUFFER_CHAR(TAG "!!!!!!!!!!!!!!!!001", au8Temp, nLength);
                            if (socket_identification_answer(pSocket, nConnectionIndex, (char*)au8Temp, nLength))
                            {
                                //ESP_LOG_BUFFER_CHAR(TAG "!!!!!!!!!!!!!!!!002", au8Temp, nLength);
                                pSocket->bIndentifyNeeded = false;
                                pSocket->bSendEnable = true;
                            }
                        }

                        if (pSocket->bLineEndingFixCRLFToCR)
                        {
                            for (int i = 0; i < nLength; i++)
                            {
                                if (i > 0)
                                {
                                    if((au8Temp[i-1] == '\r') && (au8Temp[i] == '\n'))
                                    {
                                        nLength--;
                                        for (int j = i; j < nLength; j++)
                                        {
                                            au8Temp[j] = au8Temp[j+1];
                                        }
                                    }
                                    else
                                    if((au8Temp[i-1] == '\n') && (au8Temp[i] == '\r'))
                                    {
                                        nLength--;
                                        for (int j = i; j < nLength; j++)
                                        {
                                            au8Temp[j] = au8Temp[j+1];
                                        }
                                    }
                                }
                                
                            }
                        }

                        if (pSocket->onReceive != NULL)
                        {
                            int nLengthAfterProcess = pSocket->onReceive(nConnectionIndex, (char*)au8Temp, nLength);

                            if (nLengthAfterProcess != nLength)
                            {
                                ESP_LOGI(TAG, "OnReceive event %s socket %s[%d] %d: returns %d/%d bytes", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, nLengthAfterProcess, nLength);
                                nLength = nLengthAfterProcess;
                            }
                        }
                        
                        int nLengthPush = drv_stream_push(pSocket->pRecvStream[nConnectionIndex], au8Temp, nLength);
                        int nFillStreamTCP = drv_stream_get_size(pSocket->pRecvStream[nConnectionIndex]);
                        if(nLengthPush != nLength)
                        {
                            ESP_LOGE(TAG, "Error during read from %s socket %s[%d] %d: push |%d/%d->%d|bytes", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, nLengthPush, nLength, nFillStreamTCP);
                            //socket_disconnect(pSocket);
                            socket_disconnect_connection(pSocket, nConnectionIndex);   /* Removing Socket Client Connection */
                        }
                        else
                        {
                            
                            ESP_LOGW(TAG, "%s socket %s[%d] %d: push |%d/%d->%d|bytes", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, nLengthPush, nLength, nFillStreamTCP);
                            //ESP_LOG_BUFFER_CHAR(TAG "03", au8Temp, nLength);
                        }
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Error during read from %s socket %s[%d] %d: peek/recv %d/%d bytes", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, nLengthPeek, nLength);
                        //socket_disconnect(pSocket);
                        socket_disconnect_connection(pSocket, nConnectionIndex);   /* Removing Socket Client Connection */
                    }
                }
                else
                {
                    err = errno;
                    ESP_LOGE(TAG, "Error during read data from %s socket %s[%d] %d: errno %d (%s)", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, err, strerror(err));
                    //socket_disconnect(pSocket);
                    socket_disconnect_connection(pSocket, nConnectionIndex);   /* Removing Socket Client Connection */
                }
                free(au8Temp);
            }
            else
            {
                ESP_LOGE(TAG, "Error during allocate %d bytes for read pull from %s socket %s[%d] %d", nLength, sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient);
            }
        }
        else
        {
            err = errno;
            if (err != EAGAIN)
            {
                ESP_LOGE(TAG, "Error during read peek from %s socket %s[%d] %d: errno %d (%s)", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, err, strerror(err));
                //socket_disconnect(pSocket);
                socket_disconnect_connection(pSocket, nConnectionIndex);   /* Removing Socket Client Connection */
            }

        }
    }
    else
    {
        ESP_LOGE(TAG, "Error during allocate %d bytes for read peek from %s socket %s[%d] %d", nLength, sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient);
    }
}

void socket_send(drv_socket_t* pSocket, int nConnectionIndex)
{
    int err;
    int nSocketClient;
    char sockTypeString[10];

    nSocketClient = pSocket->nSocketIndexPrimer[nConnectionIndex];
    if (pSocket->bServerType)
    {
        strcpy(sockTypeString, "client");
    }
    else
    {
        strcpy(sockTypeString, "");
    }
    #define MAX_TCP_SEND_SIZE 1024

    int nLength = MAX_TCP_SEND_SIZE;
    uint8_t* au8Temp;



    if (pSocket->bSendEnable)
    {
        au8Temp = malloc(nLength);

        if (au8Temp)
        {
            nLength = drv_stream_pull(pSocket->pSendStream[nConnectionIndex], au8Temp, nLength);

            if(pSocket->bPingUse)
            {
                if(nLength <= 0)
                {
                    pSocket->nPingTicks += pdMS_TO_TICKS(DRV_SOCKET_TASK_REST_TIME_MS);
                    if(pSocket->nPingTicks > pdMS_TO_TICKS(DRV_SOCKET_PING_SEND_TIME_MS))
                    {
                        pSocket->nPingTicks = 0;

                    
                        pSocket->nPingCount++;
                        sprintf((char*)au8Temp, "ping_count %d \r\n", pSocket->nPingCount);
                        nLength = strlen((char*)au8Temp);
                    }
                }
                else
                {
                    pSocket->nPingTicks = 0;
                }
            }

            if(nLength > 0)
            {
                int nLengthSent;
                if (pSocket->pRuntime->bBroadcastRxTx)
                {

                    bool bUseSendToIPPort = false;

                    uint32_t u32SendToIP = 0xFFFFFFFF;
                    uint16_t u16SendToPort = 0xFFFF;

                    if (pSocket->onSendTo != NULL)
                    {
                        pSocket->onSendTo(&u32SendToIP, &u16SendToPort);
                        u32SendToIP = htonl(u32SendToIP);
                        if (u16SendToPort != 0) bUseSendToIPPort = true;
                    }

                    if (bUseSendToIPPort)
                    {
                        struct sockaddr_in *host_addr_send_ip4 = (struct sockaddr_in *)&pSocket->pRuntime->host_addr_send;
                        host_addr_send_ip4->sin_port = htons(u16SendToPort);
                        host_addr_send_ip4->sin_addr.s_addr = htonl(u32SendToIP);
                        ESP_LOGW(TAG, "host_addr_send " IPSTR ":%d", IP2STR_4(host_addr_send_ip4->sin_addr.s_addr), htons(host_addr_send_ip4->sin_port));
                        ESP_LOGW(TAG, "host_addr_send 0x%08X:%d", (int)(host_addr_send_ip4->sin_addr.s_addr), htons(host_addr_send_ip4->sin_port));
                    }

                    socklen_t socklen = sizeof(pSocket->pRuntime->host_addr_send);
                    nLengthSent = sendto(nSocketClient, au8Temp, nLength, 0, (struct sockaddr *)&pSocket->pRuntime->host_addr_send, socklen);
                    
                }
                else
                {
                    nLengthSent =   send(nSocketClient, au8Temp, nLength, 0);
                }
                
                if (nLengthSent > 0)
                {
                    if (nLengthSent != nLength)
                    {
                        ESP_LOGE(TAG, "Error during send to %s socket %s[%d] %d: send %d/%d bytes", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, nLengthSent, nLength);
                        //socket_disconnect(pSocket);
                        socket_disconnect_connection(pSocket, nConnectionIndex);   /* Removing Socket Client Connection */
                    }
                    else
                    {
                        if (pSocket->onSend != NULL)
                        {
                            pSocket->onSend(nConnectionIndex, (char*)au8Temp, nLengthSent);
                        }
                        
                    }
                }
                else
                {
                    err = errno;
                    //if (err != EAGAIN)
                    {
                        ESP_LOGE(TAG, "Error during send to %s socket %s[%d] %d: errno %d (%s)", sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient, err, strerror(err));
                        //socket_disconnect(pSocket);
                        socket_disconnect_connection(pSocket, nConnectionIndex);   /* Removing Socket Client Connection */
                    }
                }
            }
            free(au8Temp);
        }
        else
        {
            ESP_LOGE(TAG, "Error during allocate %d bytes for send from %s socket %s[%d] %d", nLength, sockTypeString, pSocket->cName, nConnectionIndex, nSocketClient);
        }
    }
    else
    {
        if (pSocket->bIndentifyNeeded)
        {
            pSocket->nTimeoutSendEnable += nTaskRestTimeTicks;
            if (pSocket->nTimeoutSendEnable >= pdMS_TO_TICKS(10000))
            {
                ESP_LOGE(TAG, "Send Enable and Identify disable on Timeout socket %s[%d] %d", pSocket->cName, nConnectionIndex, nSocketClient);
                if (pSocket->bIndentifyForced)
                {
                    send_identification_answer(pSocket, nConnectionIndex);    //forced send identification
                }

                pSocket->bSendEnable = true;
                pSocket->bIndentifyNeeded = false;
            }
        }
        else
        {
            pSocket->bSendEnable = true;
        }
    }
}

void socket_get_adapter_interface_ip(drv_socket_t* pSocket)
{
    /* Get IP Address of the selected adapter interface (new selected ip address stored as string in pSocket->pRuntime->cAdapterInterfaceIP) */
    esp_netif_ip_info_t ip_info;
    struct sockaddr_in adapter_interface_addr;

    adapter_interface_addr.sin_addr.s_addr = htonl(INADDR_NONE);

    if(pSocket->pRuntime->adapter_if == ESP_IF_WIFI_STA)
    {
        #if CONFIG_USE_WIFI
        esp_netif_t* esp_netif = drv_wifi_get_netif_sta();
        if (esp_netif != NULL)
        {
            esp_netif_get_ip_info(esp_netif, &ip_info);
            inet_addr_from_ip4addr(&adapter_interface_addr.sin_addr,&ip_info.ip);
        }
        #else
        adapter_interface_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        #endif
    }
    else 
    if(pSocket->pRuntime->adapter_if == ESP_IF_WIFI_AP)
    {
        #if CONFIG_USE_WIFI
        esp_netif_t* esp_netif = drv_wifi_get_netif_ap();
        if (esp_netif != NULL)
        {
            esp_netif_get_ip_info(esp_netif, &ip_info);
            inet_addr_from_ip4addr(&adapter_interface_addr.sin_addr,&ip_info.ip);
        }
        #else
        adapter_interface_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        #endif
    }
    else //if(pSocket->adapter_if >= ESP_IF_ETH)
    {
        #if CONFIG_USE_ETHERNET
        int eth_index = pSocket->pRuntime->adapter_if - ESP_IF_ETH;
        if (drv_eth_get_netif_count() > eth_index)
        {
            esp_netif_t* esp_netif = drv_eth_get_netif(eth_index);
            if (esp_netif != NULL)
            {
                esp_netif_get_ip_info(esp_netif, &ip_info);
                inet_addr_from_ip4addr(&adapter_interface_addr.sin_addr,&ip_info.ip);
            }
        }
        else    /* use default */
        #endif
        {
            adapter_interface_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }
    }

    in_addr_t interface_address = adapter_interface_addr.sin_addr.s_addr;
    char *adapter_interface_ip = ip4addr_ntoa_r((ip4_addr_t*)&adapter_interface_addr.sin_addr.s_addr, pSocket->pRuntime->cAdapterInterfaceIP, sizeof(pSocket->pRuntime->cAdapterInterfaceIP));
    
    if (interface_address == htonl(INADDR_NONE))
    {
        ESP_LOGE(TAG, "Socket %s bad or unimplemented adapter interface selected: %d", pSocket->cName, pSocket->pRuntime->adapter_if);
    }
    else if (interface_address == htonl(INADDR_ANY))
    {
        ESP_LOGW(TAG, "Socket %s default adapter interface selected IP: %s", pSocket->cName, adapter_interface_ip);
    }
    else
    {
        ESP_LOGI(TAG, "Socket %s adapter interface selected IP: %s", pSocket->cName, adapter_interface_ip);
    }

    //pSocket->pRuntime->adapter_interface_ip_address = interface_address;
}

void socket_prepare_adapter_interface_ip_info(drv_socket_t* pSocket)
{
    /* Configure Bind to IP Info (Adapter interface Info) */
    #ifdef CONFIG_EXAMPLE_IPV6
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    if (pSocket->bIPV6)setsockopt(pSocket->nSocketIndex, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
    #endif

    #ifdef CONFIG_EXAMPLE_IPV6
    if (pSocket->bIPV6)
    {
        struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&pSocket->pRuntime->adapterif_addr;
        bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = pSocket->address_family;
        dest_addr_ip6->sin6_port = htons(pSocket->u16Port);
    }
    else
    #endif
    {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&pSocket->pRuntime->adapterif_addr;
        //dest_addr_ip4->sin_addr.s_addr = pSocket->pRuntime->adapter_interface_ip_address;
        dest_addr_ip4->sin_addr.s_addr = inet_addr(pSocket->pRuntime->cAdapterInterfaceIP);
        dest_addr_ip4->sin_family = pSocket->address_family;
        dest_addr_ip4->sin_port = htons(pSocket->u16Port);
    }
}



void clear_dns_cache() 
{
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        printf("Failed to get netif handle\n");
        return;
    }
    esp_netif_dns_info_t dns_info = {0};
    dns_info.ip.u_addr.ip4.addr = 0;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info);
}

uint32_t drv_socket_caller_id;

/* try resolve cURL to IP Address. If not resolved - use cHostIP */
char* socket_get_host_ip_address(drv_socket_t* pSocket)
{
    char* pLastUsedHostIP = NULL;
    bool bURLResolved;

    /* Try Resolve URL */
    bURLResolved = false;
    if ((pSocket->cURL != NULL) && (strlen(pSocket->cURL) > 0))
    {
        ESP_LOGI(TAG, "Socket %s Start resolve URL %s", pSocket->cName, pSocket->cURL);

        bURLResolved = drv_dns_resolve(pSocket->cURL, pSocket->cHostIPResolved, sizeof(pSocket->cHostIPResolved), &pSocket->bActiveTask);
        if (bURLResolved)
        {
            ESP_LOGI(TAG, "Socket %s resolved URL %s to ip address: %s", pSocket->cName, pSocket->cURL, pSocket->cHostIPResolved);
        }
        else
        {
            ESP_LOGE(TAG, "Socket %s Fail resolve URL %s - use default IP: %s", pSocket->cName,pSocket->cURL, pSocket->cHostIP);
        }

        ESP_LOGI(TAG, "Socket %s Final resolve URL %s", pSocket->cName, pSocket->cURL); 
    }

    if(bURLResolved)  
    {
        pLastUsedHostIP = pSocket->cHostIPResolved;
    } 
    else
    {
        pLastUsedHostIP = pSocket->cHostIP;
    }

    return pLastUsedHostIP;
}

void socket_prepare_host_ip_info(drv_socket_t* pSocket)
{
    char cRecvFromIP[16] = "255.255.255.255";
    char* pRecvFromIP = cRecvFromIP;

    //char cSendToIP[16] = "192.168.3.118";
    char cSendToIP[16] = "255.255.255.255";
    char* pSendToIP = cSendToIP;


    /* Host IP Address Preparation - host_addr_main */
    pSocket->pRuntime->bBroadcastRxTx = false;
    #ifdef CONFIG_EXAMPLE_IPV6
    if (pSocket->bIPV6)
    {
        struct sockaddr_in6 *host_addr_ip6 = (struct sockaddr_in6 *)&pSocket->pRuntime->host_addr_main;
        host_addr_ip6->sin6_addr.un = inet_addr(pSocket->pRuntime->pLastUsedHostIP);
        host_addr_ip6->sin6_family = pSocket->address_family;
        host_addr_ip6->sin6_port = htons(pSocket->u16Port);

        struct sockaddr_in6 *host_addr_recv_ip6 = (struct sockaddr_in6 *)&pSocket->pRuntime->host_addr_recv;
        host_addr_recv_ip6->sin6_addr.un = inet_addr(pRecvFromIP);
        host_addr_recv_ip6->sin6_family = pSocket->address_family;
        host_addr_recv_ip6->sin6_port = htons(pSocket->u16Port);

        struct sockaddr_in6 *host_addr_send_ip6 = (struct sockaddr_in6 *)&pSocket->pRuntime->host_addr_send;
        host_addr_send_ip6->sin6_addr.un = inet_addr(pSendToIP);
        host_addr_send_ip6->sin6_family = pSocket->address_family;
        host_addr_send_ip6->sin6_port = htons(pSocket->u16Port);
    }
    else
    #endif
    {
        struct sockaddr_in *host_addr_ip4 = (struct sockaddr_in *)&pSocket->pRuntime->host_addr_main;
        host_addr_ip4->sin_addr.s_addr = inet_addr(pSocket->pRuntime->pLastUsedHostIP);
        host_addr_ip4->sin_family = pSocket->address_family;
        host_addr_ip4->sin_port = htons(pSocket->u16Port);

        struct sockaddr_in *host_addr_recv_ip4 = (struct sockaddr_in *)&pSocket->pRuntime->host_addr_recv;
        host_addr_recv_ip4->sin_addr.s_addr = inet_addr(pRecvFromIP);
        host_addr_recv_ip4->sin_family = pSocket->address_family;
        host_addr_recv_ip4->sin_port = htons(pSocket->u16Port);

        struct sockaddr_in *host_addr_send_ip4 = (struct sockaddr_in *)&pSocket->pRuntime->host_addr_send;
        host_addr_send_ip4->sin_addr.s_addr = inet_addr(pSendToIP);
        host_addr_send_ip4->sin_family = pSocket->address_family;
        host_addr_send_ip4->sin_port = htons(pSocket->u16Port);

        if (((host_addr_ip4->sin_addr.s_addr >> 0) & 0xFF) == 0xFF)pSocket->pRuntime->bBroadcastRxTx = true;
        if (((host_addr_ip4->sin_addr.s_addr >> 8) & 0xFF) == 0xFF)pSocket->pRuntime->bBroadcastRxTx = true;
        if (((host_addr_ip4->sin_addr.s_addr >>16) & 0xFF) == 0xFF)pSocket->pRuntime->bBroadcastRxTx = true;
        if (((host_addr_ip4->sin_addr.s_addr >>24) & 0xFF) == 0xFF)pSocket->pRuntime->bBroadcastRxTx = true;
    }

    ESP_LOGI(TAG, "Socket %s Bind/Connect: %s", pSocket->cName, pSocket->pRuntime->pLastUsedHostIP); 
    ESP_LOGI(TAG, "Socket %s RecvFrom:     %s", pSocket->cName, pRecvFromIP); 
    ESP_LOGI(TAG, "Socket %s SendTo:       %s", pSocket->cName, pSendToIP); 


}

void socket_strt(drv_socket_t* pSocket)
{
    int err;
    int nSocketIndex = socket(pSocket->address_family, pSocket->protocol_type, pSocket->protocol);

    if (nSocketIndex < 0) 
    {
        err = errno;
        ESP_LOGE(TAG, "Unable to create socket %s %d: errno %d (%s)", pSocket->cName, nSocketIndex, err, strerror(err));
    }
    else
    {
        ESP_LOGW(TAG, "Created socket %s %d: AdapterIF: %d Address Family: %d", pSocket->cName, nSocketIndex, pSocket->pRuntime->adapter_if, pSocket->address_family);
    }
    if (pSocket->bServerType)
    {
        pSocket->nSocketIndexServer = nSocketIndex;
    }
    else
    {
        socket_connection_add_to_list(pSocket, nSocketIndex);
        //pSocket->nSocketIndexPrimer = nSocketIndex;
    }
}

void socket_connect_server_periodic(drv_socket_t* pSocket)
{
    int err;


    ESP_LOGD(TAG, "Socket %s %d periodic check incoming connections", pSocket->cName, pSocket->nSocketIndexServer);

    struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t addr_len = sizeof(source_addr);

    // Set a timeout of 0 ms
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    // Set the socket to non-blocking mode
    fcntl(pSocket->nSocketIndexServer, F_SETFL, O_NONBLOCK);

    // Use select() to wait for the socket to become readable or for the timeout to expire
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(pSocket->nSocketIndexServer, &rfds);
    int ready = select(pSocket->nSocketIndexServer + 1, &rfds, NULL, NULL, &timeout);

    if (ready < 0) 
    {
        ESP_LOGE(TAG, "Socket %s %d Error in select() function: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, errno, strerror(errno));
        socket_disconnect(pSocket);
        //close(pSocket->nSocketIndexServer);
        //pSocket->nSocketIndexServer = -1;
    } 
    else if (ready == 0) 
    {
        if ((pSocket->nTaskLoopCounter % ((pdMS_TO_TICKS(30000)) / nTaskRestTimeTicks)) == 0)
        {
            ESP_LOGW(TAG, "Socket %s %d Timeout waiting for client to connect", pSocket->cName, pSocket->nSocketIndexServer);
        }
        //socket_disconnect(pSocket);
        //close(pSocket->nSocketIndexServer);
        //pSocket->nSocketIndexServer = -1;
    } 
    else 
    {
        int nNewSocketClientIndex = accept(pSocket->nSocketIndexServer, (struct sockaddr *)&source_addr, &addr_len);
        if (nNewSocketClientIndex < 0) 
        {
            err = errno;
            ESP_LOGE(TAG, "Socket %s %d Unable to accept connection %d: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, nNewSocketClientIndex, err, strerror(err));
            //socket_disconnect(pSocket); //To Do check not to disconnect socket if no more connections available
            //close(pSocket->nSocketIndexServer);
            //pSocket->nSocketIndexServer = -1;
        }
        else
        {
            char addr_str[128];
            if (source_addr.ss_family == PF_INET) 
            {
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
            }
            else if (source_addr.ss_family == PF_INET6) 
            {
                #if LWIP_IPV6
                inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                #else
                ESP_LOGE(TAG, "Socket %s %d IPv6 Need LWIP_IPV6 defined", pSocket->cName, pSocket->nSocketIndexServer);
                #endif
            }
            ESP_LOGI(TAG, "Socket %s %d accepted ip address: %s", pSocket->cName, pSocket->nSocketIndexServer, addr_str);

            socket_connection_add_to_list(pSocket, nNewSocketClientIndex);
            //pSocket->nSocketIndexPrimer = nNewSocketClientIndex;
        }
    }
    // Set the socket back to blocking mode
    fcntl(pSocket->nSocketIndexServer, F_SETFL, 0);

}

void socket_connect_server(drv_socket_t* pSocket)
{
    int err = 0;

    int opt = 1;
    socklen_t optlen = sizeof(opt);

    int ret_so;

    ret_so = getsockopt( pSocket->nSocketIndexServer , SOL_SOCKET, SO_REUSEADDR,(void*)&opt, &optlen);
    if (ret_so < 0)
    {
        err = errno;
        ESP_LOGE(TAG, LOG_COLOR(LOG_COLOR_CYAN)"Socket %s %d getsockopt  SO_REUSEADDR=%d retv = %d. errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, opt, ret_so, err, strerror(errno));
    }

    opt = 1;
    ret_so = setsockopt( pSocket->nSocketIndexServer , SOL_SOCKET, SO_REUSEADDR,(void*)&opt, sizeof(opt));
    if (ret_so < 0)
    {
        err = errno;
        ESP_LOGE(TAG, LOG_COLOR(LOG_COLOR_CYAN)"Socket %s %d setsockopt  SO_REUSEADDR=%d retv = %d. errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, opt, ret_so, err, strerror(errno));
    }

    //vTaskDelay(pdMS_TO_TICKS(100));



    int eError = bind(pSocket->nSocketIndexServer, (struct sockaddr *)&pSocket->pRuntime->adapterif_addr, sizeof(pSocket->pRuntime->adapterif_addr));
    if (eError != 0) 
    {
        err = errno;
        ESP_LOGE(TAG, "Socket %s %d unable to bind: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, err, strerror(err));

        struct sockaddr_in *test_addr_ip4 = (struct sockaddr_in *)&pSocket->pRuntime->adapterif_addr;
        //dest_addr_ip4->sin_addr.s_addr = pSocket->pRuntime->adapter_interface_ip_address;

        in_addr_t interfaceAddress = test_addr_ip4->sin_addr.s_addr;
        const ip4_addr_t* pInterfaceAddress = (const ip4_addr_t*)&interfaceAddress;
        char cAdapterInterfaceIP[16];
        char *adapter_interface_ip = ip4addr_ntoa_r(pInterfaceAddress, cAdapterInterfaceIP, sizeof(cAdapterInterfaceIP));
        ESP_LOGW(TAG, "Socket %s %d adapterif addr:%s port:%d family:%d", pSocket->cName, pSocket->nSocketIndexServer, adapter_interface_ip, htons(test_addr_ip4->sin_port), test_addr_ip4->sin_family);
        
        socket_disconnect(pSocket);
        //close(pSocket->nSocketIndexServer);
        //pSocket->nSocketIndexServer = -1;
    }
    else
    {
        ESP_LOGI(TAG, "Socket %s %d bound to IF %s:%d", pSocket->cName, pSocket->nSocketIndexServer, pSocket->pRuntime->cAdapterInterfaceIP, pSocket->u16Port);

        eError = listen(pSocket->nSocketIndexServer, 1);
        if (eError != 0) 
        {
            err = errno;
            ESP_LOGE(TAG, "Error occurred during listen socket %s %d: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, err, strerror(err));
            socket_disconnect(pSocket);
            //close(pSocket->nSocketIndexServer);
            //pSocket->nSocketIndexServer = -1;
        }
        else
        {
            ESP_LOGI(TAG, "Socket %s %d listening", pSocket->cName, pSocket->nSocketIndexServer);

            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t addr_len = sizeof(source_addr);

            // Set a timeout of 30 seconds
            struct timeval timeout;
            timeout.tv_sec = 30;
            timeout.tv_usec = 0;

            // Set the socket to non-blocking mode
            fcntl(pSocket->nSocketIndexServer, F_SETFL, O_NONBLOCK);

            // Use select() to wait for the socket to become readable or for the timeout to expire
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(pSocket->nSocketIndexServer, &rfds);
            int ready = select(pSocket->nSocketIndexServer + 1, &rfds, NULL, NULL, &timeout);

            if (ready < 0) 
            {
                ESP_LOGE(TAG, "Socket %s %d Error in select() function: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, errno, strerror(errno));
                socket_disconnect(pSocket);
                //close(pSocket->nSocketIndexServer);
                //pSocket->nSocketIndexServer = -1;
            } 
            else if (ready == 0) 
            {
                ESP_LOGW(TAG, "Socket %s %d Timeout waiting for client to connect", pSocket->cName, pSocket->nSocketIndexServer);
                //socket_disconnect(pSocket);
                //close(pSocket->nSocketIndexServer);
                //pSocket->nSocketIndexServer = -1;
            } 
            else 
            {
                int nNewSocketClientIndex = accept(pSocket->nSocketIndexServer, (struct sockaddr *)&source_addr, &addr_len);
                if (nNewSocketClientIndex < 0) 
                {
                    err = errno;
                    ESP_LOGE(TAG, "Socket %s %d Unable to accept connection %d: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexServer, nNewSocketClientIndex, err, strerror(err));
                    socket_disconnect(pSocket);
                    //close(pSocket->nSocketIndexServer);
                    //pSocket->nSocketIndexServer = -1;
                }
                else
                {
                    char addr_str[128];
                    if (source_addr.ss_family == PF_INET) 
                    {
                        inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
                    }
                    else if (source_addr.ss_family == PF_INET6) 
                    {
                        #if LWIP_IPV6
                        inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                        #else
                        ESP_LOGE(TAG, "Socket %s %d IPv6 Need LWIP_IPV6 defined", pSocket->cName, pSocket->nSocketIndexServer);
                        #endif
                    }
                    ESP_LOGI(TAG, "Socket %s %d accepted ip address: %s", pSocket->cName, pSocket->nSocketIndexServer, addr_str);

                    socket_connection_add_to_list(pSocket, nNewSocketClientIndex);
                    //pSocket->nSocketIndexPrimer = nNewSocketClientIndex;
                }
            }
            // Set the socket back to blocking mode
            fcntl(pSocket->nSocketIndexServer, F_SETFL, 0);
        }
    }
}

void socket_connect_client(drv_socket_t* pSocket)
{
    int err;
    int nConnectionIndex  = 0;
    
    if (pSocket->nSocketConnectionsCount != 1)
    {
        ESP_LOGE(TAG, "Socket %s unexpected client type with %d connections ", pSocket->cName, pSocket->nSocketConnectionsCount);
    }
    else
    {
        /* client bind should be not neccesairy because an auto bind will take place at first send/recv/sendto/recvfrom using a system assigned local port */
        int eError = bind(pSocket->nSocketIndexPrimer[nConnectionIndex], (struct sockaddr *)&pSocket->pRuntime->adapterif_addr, sizeof(pSocket->pRuntime->adapterif_addr));
        if (eError != 0) 
        {
            err = errno;
            ESP_LOGE(TAG, "Socket %s %d unable to bind: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexPrimer[nConnectionIndex], err, strerror(err));
            socket_disconnect(pSocket);
            //close(pSocket->nSocketIndexPrimer);
            //pSocket->nSocketIndexPrimer = -1;
        }
        else
        {
            ESP_LOGI(TAG, "Socket %s %d bound to IF %s:%d", pSocket->cName, pSocket->nSocketIndexPrimer[nConnectionIndex], pSocket->pRuntime->cAdapterInterfaceIP, pSocket->u16Port);

            /* Connect to the host by the network interface */
            if (pSocket->pRuntime->bBroadcastRxTx == false)
            {
                int eError = connect(pSocket->nSocketIndexPrimer[nConnectionIndex], (struct sockaddr *)&pSocket->pRuntime->host_addr_main, sizeof(pSocket->pRuntime->host_addr_main));
                if (eError != 0) 
                {
                    err = errno;
                    ESP_LOGE(TAG, "Socket %s %d unable to connect: errno %d (%s)", pSocket->cName, pSocket->nSocketIndexPrimer[nConnectionIndex], err, strerror(err));
                    socket_disconnect(pSocket);
                    //close(pSocket->nSocketIndexPrimer);
                    //pSocket->nSocketIndexPrimer = -1; 
                }
                else
                {
                    ESP_LOGI(TAG, "Socket %s %d connected, port %d", pSocket->cName, pSocket->nSocketIndexPrimer[nConnectionIndex], pSocket->u16Port);
                }
            }
            else
            {
                ESP_LOGI(TAG, "Socket %s %d connected only trough bind (broadcast host address detected), port %d", pSocket->cName, pSocket->nSocketIndexPrimer[nConnectionIndex], pSocket->u16Port);
            }
        }
    }
}

void socket_prepare_ip_info(drv_socket_t* pSocket)
{
    socket_get_adapter_interface_ip(pSocket);
    socket_prepare_adapter_interface_ip_info(pSocket);

    pSocket->pRuntime->pLastUsedHostIP = socket_get_host_ip_address(pSocket);
    socket_prepare_host_ip_info(pSocket);
}

void socket_set_options(drv_socket_t* pSocket, int nConnectionIndex)
{
    int err;

    /* When changed with primer socket here was the main socket (nSocketIndex) */
    if (pSocket->bPermitBroadcast)
    {
        int bc = 1;
        if (setsockopt(pSocket->nSocketIndexPrimer[nConnectionIndex], SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc)) < 0)
        {
            err = errno;
            ESP_LOGE(TAG, "Socket %s[%d] %d Failed to set sock option permit broadcast: errno %d (%s)", pSocket->cName, nConnectionIndex, pSocket->nSocketIndexPrimer[nConnectionIndex], err, strerror(err));
        }
    }

    if (pSocket->protocol_type == SOCK_STREAM)   /* if TCP */
    {
        // Set tcp keepalive option
        int keepAlive = 1;
        int keepIdle = CONFIG_SOCKET_DEFAULT_KEEPALIVE_IDLE;
        int keepInterval = CONFIG_SOCKET_DEFAULT_KEEPALIVE_INTERVAL;
        int keepCount = CONFIG_SOCKET_DEFAULT_KEEPALIVE_COUNT;
        
        if(setsockopt(pSocket->nSocketIndexPrimer[nConnectionIndex], SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int)) < 0)
        {
            err = errno;
            ESP_LOGE(TAG, "Socket %s[%d] %d Failed to set sock option keep alive: errno %d (%s)", pSocket->cName, nConnectionIndex, pSocket->nSocketIndexPrimer[nConnectionIndex], err, strerror(err));
        }

        if(setsockopt(pSocket->nSocketIndexPrimer[nConnectionIndex], IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int)) < 0)
        {
            err = errno;
            ESP_LOGE(TAG, "Socket %s[%d] %d Failed to set sock option keep idle: errno %d (%s)", pSocket->cName, nConnectionIndex, pSocket->nSocketIndexPrimer[nConnectionIndex], err, strerror(err));
        }

        if(setsockopt(pSocket->nSocketIndexPrimer[nConnectionIndex], IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int)) < 0)
        {
            err = errno;
            ESP_LOGE(TAG, "Socket %s[%d] %d Failed to set sock option keep intvl: errno %d (%s)", pSocket->cName, nConnectionIndex, pSocket->nSocketIndexPrimer[nConnectionIndex], err, strerror(err));
        }

        if(setsockopt(pSocket->nSocketIndexPrimer[nConnectionIndex], IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int)) < 0)
        {
            err = errno;
            ESP_LOGE(TAG, "Socket %s[%d] %d Failed to set sock option keen cnt: errno %d (%s)", pSocket->cName, nConnectionIndex, pSocket->nSocketIndexPrimer[nConnectionIndex], err, strerror(err));
        }
    }
}

void socket_on_connect(drv_socket_t* pSocket, int nConnectionIndex)
{
    if (pSocket->bResetSendStreamOnConnect)
    {
        drv_stream_init(pSocket->pSendStream[nConnectionIndex], NULL, 0);
    }

    drv_stream_init(pSocket->pRecvStream[nConnectionIndex], NULL, 0);

    if (pSocket->onConnect != NULL)
    {
        pSocket->onConnect(nConnectionIndex);
    }
    

    pSocket->bIndentifyNeeded = pSocket->bIndentifyForced;
    if (pSocket->bIndentifyNeeded)
    {
        pSocket->bSendEnable = false;
    }
    else
    {
        pSocket->bSendEnable = pSocket->bAutoSendEnable;
    }
    
    pSocket->nTimeoutSendEnable = 0;
    pSocket->nPingTicks = 0;
    pSocket->nPingCount = 0;
}

void socket_add_to_list(drv_socket_t* pSocket)
{
    nSocketCountTotal++;
    if (nSocketListCount < DRV_SOCKET_COUNT_MAX)
    {
        pSocketList[nSocketListCount++] = pSocket;
    }
    
}

void socket_del_from_list(drv_socket_t* pSocket)
{
    nSocketCountTotal--;
    for (int index = 0; index < nSocketListCount; index++)
    {
        if (pSocketList[index] == pSocket)
        {
            pSocketList[index] = NULL;
            for (int pos = index+1; pos < nSocketListCount; pos++)
            {
                
                pSocketList[pos-1] = pSocketList[pos];
                pSocketList[pos] = NULL;
            }
            nSocketListCount--;
        }
    }
}

void socket_runtime_init(drv_socket_t* pSocket)
{
    /* Runtime Initialization */
    strcpy(pSocket->pRuntime->cAdapterInterfaceIP,"0.0.0.0");
    pSocket->pRuntime->pLastUsedHostIP = pSocket->cHostIP;
    pSocket->pRuntime->bBroadcastRxTx = false;
    bzero((void*)&pSocket->pRuntime->host_addr_main, sizeof(pSocket->pRuntime->host_addr_main));
    bzero((void*)&pSocket->pRuntime->host_addr_recv, sizeof(pSocket->pRuntime->host_addr_recv));
    bzero((void*)&pSocket->pRuntime->host_addr_send, sizeof(pSocket->pRuntime->host_addr_send));
    bzero((void*)&pSocket->pRuntime->adapterif_addr, sizeof(pSocket->pRuntime->adapterif_addr));

    #if CONFIG_USE_ETHERNET
    pSocket->pRuntime->adapter_if = ESP_IF_ETH + drv_eth_get_netif_count(); //set as not selected if
    #else
    pSocket->pRuntime->adapter_if = ESP_IF_ETH; //set as not selected if
    #endif
}

void socket_force_disconnect(drv_socket_t* pSocket)
{
    /* drv_socket_t Initialization */
    if (pSocket->nSocketIndexServer >= 0)
    {
        shutdown(pSocket->nSocketIndexServer, SHUT_RDWR);
        close(pSocket->nSocketIndexServer);
        pSocket->nSocketIndexServer = -1;
    }
    for (int nIndex = 0; nIndex < DRV_SOCKET_MAX_CLIENTS; nIndex++)
    {
        if (pSocket->nSocketIndexPrimer[nIndex] >= 0)
        {
            shutdown(pSocket->nSocketIndexPrimer[nIndex], SHUT_RDWR);
            //shutdown(pSocket->nSocketIndexClient, 0);
            close(pSocket->nSocketIndexPrimer[nIndex]);
            pSocket->nSocketIndexPrimer[nIndex] = -1;
        }
    }
}

bool socket_check_interface_connected(esp_interface_t interface)
{
    if(interface == ESP_IF_WIFI_STA)
    {
        #if CONFIG_USE_WIFI
        return drv_wifi_get_sta_connected();
        #else
        return false;
        #endif
    }
    else if(interface == ESP_IF_WIFI_AP)
    {
        #if CONFIG_USE_WIFI
        return drv_wifi_get_ap_connected();
        #else
        return false;
        #endif
    }
    else //if(interface >= ESP_IF_ETH)
    {
        #if CONFIG_USE_ETHERNET
        int eth_index = interface - ESP_IF_ETH;
        return drv_eth_get_connected(eth_index);
        #else
        return false;
        #endif
    }
}

void socket_select_adapter_if(drv_socket_t* pSocket)
{
    #if CONFIG_USE_ETHERNET
    if (pSocket->pRuntime->adapter_if >= (ESP_IF_ETH + drv_eth_get_netif_count()))  //not selected valid if
    #else
    if (pSocket->pRuntime->adapter_if >= ESP_IF_ETH)  //not selected valid if
    #endif
    {
        ESP_LOGE(TAG, "Socket %s not selected valid if", pSocket->cName);
        pSocket->pRuntime->adapter_if = pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_DEFAULT];
        pSocket->bDisconnectRequest = true;
    }
    else if (pSocket->pRuntime->adapter_if == pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_DEFAULT])
    {
        if (socket_check_interface_connected(pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_DEFAULT]))
        {
            if (pSocket->bPriorityBackupAdapterInterface == DRV_SOCKET_PRIORITY_INTERFACE_BACKUP)
            {
                if (socket_check_interface_connected(pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_BACKUP]))
                {
                    ESP_LOGW(TAG, "Socket %s switch to INTERFACE DEFAULT -> BACKUP", pSocket->cName);
                    pSocket->pRuntime->adapter_if = pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_BACKUP];
                    pSocket->bDisconnectRequest = true;
                } 
            }
        }
        else
        {
            if (socket_check_interface_connected(pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_BACKUP]))
            {
                ESP_LOGW(TAG, "Socket %s switch to INTERFACE DEFAULT -> BACKUP", pSocket->cName);
                pSocket->pRuntime->adapter_if = pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_BACKUP];
                //pSocket->bDisconnectRequest = true;
            } 
        }
    }
    else if (pSocket->pRuntime->adapter_if == pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_BACKUP])
    {
        if (socket_check_interface_connected(pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_BACKUP]))
        {
            if (pSocket->bPriorityBackupAdapterInterface == DRV_SOCKET_PRIORITY_INTERFACE_DEFAULT)
            {
                if (socket_check_interface_connected(pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_DEFAULT]))
                {
                    ESP_LOGW(TAG, "Socket %s switch to INTERFACE BACKUP -> DEFAULT", pSocket->cName);
                    pSocket->pRuntime->adapter_if = pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_DEFAULT];
                    pSocket->bDisconnectRequest = true;
                }
            }
        }
        else
        {
            if (socket_check_interface_connected(pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_DEFAULT]))
            {
                ESP_LOGW(TAG, "Socket %s switch to INTERFACE BACKUP -> DEFAULT", pSocket->cName);
                pSocket->pRuntime->adapter_if = pSocket->adapter_interface[DRV_SOCKET_ADAPTER_INTERFACE_DEFAULT];
                //pSocket->bDisconnectRequest = true;
            }  
        }
    }

}


static void socket_task(void* parameters)
{
    drv_socket_t* pSocket = (drv_socket_t*)parameters;

    if (pSocket == NULL)
    {
        ESP_LOGE(TAG, "Unable to create socket NULL task");
        vTaskDelete(NULL);
    }

    drv_socket_runtime_t* pSocketRuntime = malloc(sizeof(drv_socket_runtime_t));

    if (pSocketRuntime == NULL)
    {
        ESP_LOGE(TAG, "Unable to allocate memory for runtime variables of socket %s", pSocket->cName);
        pSocket->pTask = NULL;
        vTaskDelete(NULL);
    }

    pSocket->pRuntime = pSocketRuntime;

    socket_runtime_init(pSocket);
    socket_force_disconnect(pSocket);

    pSocket->nTaskLoopCounter = 0;
    pSocket->bActiveTask = true;
    pSocket->bDisconnectRequest = false;
    pSocket->bConnected = false;

    socket_add_to_list(pSocket);
  
    while(pSocket->bActiveTask)
    {

        socket_select_adapter_if(pSocket);

        /* socket disconnect */
        //if ((pSocket->nSocketIndexPrimer >= 0) || (pSocket->nSocketIndexServer >= 0))
        if ((pSocket->nSocketConnectionsCount > 0) || (pSocket->nSocketIndexServer >= 0))
        {
            if (pSocket->bDisconnectRequest)
            {
                pSocket->bDisconnectRequest = false;
                socket_disconnect(pSocket);
            }
        }

        /* socket is must be disconnected */
        if (pSocket->pRuntime != NULL)
        {
            if (pSocket->pRuntime->adapter_if == ESP_IF_ETH)//to do fix for more than one eth interface
            {
                pSocket->bConnectDeny = pSocket->bConnectDenyETH;
            }
            else if (pSocket->pRuntime->adapter_if == ESP_IF_WIFI_STA)
            {
                pSocket->bConnectDeny = pSocket->bConnectDenySTA;
            }
            else if (pSocket->pRuntime->adapter_if == ESP_IF_WIFI_AP)
            {
                pSocket->bConnectDeny = pSocket->bConnectDenyAP;
            }
        }

        if (pSocket->bConnectDeny)
        {
            pSocket->bDisconnectRequest = true;
        }
        else 
        /* socket is connected */
        if (pSocket->bConnected)
        {
            /* Data from/to all connections */
            for (int nIndex = 0; nIndex < pSocket->nSocketConnectionsCount; nIndex++)
            {
                /* Receive Data */
                socket_recv(pSocket, nIndex);
                /* Send Data */
                socket_send(pSocket, nIndex);
            }
            /* check for incoming connections */
            socket_connect_server_periodic(pSocket);
            

            //ESP_LOGI(TAG, "socket %s %d: Loop Connected", pSocket->cName, nSocketClient);
        }
        else
        {
            /* start connection from beginning */
            //socket_disconnect(pSocket);

            /* need to create socket (server for server or primer for client) */
            if (((pSocket->bServerType == true) && (pSocket->nSocketIndexServer < 0)) 
            // || ((pSocket->bServerType == false) && (pSocket->nSocketIndexPrimer[0] < 0)))
            || ((pSocket->bServerType == false) && (pSocket->nSocketConnectionsCount <= 0)))
            {
                if (pSocket->bServerType)
                {
                    ESP_LOGW(TAG, "socket server %s %d: Try Create Socket", pSocket->cName, pSocket->nSocketIndexServer);
                }
                else
                {
                    ESP_LOGW(TAG, "socket client %s[0] %d: Try Create Socket", pSocket->cName, pSocket->nSocketIndexPrimer[0]);
                }
                /* Try Create Socket */
                socket_strt(pSocket);
                //pSocket->bConnected = false; - not needed
            }
            else
            /* socket is created but not connected */
            if (((pSocket->bServerType == true) && (pSocket->nSocketIndexServer >= 0)) 
            // || ((pSocket->bServerType == false) && (pSocket->nSocketIndexPrimer[0] >= 0)))
            || ((pSocket->bServerType == false) && (pSocket->nSocketConnectionsCount > 0)))
            {
                if (pSocket->bServerType)
                {
                    ESP_LOGW(TAG, "socket server %s %d: Try Connect Socket", pSocket->cName, pSocket->nSocketIndexServer);
                }
                else
                {
                    ESP_LOGW(TAG, "socket client %s[0] %d: Try Connect Socket", pSocket->cName, pSocket->nSocketIndexPrimer[0]);
                }
                /* Try Connect Socket */
                socket_prepare_ip_info(pSocket);

                if (pSocket->bServerType)
                {
                    socket_connect_server(pSocket);
                }
                else /* Client socket type */
                {
                    socket_connect_client(pSocket);
                }

                if ((pSocket->bServerType == true) && (pSocket->nSocketIndexServer >= 0)) 
                {
                    pSocket->bConnected = true;
                    pSocket->bDisconnectRequest = false;
                }
                else
                if (pSocket->nSocketConnectionsCount > 0)
                //if (pSocket->nSocketIndexPrimer[0] > 0)
                {
                    
                    pSocket->bConnected = true;
                    pSocket->bDisconnectRequest = false;
                }
                else
                {
                    //pSocket->bConnected = false; - not needed
                    vTaskDelay(nReconnectTimeTicks); 
                }
            }
        }
        pSocket->nTaskLoopCounter++;
        vTaskDelay(nTaskRestTimeTicks);
    }
    socket_force_disconnect(pSocket);
    socket_del_from_list(pSocket);
    pSocket->pRuntime = NULL;
    free(pSocketRuntime);
    pSocket->pTask = NULL;
    vTaskDelete(NULL);
}

/* Start / Re-start socket */
esp_err_t drv_socket_task(drv_socket_t* pSocket, int priority)
{
    if (pSocket == NULL) return ESP_FAIL;
    do
    {
        pSocket->bActiveTask = false;
    }while(pSocket->pTask != NULL);
    char* pTaskName = malloc(16);
    sprintf(pTaskName, "socket_%s",pSocket->cName);
    ESP_LOGI(TAG, "Creating Task %s", pTaskName);
    if (priority >= configMAX_PRIORITIES)
    {
        priority = configMAX_PRIORITIES - 1;
    }
    else if (priority < 0)
    {
        priority = 5;       /* use default priority */
    }
    xTaskCreate(socket_task, pTaskName, 4096, (void*)pSocket, priority, &pSocket->pTask);
    free(pTaskName);
    if (pSocket->pTask == NULL) return ESP_FAIL;
    return ESP_OK;
}

void drv_socket_init(void)
{

}