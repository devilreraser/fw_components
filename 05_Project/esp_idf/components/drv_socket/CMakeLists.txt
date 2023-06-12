idf_component_register(SRCS "drv_socket.c" "cmd_socket.c"
                    INCLUDE_DIRS "." 
                    REQUIRES    "lwip" 
                                "console" 
                                "esp_netif"
                                "esp_wifi" 
                                "drv_stream" 
                                "drv_console" 
                                "drv_version"
                                "drv_eth" 
                                "drv_wifi" 
                                "drv_dns"
                                      )
                 

