#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "config.h"
#include "protocol_examples_common.h"

/** DEFINES **/
#define WIFI_SUCCESS 1 << 0
#define WIFI_FAILURE 1 << 1
#define TCP_SUCCESS 1 << 0
#define TCP_FAILURE 1 << 1
#define MAX_FAILURES 10
#define PORT 3000
#define IP_ADDRESS "10.0.0.181"
#define BUFFER_SIZE 1024
#define BLINK_LED 2

// event group to contain status information
static EventGroupHandle_t wifi_event_group;
// retry tracker
static int s_retry_num = 0;

// task tag
static const char *TAG = "WIFI";

//event handler for wifi events
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		ESP_LOGI(TAG, "Connecting to AP...");
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		if (s_retry_num < MAX_FAILURES)
		{
			ESP_LOGI(TAG, "Reconnecting to AP...");
			esp_wifi_connect();
			s_retry_num++;
		} else {
			xEventGroupSetBits(wifi_event_group, WIFI_FAILURE);
		}
	}
}

//event handler for ip events
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
	if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);
    }
}

// connect to the server and return the result
esp_err_t connect_tcp_server(void)
{
	int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    char buffer[BUFFER_SIZE];
    // char *response = "Hello from ESP32!\n";

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Set up server address
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    // Bind socket to address and port
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    if (listen(server_socket, 1) == -1) {
        perror("Error listening for connections");
        exit(EXIT_FAILURE);
    }

    printf("Listening on port %d...\n", PORT);

    while (1) {
        // Accept connection
        socklen_t client_address_len = sizeof(client_address);
        client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
        if (client_socket == -1) {
            perror("Error accepting connection");
            exit(EXIT_FAILURE);
        }

        printf("Accepted connection from %s:%d\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

        // Send response to the client
        // if (send(client_socket, response, strlen(response), 0) == -1) {
        //     perror("Error sending response to the client");
        //     exit(EXIT_FAILURE);
        // }

        // bzero(buffer, sizeof(buffer));
        int r = -1;

        r = read(client_socket, buffer, sizeof(buffer)-1);
        if(r > 0) 
        {
            printf("message: %s\n",buffer);
            r = -1;

            if(strstr(buffer, "TURN_ON_LED"))
            {
                // write(client_socket, on_msg, strlen(on_msg));
                gpio_set_level(BLINK_LED, 1);
            }
            else
            {
                // write(client_socket, off_msg, strlen(off_msg));
                gpio_set_level(BLINK_LED, 0);
            }

        }
        // Close the connection
        close(client_socket);
    }

    // Close the server socket
    close(server_socket);

    return TCP_SUCCESS;
}

void app_main(void)
{
    esp_err_t status = WIFI_FAILURE;
    // Connect to WiFi     
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    status = connect_tcp_server();
	if (TCP_SUCCESS != status)
	{
		ESP_LOGI(TAG, "Failed to connect to remote server, dying...");
		return;
	}
}
