#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"

// LWIP includes 
#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/dns.h"
#include "lwip/ip.h"
#include "lwip/ip_addr.h" 
#include "lwip/prot/ip4.h"

// Standard C headers
#include <string.h>
#include <stdio.h>

// Defines Sensores
#define BUTTON1_PIN 5
#define BUTTON2_PIN 6
#define JOY_X_PIN 26
#define JOY_Y_PIN 27

// Defines Wifi
#define WIFI_SSID "WIFI_SSID"
#define WIFI_PASS "WIFI_PASS"

// Limiares para o joystick
#define JOY_CENTER_MIN 2000
#define JOY_CENTER_MAX 2100
#define JOY_THRESHOLD_HIGH 3000
#define JOY_THRESHOLD_LOW 1000

// Chave de API do ThingSpeak e Hostname
#define THINGSPEAK_API_KEY "YNRAWU8A5GU3EJ22"
#define THINGSPEAK_HOST "api.thingspeak.com"
#define THINGSPEAK_PORT 80 // Porta HTTP padrão

// Global variables (standard types first)
char button1_message[50] = "Nenhum evento no botão 1";
char button2_message[50] = "Nenhum evento no botão 2";
char joystick_direction[20] = "Centro";
char http_response[1024];

uint16_t joystick_x = 0;
uint16_t joystick_y = 0;

// Variáveis globais para o ThingSpeak client
static struct altcp_pcb *thingspeak_client_pcb = NULL;
static ip_addr_t thingspeak_server_ip;
static volatile bool dns_resolved = false;
static volatile bool thingspeak_request_in_progress = false;

static void app_dns_found_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    if (ipaddr) {
        printf("DNS resolved for %s: %s\n", name, ip4addr_ntoa(ipaddr)); // FIX: Use ip4addr_ntoa
        ip_addr_copy(thingspeak_server_ip, *ipaddr);
        dns_resolved = true;
    } else {
        printf("DNS lookup failed for %s\n", name);
        dns_resolved = false;
        thingspeak_request_in_progress = false; 
    }
}

static err_t thingspeak_client_connected(void *arg, struct altcp_pcb *tpcb, err_t err) {
    if (err == ERR_OK) {
        printf("Conectado ao ThingSpeak.\n");

        thingspeak_request_in_progress = true;

        char request_buffer[512];

        int dir_code = 0;
        if (strcmp(joystick_direction, "Norte") == 0) dir_code = 1;
        else if (strcmp(joystick_direction, "Nordeste") == 0) dir_code = 2;
        else if (strcmp(joystick_direction, "Leste") == 0) dir_code = 3;
        else if (strcmp(joystick_direction, "Sudeste") == 0) dir_code = 4;
        else if (strcmp(joystick_direction, "Sul") == 0) dir_code = 5;
        else if (strcmp(joystick_direction, "Sudoeste") == 0) dir_code = 6;
        else if (strcmp(joystick_direction, "Oeste") == 0) dir_code = 7;
        else if (strcmp(joystick_direction, "Noroeste") == 0) dir_code = 8;

        int btn1_state = !gpio_get(BUTTON1_PIN);
        int btn2_state = !gpio_get(BUTTON2_PIN);

        snprintf(request_buffer, sizeof(request_buffer),
                 "GET /update?api_key=%s&field1=%d&field2=%d&field3=%d&field4=%d&field5=%d HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                 THINGSPEAK_API_KEY,
                 btn1_state,
                 btn2_state,
                 joystick_x,
                 joystick_y,
                 dir_code,
                 THINGSPEAK_HOST);

        altcp_write(tpcb, request_buffer, strlen(request_buffer), TCP_WRITE_FLAG_COPY);
        altcp_output(tpcb);

    } else {
        printf("Falha ao conectar ao ThingSpeak: %d\n", err);
        altcp_close(tpcb);
        thingspeak_client_pcb = NULL;
        thingspeak_request_in_progress = false;
    }
    return ERR_OK;
}

