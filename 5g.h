#ifndef _5G_H_
#define _5G_H_

#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <cstring>
#include <stdio.h>

#include "LDPC.h"  //  Важно: этот include должен быть здесь

typedef struct {
    int fd;
    int baudrate;
    std::string apn;
    std::string imei;
    std::string imsi;
    int rssi;
} fiveg_connection_t;

#define FIVEG_SUCCESS 0
#define FIVEG_ERROR_GENERAL -1
#define FIVEG_ERROR_NO_SIGNAL -2
#define FIVEG_ERROR_NOT_CONNECTED -3

// Функция для отправки AT-команды модему
int send_at_command(fiveg_connection_t* connection, const char* command, char* response, int response_size);

// --- Реализация функций ---

// ... (Реализация функций fiveg_get_signal_strength_impl, fiveg_get_network_operator_impl, 
// fiveg_get_ip_address_impl, fiveg_get_imei_impl, fiveg_get_imsi_impl, fiveg_connect_impl, fiveg_send_data_impl) ...

// Проверка состояния модема
int fiveg_check_modem_status_impl(fiveg_connection_t* connection) {
    char command[] = "AT+CSQ\r";
    char response[256];
    int result = send_at_command(connection, command, response, sizeof(response));
    if (result != FIVEG_SUCCESS) {
        return result;
    }

    // Парсинг ответа (пример, проверьте формат ответа в документации)
    int rssi, ber;
    if (sscanf(response, "+CSQ: %d,%d", &rssi, &ber) == 2) {
        printf("Сила сигнала (RSSI): %d, BER: %d\n", rssi, ber);
        return FIVEG_SUCCESS;
    } else {
        return FIVEG_ERROR_GENERAL;
    }
}

// Получение информации о сети
int fiveg_get_network_info_impl(fiveg_connection_t* connection) {
    char command[] = "AT+COPS?\r";
    char response[256];
    return send_at_command(connection, command, response, sizeof(response));
}

// Настройка APN
int fiveg_set_apn_impl(fiveg_connection_t* connection, const char* apn, const char* username, const char* password) {
    char command[256];
    snprintf(command, sizeof(command), "AT+CSTT=\"%s\",\"%s\",\"%s\"\r", apn, username, password);
    char response[256];
    return send_at_command(connection, command, response, sizeof(response));
}

// Запуск подключения
int fiveg_start_connection_impl(fiveg_connection_t* connection) {
    char command[] = "AT+CIICR\r";
    char response[256];
    return send_at_command(connection, command, response, sizeof(response));
}

// Получение IP-адреса (через AT+CIFSR)
int fiveg_get_ip_address_cifsr_impl(fiveg_connection_t* connection) {
    char command[] = "AT+CIFSR\r";
    char response[256];
    return send_at_command(connection, command, response, sizeof(response));
}

// Отключение от сети (через AT+CGATT=0)
int fiveg_deactivate_network_impl(fiveg_connection_t* connection) {
    char command[] = "AT+CGATT=0\r";
    char response[256];
    return send_at_command(connection, command, response, sizeof(response));
}

// Проверка статуса подключения
int fiveg_check_connection_status_impl(fiveg_connection_t* connection) {
    char command[] = "AT+CGACT?\r";
    char response[256];
    return send_at_command(connection, command, response, sizeof(response));
}

// Перезагрузка модема
int fiveg_restart_modem_impl(fiveg_connection_t* connection) {
    char command[] = "AT+ZRESTART\r";
    char response[256];
    return send_at_command(connection, command, response, sizeof(response));
}

// --- Макросы для вызова функций ---

#define fiveg_get_signal_strength(connection, signal_strength) fiveg_get_signal_strength_impl(connection, signal_strength)
#define fiveg_get_network_operator(connection, operator_name, buffer_size) fiveg_get_network_operator_impl(connection, operator_name, buffer_size)
#define fiveg_get_ip_address(connection, ip_address, buffer_size) fiveg_get_ip_address_impl(connection, ip_address, buffer_size)
#define fiveg_get_imei(connection, imei, buffer_size) fiveg_get_imei_impl(connection, imei, buffer_size)
#define fiveg_get_imsi(connection, imsi, buffer_size) fiveg_get_imsi_impl(connection, imsi, buffer_size)
#define fiveg_connect(connection) fiveg_connect_impl(connection)
#define fiveg_send_data(connection, data, data_length) fiveg_send_data_impl(connection, data, data_length)

// --- Новые макросы для добавленных функций ---

#define fiveg_check_modem_status(connection) fiveg_check_modem_status_impl(connection)
#define fiveg_get_network_info(connection) fiveg_get_network_info_impl(connection)
#define fiveg_set_apn(connection, apn, username, password) fiveg_set_apn_impl(connection, apn, username, password)
#define fiveg_start_connection(connection) fiveg_start_connection_impl(connection)
#define fiveg_get_ip_address_cifsr(connection) fiveg_get_ip_address_cifsr_impl(connection)
#define fiveg_deactivate_network(connection) fiveg_deactivate_network_impl(connection)
#define fiveg_check_connection_status(connection) fiveg_check_connection_status_impl(connection)
#define fiveg_restart_modem(connection) fiveg_restart_modem_impl(connection)

#endif // _5G_H_