/* *****************************************************************************
 * File:   drv_dns.c
 * Author: XX
 *
 * Created on YYYY MM DD
 * 
 * Description: ...
 * 
 **************************************************************************** */

/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include "drv_dns.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"

#include "lwip/netdb.h"

#include "lwip/dns.h"

/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "drv_dns"

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
bool bInitializedDNS = false;

SemaphoreHandle_t flag_dns_busy = NULL;

ip_addr_t ip_addr_found;

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */

void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    bool *pbFound = (bool*)callback_arg;
    if (ipaddr == NULL)
    {
        ESP_LOGE(TAG, "DNS failed to resolve URL %s to a valid IP Address", name);
        if (pbFound != NULL) *pbFound = false;
    }
    else
    {
        memcpy(&ip_addr_found, ipaddr, sizeof(ip_addr_t));
        //ip_addr_found.u_addr = ipaddr->u_addr;
        char cTempIP[16] = {0};
        inet_ntoa_r(ip_addr_found, cTempIP, sizeof(cTempIP));
        ESP_LOGI(TAG, "DNS resolve URL %s to IP Address: %s", name, cTempIP);
        if (pbFound != NULL) *pbFound = true;
    }
}

bool drv_dns_resolve(char* cName, char* cResolveIP, size_t nResolveIPSize, bool* waitResolve)
{
    ip_addr_t ip_addr_resolved;

    bool bURLResolved;

    xSemaphoreTake(flag_dns_busy, portMAX_DELAY);
    printf("Get IP for URL: %s\n", cName );
    
    if (bInitializedDNS == false)
    {
        bInitializedDNS = true;
        dns_init();
        #ifdef dns_clear_cache
        dns_clear_cache();
        #endif
    }
    else
    {
        #ifdef dns_clear_cache
        dns_clear_cache();
        #else
        dns_init();
        #endif
    }
    bURLResolved = false;
    err_t resultDNS = dns_gethostbyname(cName, &ip_addr_resolved, dns_found_cb, &bURLResolved);

    if (resultDNS == ERR_OK)
    {
        /* use ip_addr_resolved from cache */
        
        inet_ntoa_r(ip_addr_resolved, cResolveIP, nResolveIPSize);
        ESP_LOGI(TAG, "Resolved from cache URL %s to ip address: %s", cName, cResolveIP);

        //memcpy(p_socket->cHostIP, cResolveIP, sizeof(p_socket->cHostIP));
        bURLResolved = true;
    }
    else if (resultDNS == ERR_INPROGRESS)
    {
        int delay_ms = 0;
        do
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            delay_ms += 100;
        } while ((bURLResolved == false) && (*waitResolve == true) && (delay_ms < 60000));

        if (bURLResolved)
        {
            memset(cResolveIP, 0, nResolveIPSize);
            memcpy(&ip_addr_resolved, &ip_addr_found, sizeof(ip_addr_t));
            inet_ntoa_r(ip_addr_resolved, cResolveIP, nResolveIPSize);
            ESP_LOGI(TAG, "Resolved (just now for %d ms) URL %s to ip address: %s", delay_ms, cName, cResolveIP);
        }
    }
    xSemaphoreGive(flag_dns_busy);

    return bURLResolved;
}

void drv_dns_init(void)
{
    flag_dns_busy = xSemaphoreCreateBinary();
    xSemaphoreGive(flag_dns_busy);
}