static err_t thingspeak_client_recv(void *arg, struct altcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (err == ERR_OK && p != NULL) {
        printf("Resposta ThingSpeak:\n%.*s\n", p->len, (char*)p->payload);
        pbuf_free(p);
    } else if (err == ERR_OK && p == NULL) {
        printf("Conexão ThingSpeak fechada.\n");
        altcp_close(tpcb);
        thingspeak_client_pcb = NULL;
        thingspeak_request_in_progress = false;
    }
    return ERR_OK;
}

static void thingspeak_client_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        printf("Erro ThingSpeak client: %d\n", err);
    }
    thingspeak_client_pcb = NULL;
    thingspeak_request_in_progress = false;
}

void send_to_thingspeak() {
    
    if (thingspeak_request_in_progress) {
        printf("Requisição ThingSpeak em andamento, aguardando...\n");
        return;
    }

    if (!dns_resolved) {
        printf("Tentando resolver DNS para ThingSpeak...\n");
        dns_gethostbyname(THINGSPEAK_HOST, &thingspeak_server_ip, app_dns_found_callback, NULL);
        return;
    }

    if (thingspeak_client_pcb == NULL) {
        thingspeak_client_pcb = altcp_tcp_new_ip_type(IPADDR_TYPE_V4);
        if (thingspeak_client_pcb) {
            altcp_arg(thingspeak_client_pcb, NULL);
            altcp_sent(thingspeak_client_pcb, NULL);
            altcp_recv(thingspeak_client_pcb, thingspeak_client_recv);
            altcp_err(thingspeak_client_pcb, thingspeak_client_err);

            err_t err = altcp_connect(thingspeak_client_pcb, &thingspeak_server_ip, THINGSPEAK_PORT, thingspeak_client_connected);
            if (err != ERR_OK) {
                printf("Erro ao iniciar conexão com ThingSpeak: %d\n", err);
                altcp_close(thingspeak_client_pcb);
                thingspeak_client_pcb = NULL;
                thingspeak_request_in_progress = false;
            }
        } else {
            printf("Erro: Não foi possível criar PCB para ThingSpeak.\n");
            thingspeak_request_in_progress = false;
        }
    } else {
        printf("Conexão ThingSpeak já ativa, aguardando fechamento...\n");
    }
}

void read_joystick() {
    adc_select_input(0);
    joystick_x = adc_read();

    adc_select_input(1);
    joystick_y = adc_read();
}

void get_joystick_direction() {
    bool is_center_x = (joystick_x >= JOY_CENTER_MIN && joystick_x <= JOY_CENTER_MAX);
    bool is_center_y = (joystick_y >= JOY_CENTER_MIN && joystick_y <= JOY_CENTER_MAX);

    if (is_center_x && is_center_y) {
        strcpy(joystick_direction, "Centro");
    }
    else if (joystick_y < JOY_THRESHOLD_LOW && joystick_x > JOY_THRESHOLD_HIGH) { // Y baixo (Norte) e X alto (Leste)
        strcpy(joystick_direction, "Noroeste");
    }
    else if (joystick_y < JOY_THRESHOLD_LOW && joystick_x < JOY_THRESHOLD_LOW) {  // Y baixo (Norte) e X baixo (Oeste)
        strcpy(joystick_direction, "Sudoeste");
    }
    else if (joystick_y > JOY_THRESHOLD_HIGH && joystick_x > JOY_THRESHOLD_HIGH) { // Y alto (Sul) e X alto (Leste)
        strcpy(joystick_direction, "Nordeste");
    }
    else if (joystick_y > JOY_THRESHOLD_HIGH && joystick_x < JOY_THRESHOLD_LOW) {  // Y alto (Sul) e X baixo (Oeste)
        strcpy(joystick_direction, "Sudeste");
    }
    else if (joystick_y < JOY_THRESHOLD_LOW) {
        strcpy(joystick_direction, "Oeste");
    }
    else if (joystick_y > JOY_THRESHOLD_HIGH) {
        strcpy(joystick_direction, "Leste");
    }
    else if (joystick_x > JOY_THRESHOLD_HIGH) {
        strcpy(joystick_direction, "Norte");
    }
    else if (joystick_x < JOY_THRESHOLD_LOW) {
        strcpy(joystick_direction, "Sul");
    }
    else {
        strcpy(joystick_direction, "Centro");
    }
}

