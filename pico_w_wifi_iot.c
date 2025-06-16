// pico_w_wifi_complete_example.c
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>

#define BUTTON1_PIN 5
#define BUTTON2_PIN 6
#define JOY_X_PIN 26
#define JOY_Y_PIN 27
#define WIFI_SSID "WIFI_SSID"
#define WIFI_PASS "WIFI_PASS"

// Limiares para o joystick
#define JOY_CENTER_MIN 2000
#define JOY_CENTER_MAX 2100
#define JOY_THRESHOLD_HIGH 3000
#define JOY_THRESHOLD_LOW 1000

char button1_message[50] = "Nenhum evento no botão 1";
char button2_message[50] = "Nenhum evento no botão 2";
char joystick_direction[20] = "Centro";
char http_response[1024];

uint16_t joystick_x = 0;
uint16_t joystick_y = 0;

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
        "  window.location.href = '/update';" // Redireciona para a rota /update
        "}"
        "setInterval(autoRefresh, 1000);" // Chama autoRefresh a cada 1000ms (1 segundo)
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

    bool button1_state = !gpio_get(BUTTON1_PIN); // Botão pressionado = LOW
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

    // Inicializa o Wi-Fi
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
        // Read the ip address in a human readable way
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("Endereço IP %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    printf("Wi-Fi conectado!\n");

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

    while (true) {
        cyw43_arch_poll();
        monitor_buttons();
        read_joystick();
        get_joystick_direction(); 
        sleep_ms(1000); //a cada 1 segundo
    }

    cyw43_arch_deinit();
    return 0;
} 