void create_http_response() {
    snprintf(http_response, sizeof(http_response),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n"
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Controle dos Botões</title>"
        "<script>"
        "function autoRefresh() {"
        "  window.location.href = '/update';"
        "}"
        "setInterval(autoRefresh, 1000);"
        "</script>"
        "</head>"
        "<body>"
        "<h1>Controle dos Botões</h1>"
        "<h2>Estado dos Botões:</h2>"
        "<p>Botão 1: %s</p>"
        "<p>Botão 2: %s</p>"
        "<h2>Joystick:</h2>"
        "<p>X: %d</p>"
        "<p>Y: %d</p>"
        "<p>Direção: %s</p>"
        "</body>"
        "</html>\r\n",
        button1_message, button2_message, joystick_x, joystick_y, joystick_direction);
}

static err_t http_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        tcp_close(tpcb);
        return ERR_OK;
    }
    char *request = (char *)p->payload;

    create_http_response();
    tcp_write(tpcb, http_response, strlen(http_response), TCP_WRITE_FLAG_COPY);
    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, http_callback);
    return ERR_OK;
}

static void start_http_server(void) {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return;
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) return;
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
}

void monitor_buttons() {
    static bool button1_last_state = false;
    static bool button2_last_state = false;

    bool button1_state = !gpio_get(BUTTON1_PIN);
    bool button2_state = !gpio_get(BUTTON2_PIN);

    if (button1_state != button1_last_state) {
        button1_last_state = button1_state;
        if (button1_state) {
            snprintf(button1_message, sizeof(button1_message), "Botão 1 foi pressionado!");
            printf("Botão 1 pressionado\n");
        } else {
            snprintf(button1_message, sizeof(button1_message), "Botão 1 foi solto!");
            printf("Botão 1 solto\n");
        }
    }

    if (button2_state != button2_last_state) {
        button2_last_state = button2_state;
        if (button2_state) {
            snprintf(button2_message, sizeof(button2_message), "Botão 2 foi pressionado!");
            printf("Botão 2 pressionado\n");
        } else {
            snprintf(button2_message, sizeof(button2_message), "Botão 2 foi solto!");
            printf("Botão 2 solto\n");
        }
    }
}

int main() {
    stdio_init_all();
    sleep_ms(5000);
    printf("Iniciando servidor HTTP\n");

    if (cyw43_arch_init()) {
        printf("Erro ao inicializar o Wi-Fi\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    printf("Conectando ao Wi-Fi...\n");

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        return 1;
    }else {
        printf("Connected.\n");
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("Endereço IP %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    printf("Wi-Fi conectado!\n");

    #define LED_PIN 12
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_init(BUTTON1_PIN);
    gpio_set_dir(BUTTON1_PIN, GPIO_IN);
    gpio_pull_up(BUTTON1_PIN);
    gpio_init(BUTTON2_PIN);
    gpio_set_dir(BUTTON2_PIN, GPIO_IN);
    gpio_pull_up(BUTTON2_PIN);

    adc_init();
    adc_gpio_init(JOY_X_PIN);
    adc_gpio_init(JOY_Y_PIN);

    start_http_server();

    int thingspeak_send_counter = 0;

    while (true) {
        cyw43_arch_poll();
        monitor_buttons();
        read_joystick();
        get_joystick_direction();
        send_to_thingspeak();
        sleep_ms(1000);
    }

    cyw43_arch_deinit();
    return 0;
}
