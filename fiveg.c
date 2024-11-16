/*
 * Copyright © 2024-2024 ZenithOS Open Source Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file fivegcrC2.c
 *
 * This file contains the implementation of a simplified 5G driver for
 * interacting with 5G modems and managing network connections.
 *
 * Driver version: 2.7 - current
 * Register writing functionality available up to version 6.8 of Linux.
 *
 * This software is licensed under the MIT License.

  * This driver is under development and has been tested on 3 devices.
 * It may not even transmit signals yet! But there are many more tests to come...
 *
 * Developed by NE5LINK (znts543@gmail.com)

 * Due to limitations, we cannot include <skbuff.h>, which adds complexity to the implementation.
 *
 * We welcome forks and contributions to this project!  Feel free to contribute.

 */



#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h> // Для работы с потоками
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/rtnetlink.h>
#include <stdio.h>
#include <net/route.h>
#include <linux/qrtr.h> // QRTR для взаимодействия с Qualcomm-модемами
#include <linux/netlink.h> // Netlink для низкоуровневого взаимодействия с ядром
#include <linux/if_link.h>
#include <linux/if_addr.h>
#include <linux/if_tun.h>
#include <linux/selinux_netlink.h>
#include <time.h>       // Для time()
#include <linux/module.h>

// Определение размеров матрицы для LDPC
#define N 12 // Количество бит
#define M 6  // Количество проверок

#define NETLINK_QRTR 16



// This might be removed as u16 is not working on my compiler 
// for some reason. Might be deleted in future versions. ↓↓

#ifndef __u16_defined
#define __u16_defined
typedef unsigned short u16;
#endif

// Структура для хранения данных драйвера
typedef struct {
    int udpSocket;
    struct sockaddr_in serverAddr;
    int rtnetlinkSocket;
    pthread_mutex_t mtx; // Мьютекс для синхронизации
    int isConnected;
    char imsi[20]; // Фиксированный размер для IMSI
    char apn[50];  // Фиксированный размер для APN
    uint32_t gtpTeid;
    char iccid[50];
    int ifindex; // Индекс виртуального интерфейса
    int qrtrSocket; // Сокет для QRTR
    int tp;
    int tun_fd
} Simple5GDriver;

#include <stdio.h>
#include <linux/version.h>

#define SIOCSMIIREG 0x89f0


// Функции для работы с драйвером
Simple5GDriver* Simple5GDriver_create();
void Simple5GDriver_destroy(Simple5GDriver* driver);

void Simple5GDriver_sendData(Simple5GDriver* driver, const char* data);
void Simple5GDriver_receiveData(Simple5GDriver* driver);
void Simple5GDriver_handleResponse(Simple5GDriver* driver, const char* response);
void Simple5GDriver_logError(const char* message);
void Simple5GDriver_setupGTP(Simple5GDriver* driver);
void Simple5GDriver_configureVLAN(Simple5GDriver* driver, const char* bondInterface, int vlanId);
void Simple5GDriver_configureBonding(Simple5GDriver* driver, char** slaveInterfaces, int numInterfaces);
int Simple5GDriver_createVirtualInterface(Simple5GDriver* driver, const char *devname);
int Simple5GDriver_addRoute(Simple5GDriver* driver, const char *dest, const char *gateway, const char *dev);
void Simple5GDriver_establish5GConnection(Simple5GDriver* driver);
void Simple5GDriver_connectTo5G(Simple5GDriver* driver);
void Simple5GDriver_fetchICCID(Simple5GDriver* driver);
void* Simple5GDriver_startDataUpdate(void* arg); // Функция для потока
void Simple5GDriver_sendUserData(Simple5GDriver* driver);
void Simple5GDriver_optimizeNetwork(Simple5GDriver* driver);
void Simple5GDriver_createNetworkSlice(Simple5GDriver* driver, const char* sliceName, int bandwidth);
void Simple5GDriver_deployEdgeApplication(Simple5GDriver* driver, const char* appName);
void Simple5GDriver_enableBeamforming(Simple5GDriver* driver, const char* userId);

void Simple5GDriver_logError(const char* message) {
    fprintf(stderr, "Error: %s\n", message);
}
int get5GSignalStrength(const char* interfaceName);
void blockICMP();


static const u16 phy_mcu_ram_code_8126a_1_1[] = {
        0xa436, 0x8023, 0xa438, 0x4900, 0xa436, 0xB82E, 0xa438, 0x0001,
        0xBFBA, 0xE000, 0xBF1A, 0xC1B9, 0xBFA8, 0x10F0, 0xBFB0, 0x0210,
        0xBFB4, 0xE7E4, 0xb820, 0x0090, 0xa436, 0xA016, 0xa438, 0x0000,
        0xa436, 0xA012, 0xa438, 0x0000, 0xa436, 0xA014, 0xa438, 0x1800,
        0xa438, 0x8010, 0xa438, 0x1800, 0xa438, 0x8062, 0xa438, 0x1800,
        0xa438, 0x8069, 0xa438, 0x1800, 0xa438, 0x80e2, 0xa438, 0x1800,
        0xa438, 0x80eb, 0xa438, 0x1800, 0xa438, 0x80f5, 0xa438, 0x1800,
        0xa438, 0x811b, 0xa438, 0x1800, 0xa438, 0x8120, 0xa438, 0xd500,
        0xa438, 0xd049, 0xa438, 0xd1b9, 0xa438, 0xa208, 0xa438, 0x8208,
        0xa438, 0xd503, 0xa438, 0xa104, 0xa438, 0x0c07, 0xa438, 0x0902,
        0xa438, 0xd500, 0xa438, 0xbc10, 0xa438, 0xc484, 0xa438, 0xd503,
        0xa438, 0xcc02, 0xa438, 0xcd0d, 0xa438, 0xaf01, 0xa438, 0xd500,
        0xa438, 0xd703, 0xa438, 0x4531, 0xa438, 0xbd08, 0xa438, 0x1000,
        0xa438, 0x16bb, 0xa438, 0xd75e, 0xa438, 0x5fb3, 0xa438, 0xd503,
        0xa438, 0xd04d, 0xa438, 0xd1c7, 0xa438, 0x0cf0, 0xa438, 0x0e10,
        0xa438, 0xd704, 0xa438, 0x5ffc, 0xa438, 0xd04d, 0xa438, 0xd1c7,
        0xa438, 0x0cf0, 0xa438, 0x0e20, 0xa438, 0xd704, 0xa438, 0x5ffc,
        0xa438, 0xd04d, 0xa438, 0xd1c7, 0xa438, 0x0cf0, 0xa438, 0x0e40,
        0xa438, 0xd704, 0xa438, 0x5ffc, 0xa438, 0xd04d, 0xa438, 0xd1c7,
        0xa438, 0x0cf0, 0xa438, 0x0e80, 0xa438, 0xd704, 0xa438, 0x5ffc,
        0xa438, 0xd07b, 0xa438, 0xd1c5, 0xa438, 0x8ef0, 0xa438, 0xd704,
        0xa438, 0x5ffc, 0xa438, 0x9d08, 0xa438, 0x1000, 0xa438, 0x16bb,
        0xa438, 0xd75e, 0xa438, 0x7fb3, 0xa438, 0x1000, 0xa438, 0x16bb,
        0xa438, 0xd75e, 0xa438, 0x5fad, 0xa438, 0x1000, 0xa438, 0x181f,
        0xa438, 0xd703, 0xa438, 0x3181, 0xa438, 0x8059, 0xa438, 0x60ad,
        0xa438, 0x1000, 0xa438, 0x16bb, 0xa438, 0xd703, 0xa438, 0x5fbb,
        0xa438, 0x1000, 0xa438, 0x16bb, 0xa438, 0xd719, 0xa438, 0x7fa8,
        0xa438, 0xd500, 0xa438, 0xd049, 0xa438, 0xd1b9, 0xa438, 0x1800,
        0xa438, 0x0f0b, 0xa438, 0xd500, 0xa438, 0xd07b, 0xa438, 0xd1b5,
        0xa438, 0xd0f6, 0xa438, 0xd1c5, 0xa438, 0x1800, 0xa438, 0x1049,
        0xa438, 0xd707, 0xa438, 0x4121, 0xa438, 0xd706, 0xa438, 0x40fa,
        0xa438, 0xd099, 0xa438, 0xd1c6, 0xa438, 0x1000, 0xa438, 0x16bb,
        0xa438, 0xd704, 0xa438, 0x5fbc, 0xa438, 0xbc80, 0xa438, 0xc489,
        0xa438, 0xd503, 0xa438, 0xcc08, 0xa438, 0xcd46, 0xa438, 0xaf01,
        0xa438, 0xd500, 0xa438, 0x1000, 0xa438, 0x0903, 0xa438, 0x1000,
        0xa438, 0x16bb, 0xa438, 0xd75e, 0xa438, 0x5f6d, 0xa438, 0x1000,
        0xa438, 0x181f, 0xa438, 0xd504, 0xa438, 0xa210, 0xa438, 0xd500,
        0xa438, 0x1000, 0xa438, 0x16bb, 0xa438, 0xd719, 0xa438, 0x5fbc,
        0xa438, 0xd504, 0xa438, 0x8210, 0xa438, 0xd503, 0xa438, 0xc6d0,
        0xa438, 0xa521, 0xa438, 0xcd49, 0xa438, 0xaf01, 0xa438, 0xd504,
        0xa438, 0xa220, 0xa438, 0xd500, 0xa438, 0x1000, 0xa438, 0x16bb,
        0xa438, 0xd75e, 0xa438, 0x5fad, 0xa438, 0x1000, 0xa438, 0x181f,
        0xa438, 0xd503, 0xa438, 0xa704, 0xa438, 0x0c07, 0xa438, 0x0904,
        0xa438, 0xd504, 0xa438, 0xa102, 0xa438, 0xd500, 0xa438, 0x1000,
        0xa438, 0x16bb, 0xa438, 0xd718, 0xa438, 0x5fab, 0xa438, 0xd503,
        0xa438, 0xc6f0, 0xa438, 0xa521, 0xa438, 0xd505, 0xa438, 0xa404,
        0xa438, 0xd500, 0xa438, 0xd701, 0xa438, 0x6085, 0xa438, 0xd504,
        0xa438, 0xc9f1, 0xa438, 0xf003, 0xa438, 0xd504, 0xa438, 0xc9f0,
        0xa438, 0xd503, 0xa438, 0xcd4a, 0xa438, 0xaf01, 0xa438, 0xd500,
        0xa438, 0xd504, 0xa438, 0xa802, 0xa438, 0xd500, 0xa438, 0x1000,
        0xa438, 0x16bb, 0xa438, 0xd707, 0xa438, 0x5fb1, 0xa438, 0xd707,
        0xa438, 0x5f10, 0xa438, 0xd505, 0xa438, 0xa402, 0xa438, 0xd503,
        0xa438, 0xd707, 0xa438, 0x41a1, 0xa438, 0xd706, 0xa438, 0x60ba,
        0xa438, 0x60fc, 0xa438, 0x0c07, 0xa438, 0x0204, 0xa438, 0xf009,
        0xa438, 0x0c07, 0xa438, 0x0202, 0xa438, 0xf006, 0xa438, 0x0c07,
        0xa438, 0x0206, 0xa438, 0xf003, 0xa438, 0x0c07, 0xa438, 0x0202,
        0xa438, 0xd500, 0xa438, 0xd703, 0xa438, 0x3181, 0xa438, 0x80e0,
        0xa438, 0x616d, 0xa438, 0xd701, 0xa438, 0x6065, 0xa438, 0x1800,
        0xa438, 0x1229, 0xa438, 0x1000, 0xa438, 0x16bb, 0xa438, 0xd707,
        0xa438, 0x6061, 0xa438, 0xd704, 0xa438, 0x5f7c, 0xa438, 0x1800,
        0xa438, 0x124a, 0xa438, 0xd504, 0xa438, 0x8c0f, 0xa438, 0xd505,
        0xa438, 0xa20e, 0xa438, 0xd500, 0xa438, 0x1000, 0xa438, 0x1871,
        0xa438, 0x1800, 0xa438, 0x1899, 0xa438, 0xd70b, 0xa438, 0x60b0,
        0xa438, 0xd05a, 0xa438, 0xd19a, 0xa438, 0x1800, 0xa438, 0x1aef,
        0xa438, 0xd0ef, 0xa438, 0xd19a, 0xa438, 0x1800, 0xa438, 0x1aef,
        0xa438, 0x1000, 0xa438, 0x1d09, 0xa438, 0xd708, 0xa438, 0x3399,
        0xa438, 0x1b63, 0xa438, 0xd709, 0xa438, 0x5f5d, 0xa438, 0xd70b,
        0xa438, 0x6130, 0xa438, 0xd70d, 0xa438, 0x6163, 0xa438, 0xd709,
        0xa438, 0x430b, 0xa438, 0xd71e, 0xa438, 0x62c2, 0xa438, 0xb401,
        0xa438, 0xf014, 0xa438, 0xc901, 0xa438, 0x1000, 0xa438, 0x810e,
        0xa438, 0xf010, 0xa438, 0xc902, 0xa438, 0x1000, 0xa438, 0x810e,
        0xa438, 0xf00c, 0xa438, 0xce04, 0xa438, 0xcf01, 0xa438, 0xd70a,
        0xa438, 0x5fe2, 0xa438, 0xce04, 0xa438, 0xcf02, 0xa438, 0xc900,
        0xa438, 0xd70a, 0xa438, 0x4057, 0xa438, 0xb401, 0xa438, 0x0800,
        0xa438, 0x1800, 0xa438, 0x1b5d, 0xa438, 0xa480, 0xa438, 0xa2b0,
        0xa438, 0xa806, 0xa438, 0x1800, 0xa438, 0x225c, 0xa438, 0xa7e8,
        0xa438, 0xac08, 0xa438, 0x1800, 0xa438, 0x1a4e, 0xa436, 0xA026,
        0xa438, 0x1a4d, 0xa436, 0xA024, 0xa438, 0x225a, 0xa436, 0xA022,
        0xa438, 0x1b53, 0xa436, 0xA020, 0xa438, 0x1aed, 0xa436, 0xA006,
        0xa438, 0x1892, 0xa436, 0xA004, 0xa438, 0x11a4, 0xa436, 0xA002,
        0xa438, 0x103c, 0xa436, 0xA000, 0xa438, 0x0ea6, 0xa436, 0xA008,
        0xa438, 0xff00, 0xa436, 0xA016, 0xa438, 0x0000, 0xa436, 0xA012,
        0xa438, 0x0ff8, 0xa436, 0xA014, 0xa438, 0x0000, 0xa438, 0xD098,
        0xa438, 0xc483, 0xa438, 0xc483, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa436, 0xA152, 0xa438, 0x3fff,
        0xa436, 0xA154, 0xa438, 0x0413, 0xa436, 0xA156, 0xa438, 0x1A32,
        0xa436, 0xA158, 0xa438, 0x1CC0, 0xa436, 0xA15A, 0xa438, 0x3fff,
        0xa436, 0xA15C, 0xa438, 0x3fff, 0xa436, 0xA15E, 0xa438, 0x3fff,
        0xa436, 0xA160, 0xa438, 0x3fff, 0xa436, 0xA150, 0xa438, 0x000E,
        0xa436, 0xA016, 0xa438, 0x0020, 0xa436, 0xA012, 0xa438, 0x0000,
        0xa436, 0xA014, 0xa438, 0x1800, 0xa438, 0x8010, 0xa438, 0x1800,
        0xa438, 0x8021, 0xa438, 0x1800, 0xa438, 0x8037, 0xa438, 0x1800,
        0xa438, 0x803f, 0xa438, 0x1800, 0xa438, 0x8084, 0xa438, 0x1800,
        0xa438, 0x80c5, 0xa438, 0x1800, 0xa438, 0x80cc, 0xa438, 0x1800,
        0xa438, 0x80d5, 0xa438, 0xa00a, 0xa438, 0xa280, 0xa438, 0xa404,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x1800, 0xa438, 0x099b, 0xa438, 0x1000, 0xa438, 0x1021,
        0xa438, 0xd700, 0xa438, 0x5fab, 0xa438, 0xa208, 0xa438, 0x8204,
        0xa438, 0xcb38, 0xa438, 0xaa40, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x1800, 0xa438, 0x0b2a,
        0xa438, 0x82a0, 0xa438, 0x8404, 0xa438, 0xa110, 0xa438, 0xd706,
        0xa438, 0x4041, 0xa438, 0xa180, 0xa438, 0x1800, 0xa438, 0x0e7f,
        0xa438, 0x8190, 0xa438, 0xcb93, 0xa438, 0x1000, 0xa438, 0x0ef4,
        0xa438, 0xd704, 0xa438, 0x7fb8, 0xa438, 0xa008, 0xa438, 0xd706,
        0xa438, 0x4040, 0xa438, 0xa002, 0xa438, 0xd705, 0xa438, 0x4079,
        0xa438, 0x1000, 0xa438, 0x10ad, 0xa438, 0x0c03, 0xa438, 0x1502,
        0xa438, 0x85f0, 0xa438, 0x9503, 0xa438, 0xd705, 0xa438, 0x40d9,
        0xa438, 0xd70c, 0xa438, 0x6083, 0xa438, 0x0c1f, 0xa438, 0x0d09,
        0xa438, 0xf003, 0xa438, 0x0c1f, 0xa438, 0x0d0a, 0xa438, 0x0cc0,
        0xa438, 0x0d80, 0xa438, 0x1000, 0xa438, 0x104f, 0xa438, 0x1000,
        0xa438, 0x0ef4, 0xa438, 0x8020, 0xa438, 0xd705, 0xa438, 0x40d9,
        0xa438, 0xd704, 0xa438, 0x609f, 0xa438, 0xd70c, 0xa438, 0x6043,
        0xa438, 0x8504, 0xa438, 0xcb94, 0xa438, 0x1000, 0xa438, 0x0ef4,
        0xa438, 0xd706, 0xa438, 0x7fa2, 0xa438, 0x800a, 0xa438, 0x0c03,
        0xa438, 0x1502, 0xa438, 0x0cf0, 0xa438, 0x05a0, 0xa438, 0x9503,
        0xa438, 0xd705, 0xa438, 0x40b9, 0xa438, 0x0c1f, 0xa438, 0x0d00,
        0xa438, 0x8dc0, 0xa438, 0xf005, 0xa438, 0xa190, 0xa438, 0x0c1f,
        0xa438, 0x0d17, 0xa438, 0x8dc0, 0xa438, 0x1000, 0xa438, 0x104f,
        0xa438, 0xd705, 0xa438, 0x39cc, 0xa438, 0x0c7d, 0xa438, 0x1800,
        0xa438, 0x0e67, 0xa438, 0xcb96, 0xa438, 0x0c03, 0xa438, 0x1502,
        0xa438, 0xab05, 0xa438, 0xac04, 0xa438, 0xac08, 0xa438, 0x9503,
        0xa438, 0x0c1f, 0xa438, 0x0d00, 0xa438, 0x8dc0, 0xa438, 0x1000,
        0xa438, 0x104f, 0xa438, 0x1000, 0xa438, 0x1021, 0xa438, 0xd706,
        0xa438, 0x2215, 0xa438, 0x8099, 0xa438, 0x0c03, 0xa438, 0x1502,
        0xa438, 0xae02, 0xa438, 0x9503, 0xa438, 0xd706, 0xa438, 0x6451,
        0xa438, 0xd71f, 0xa438, 0x2e70, 0xa438, 0x0f00, 0xa438, 0xd706,
        0xa438, 0x3290, 0xa438, 0x80be, 0xa438, 0xd704, 0xa438, 0x2e70,
        0xa438, 0x8090, 0xa438, 0xd706, 0xa438, 0x339c, 0xa438, 0x8090,
        0xa438, 0x8718, 0xa438, 0x8910, 0xa438, 0x0c03, 0xa438, 0x1502,
        0xa438, 0xc500, 0xa438, 0x9503, 0xa438, 0x0c1f, 0xa438, 0x0d17,
        0xa438, 0x8dc0, 0xa438, 0x1000, 0xa438, 0x104f, 0xa438, 0x0c03,
        0xa438, 0x1502, 0xa438, 0x8c04, 0xa438, 0x9503, 0xa438, 0xa00a,
        0xa438, 0xa190, 0xa438, 0xa280, 0xa438, 0xa404, 0xa438, 0x1800,
        0xa438, 0x0f35, 0xa438, 0x1800, 0xa438, 0x0f07, 0xa438, 0x0c03,
        0xa438, 0x1502, 0xa438, 0x8c08, 0xa438, 0x8c04, 0xa438, 0x9503,
        0xa438, 0x1800, 0xa438, 0x0f02, 0xa438, 0x1000, 0xa438, 0x1021,
        0xa438, 0xd700, 0xa438, 0x5fb4, 0xa438, 0xaa10, 0xa438, 0x1800,
        0xa438, 0x0c6b, 0xa438, 0x82a0, 0xa438, 0x8406, 0xa438, 0x0c03,
        0xa438, 0x1502, 0xa438, 0xac04, 0xa438, 0x8602, 0xa438, 0x9503,
        0xa438, 0x1800, 0xa438, 0x0e09, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x8308, 0xa438, 0x0c03,
        0xa438, 0x1502, 0xa438, 0xc555, 0xa438, 0x9503, 0xa438, 0xa728,
        0xa438, 0x8440, 0xa438, 0x0c03, 0xa438, 0x0901, 0xa438, 0x8801,
        0xa438, 0xd700, 0xa438, 0x4040, 0xa438, 0xa801, 0xa438, 0xd701,
        0xa438, 0x4052, 0xa438, 0xa810, 0xa438, 0xd701, 0xa438, 0x4054,
        0xa438, 0xa820, 0xa438, 0xd701, 0xa438, 0x4057, 0xa438, 0xa640,
        0xa438, 0xd704, 0xa438, 0x4046, 0xa438, 0xa840, 0xa438, 0xd706,
        0xa438, 0x40b5, 0xa438, 0x0c03, 0xa438, 0x1502, 0xa438, 0xae20,
        0xa438, 0x9503, 0xa438, 0xd401, 0xa438, 0x1000, 0xa438, 0x0fcf,
        0xa438, 0x1000, 0xa438, 0x0fda, 0xa438, 0x1000, 0xa438, 0x1008,
        0xa438, 0x1000, 0xa438, 0x0fe3, 0xa438, 0xcc00, 0xa438, 0x80c0,
        0xa438, 0x8103, 0xa438, 0x83e0, 0xa438, 0xd71e, 0xa438, 0x2318,
        0xa438, 0x01ae, 0xa438, 0xd704, 0xa438, 0x40bc, 0xa438, 0x0c03,
        0xa438, 0x1502, 0xa438, 0x8302, 0xa438, 0x9503, 0xa438, 0xb801,
        0xa438, 0xd706, 0xa438, 0x2b59, 0xa438, 0x07f8, 0xa438, 0xd700,
        0xa438, 0x2109, 0xa438, 0x04ab, 0xa438, 0xa508, 0xa438, 0xcb15,
        0xa438, 0xd70c, 0xa438, 0x430c, 0xa438, 0x1000, 0xa438, 0x10ca,
        0xa438, 0x0c03, 0xa438, 0x1502, 0xa438, 0xa108, 0xa438, 0x9503,
        0xa438, 0x0c03, 0xa438, 0x1502, 0xa438, 0x0c1f, 0xa438, 0x0f13,
        0xa438, 0x9503, 0xa438, 0x1000, 0xa438, 0x1021, 0xa438, 0xd70c,
        0xa438, 0x5fb3, 0xa438, 0x0c03, 0xa438, 0x1502, 0xa438, 0x8f1f,
        0xa438, 0x9503, 0xa438, 0x1000, 0xa438, 0x1021, 0xa438, 0xd70c,
        0xa438, 0x7f33, 0xa438, 0x0c03, 0xa438, 0x1502, 0xa438, 0x0c0f,
        0xa438, 0x0d00, 0xa438, 0x0c70, 0xa438, 0x0b00, 0xa438, 0xab08,
        0xa438, 0x9503, 0xa438, 0xd704, 0xa438, 0x3cf1, 0xa438, 0x01f9,
        0xa438, 0x0c1f, 0xa438, 0x0d11, 0xa438, 0xf003, 0xa438, 0x0c1f,
        0xa438, 0x0d0d, 0xa438, 0x0cc0, 0xa438, 0x0d40, 0xa438, 0x1000,
        0xa438, 0x104f, 0xa438, 0x0c03, 0xa438, 0x1502, 0xa438, 0xab80,
        0xa438, 0x9503, 0xa438, 0x1000, 0xa438, 0x1021, 0xa438, 0xa940,
        0xa438, 0xd700, 0xa438, 0x5f99, 0xa438, 0x0c03, 0xa438, 0x1502,
        0xa438, 0x8b80, 0xa438, 0x9503, 0xa438, 0x8940, 0xa438, 0xd700,
        0xa438, 0x5bbf, 0xa438, 0x0c03, 0xa438, 0x1502, 0xa438, 0x8b08,
        0xa438, 0x9503, 0xa438, 0xba20, 0xa438, 0xd704, 0xa438, 0x4100,
        0xa438, 0xd115, 0xa438, 0xd04f, 0xa438, 0xf001, 0xa438, 0x1000,
        0xa438, 0x1021, 0xa438, 0xd700, 0xa438, 0x5fb4, 0xa438, 0x0c03,
        0xa438, 0x1502, 0xa438, 0x0c0f, 0xa438, 0x0d00, 0xa438, 0x0c70,
        0xa438, 0x0b10, 0xa438, 0xab08, 0xa438, 0x9503, 0xa438, 0xd704,
        0xa438, 0x3cf1, 0xa438, 0x8178, 0xa438, 0x0c1f, 0xa438, 0x0d11,
        0xa438, 0xf003, 0xa438, 0x0c1f, 0xa438, 0x0d0d, 0xa438, 0x0cc0,
        0xa438, 0x0d40, 0xa438, 0x1000, 0xa438, 0x104f, 0xa438, 0x0c03,
        0xa438, 0x1502, 0xa438, 0xab80, 0xa438, 0x9503, 0xa438, 0x1000,
        0xa438, 0x1021, 0xa438, 0xd706, 0xa438, 0x5fad, 0xa438, 0xd407,
        0xa438, 0x1000, 0xa438, 0x0fcf, 0xa438, 0x0c03, 0xa438, 0x1502,
        0xa438, 0x8b88, 0xa438, 0x9503, 0xa438, 0x1000, 0xa438, 0x1021,
        0xa438, 0xd702, 0xa438, 0x7fa4, 0xa438, 0xd706, 0xa438, 0x61bf,
        0xa438, 0x0c03, 0xa438, 0x1502, 0xa438, 0x0c30, 0xa438, 0x0110,
        0xa438, 0xa304, 0xa438, 0x9503, 0xa438, 0xd199, 0xa438, 0xd04b,
        0xa438, 0x1000, 0xa438, 0x1021, 0xa438, 0xd700, 0xa438, 0x5fb4,
        0xa438, 0xd704, 0xa438, 0x3cf1, 0xa438, 0x81a5, 0xa438, 0x0c1f,
        0xa438, 0x0d02, 0xa438, 0xf003, 0xa438, 0x0c1f, 0xa438, 0x0d01,
        0xa438, 0x0cc0, 0xa438, 0x0d40, 0xa438, 0xa420, 0xa438, 0x8720,
        0xa438, 0x1000, 0xa438, 0x104f, 0xa438, 0x1000, 0xa438, 0x0fda,
        0xa438, 0xd70c, 0xa438, 0x41ac, 0xa438, 0x0c03, 0xa438, 0x1502,
        0xa438, 0x8108, 0xa438, 0x9503, 0xa438, 0x0cc0, 0xa438, 0x0040,
        0xa438, 0x0c03, 0xa438, 0x0102, 0xa438, 0x0ce0, 0xa438, 0x03e0,
        0xa438, 0xccce, 0xa438, 0xf008, 0xa438, 0x0cc0, 0xa438, 0x0040,
        0xa438, 0x0c03, 0xa438, 0x0100, 0xa438, 0x0ce0, 0xa438, 0x0380,
        0xa438, 0xcc9c, 0xa438, 0x1000, 0xa438, 0x103f, 0xa438, 0x0c03,
        0xa438, 0x1502, 0xa438, 0xa640, 0xa438, 0x9503, 0xa438, 0xcb16,
        0xa438, 0xd706, 0xa438, 0x6129, 0xa438, 0xd70c, 0xa438, 0x608c,
        0xa438, 0xd17a, 0xa438, 0xd04a, 0xa438, 0xf006, 0xa438, 0xd17a,
        0xa438, 0xd04b, 0xa438, 0xf003, 0xa438, 0xd13d, 0xa438, 0xd04b,
        0xa438, 0x0c1f, 0xa438, 0x0f14, 0xa438, 0xcb17, 0xa438, 0x8fc0,
        0xa438, 0x1000, 0xa438, 0x0fbd, 0xa438, 0xaf40, 0xa438, 0x1000,
        0xa438, 0x0fbd, 0xa438, 0x0cc0, 0xa438, 0x0f80, 0xa438, 0x1000,
        0xa438, 0x0fbd, 0xa438, 0xafc0, 0xa438, 0x1000, 0xa438, 0x0fbd,
        0xa438, 0x1000, 0xa438, 0x1021, 0xa438, 0xd701, 0xa438, 0x652e,
        0xa438, 0xd700, 0xa438, 0x5db4, 0xa438, 0x0c03, 0xa438, 0x1502,
        0xa438, 0x8640, 0xa438, 0xa702, 0xa438, 0x9503, 0xa438, 0xa720,
        0xa438, 0x1000, 0xa438, 0x0fda, 0xa438, 0xa108, 0xa438, 0x1000,
        0xa438, 0x0fec, 0xa438, 0x8108, 0xa438, 0x1000, 0xa438, 0x0fe3,
        0xa438, 0xa202, 0xa438, 0xa308, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x8308, 0xa438, 0xcb18,
        0xa438, 0x1000, 0xa438, 0x10c2, 0xa438, 0x1000, 0xa438, 0x1021,
        0xa438, 0xd70c, 0xa438, 0x2c60, 0xa438, 0x02bd, 0xa438, 0xff58,
        0xa438, 0x8f1f, 0xa438, 0x1000, 0xa438, 0x1021, 0xa438, 0xd701,
        0xa438, 0x7f8e, 0xa438, 0x1000, 0xa438, 0x0fe3, 0xa438, 0xa130,
        0xa438, 0xaa2f, 0xa438, 0xa2d5, 0xa438, 0xa407, 0xa438, 0xa720,
        0xa438, 0x8310, 0xa438, 0xa308, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x0000,
        0xa438, 0x0000, 0xa438, 0x0000, 0xa438, 0x8308, 0xa438, 0x1800,
        0xa438, 0x02d2, 0xa436, 0xA10E, 0xa438, 0x017f, 0xa436, 0xA10C,
        0xa438, 0x0e04, 0xa436, 0xA10A, 0xa438, 0x0c67, 0xa436, 0xA108,
        0xa438, 0x0f13, 0xa436, 0xA106, 0xa438, 0x0eb1, 0xa436, 0xA104,
        0xa438, 0x0e79, 0xa436, 0xA102, 0xa438, 0x0b23, 0xa436, 0xA100,
        0xa438, 0x0908, 0xa436, 0xA110, 0xa438, 0x00ff, 0xa436, 0xb87c,
        0xa438, 0x8ad8, 0xa436, 0xb87e, 0xa438, 0xaf8a, 0xa438, 0xf0af,
        0xa438, 0x8af9, 0xa438, 0xaf8d, 0xa438, 0xdaaf, 0xa438, 0x8e1c,
        0xa438, 0xaf8f, 0xa438, 0x03af, 0xa438, 0x8f06, 0xa438, 0xaf8f,
        0xa438, 0x06af, 0xa438, 0x8f06, 0xa438, 0x0265, 0xa438, 0xa002,
        0xa438, 0x8d78, 0xa438, 0xaf23, 0xa438, 0x47a1, 0xa438, 0x0d06,
        0xa438, 0x028b, 0xa438, 0x05af, 0xa438, 0x225a, 0xa438, 0xaf22,
        0xa438, 0x66f8, 0xa438, 0xe08a, 0xa438, 0x33a0, 0xa438, 0x0005,
        0xa438, 0x028b, 0xa438, 0x21ae, 0xa438, 0x0ea0, 0xa438, 0x0105,
        0xa438, 0x028b, 0xa438, 0xb3ae, 0xa438, 0x06a0, 0xa438, 0x0203,
        0xa438, 0x028c, 0xa438, 0x9dfc, 0xa438, 0x04f8, 0xa438, 0xfbfa,
        0xa438, 0xef69, 0xa438, 0xe080, 0xa438, 0x13ad, 0xa438, 0x267e,
        0xa438, 0xd067, 0xa438, 0xe48a, 0xa438, 0x34e4, 0xa438, 0x8a36,
        0xa438, 0xe48a, 0xa438, 0x38e4, 0xa438, 0x8a3a, 0xa438, 0xd0ae,
        0xa438, 0xe48a, 0xa438, 0x35e4, 0xa438, 0x8a37, 0xa438, 0xe48a,
        0xa438, 0x39e4, 0xa438, 0x8a3b, 0xa438, 0xd000, 0xa438, 0xe48a,
        0xa438, 0x3ce4, 0xa438, 0x8a3d, 0xa438, 0xe48a, 0xa438, 0x3ee4,
        0xa438, 0x8a3f, 0xa438, 0xe48a, 0xa438, 0x40e4, 0xa438, 0x8a41,
        0xa438, 0xe48a, 0xa438, 0x42e4, 0xa438, 0x8a43, 0xa438, 0xe48a,
        0xa438, 0x44d0, 0xa438, 0x02e4, 0xa438, 0x8a45, 0xa438, 0xd00a,
        0xa438, 0xe48a, 0xa438, 0x46d0, 0xa438, 0x16e4, 0xa438, 0x8a47,
        0xa438, 0xd01e, 0xa438, 0xe48a, 0xa438, 0x48d1, 0xa438, 0x02bf,
        0xa438, 0x8dce, 0xa438, 0x026b, 0xa438, 0xd0d1, 0xa438, 0x0abf,
        0xa438, 0x8dd1, 0xa438, 0x026b, 0xa438, 0xd0d1, 0xa438, 0x16bf,
        0xa438, 0x8dd4, 0xa438, 0x026b, 0xa438, 0xd0d1, 0xa438, 0x1ebf,
        0xa438, 0x8dd7, 0xa438, 0x026b, 0xa438, 0xd002, 0xa438, 0x73ab,
        0xa438, 0xef47, 0xa438, 0xe585, 0xa438, 0x5de4, 0xa438, 0x855c,
        0xa438, 0xee8a, 0xa438, 0x3301, 0xa438, 0xae03, 0xa438, 0x0224,
        0xa438, 0x95ef, 0xa438, 0x96fe, 0xa438, 0xfffc, 0xa438, 0x04f8,
        0xa438, 0xf9fa, 0xa438, 0xcefa, 0xa438, 0xef69, 0xa438, 0xfb02,
        0xa438, 0x8dab, 0xa438, 0xad50, 0xa438, 0x2ee1, 0xa438, 0x8a44,
        0xa438, 0xa104, 0xa438, 0x2bee, 0xa438, 0x8a33, 0xa438, 0x02e1,
        0xa438, 0x8a45, 0xa438, 0xbf8d, 0xa438, 0xce02, 0xa438, 0x6bd0,
        0xa438, 0xe18a, 0xa438, 0x46bf, 0xa438, 0x8dd1, 0xa438, 0x026b,
        0xa438, 0xd0e1, 0xa438, 0x8a47, 0xa438, 0xbf8d, 0xa438, 0xd402,
        0xa438, 0x6bd0, 0xa438, 0xe18a, 0xa438, 0x48bf, 0xa438, 0x8dd7,
        0xa438, 0x026b, 0xa438, 0xd0af, 0xa438, 0x8c94, 0xa438, 0xd200,
        0xa438, 0xbe00, 0xa438, 0x0002, 0xa438, 0x8ca5, 0xa438, 0x12a2,
        0xa438, 0x04f6, 0xa438, 0xe18a, 0xa438, 0x44a1, 0xa438, 0x0020,
        0xa438, 0xd129, 0xa438, 0xbf8d, 0xa438, 0xce02, 0xa438, 0x6bd0,
        0xa438, 0xd121, 0xa438, 0xbf8d, 0xa438, 0xd102, 0xa438, 0x6bd0,
        0xa438, 0xd125, 0xa438, 0xbf8d, 0xa438, 0xd402, 0xa438, 0x6bd0,
        0xa438, 0xbf8d, 0xa438, 0xd702, 0xa438, 0x6bd0, 0xa438, 0xae44
};

static const u16 phy_mcu_ram_code_8126a_1_2[] = {
        0xB87C, 0x8a32, 0xB87E, 0x0400, 0xB87C, 0x8376, 0xB87E, 0x0300,
        0xce00, 0x6CAF, 0xB87C, 0x8301, 0xB87E, 0x1133, 0xB87C, 0x8105,
        0xB87E, 0xa000, 0xB87C, 0x8148, 0xB87E, 0xa000, 0xa436, 0x81d8,
        0xa438, 0x5865, 0xacf8, 0xCCC0, 0xac90, 0x52B0, 0xad2C, 0x8000,
        0xB87C, 0x83e6, 0xB87E, 0x4A0E, 0xB87C, 0x83d2, 0xB87E, 0x0A0E,
        0xB87C, 0x80a0, 0xB87E, 0xB8B6, 0xB87C, 0x805e, 0xB87E, 0xB8B6,
        0xB87C, 0x8057, 0xB87E, 0x305A, 0xB87C, 0x8099, 0xB87E, 0x305A,
        0xB87C, 0x8052, 0xB87E, 0x3333, 0xB87C, 0x8094, 0xB87E, 0x3333,
        0xB87C, 0x807F, 0xB87E, 0x7975, 0xB87C, 0x803D, 0xB87E, 0x7975,
        0xB87C, 0x8036, 0xB87E, 0x305A, 0xB87C, 0x8078, 0xB87E, 0x305A,
        0xB87C, 0x8031, 0xB87E, 0x3335, 0xB87C, 0x8073, 0xB87E, 0x3335,
        0xa436, 0x81D8, 0xa438, 0x5865, 0xB87C, 0x867c, 0xB87E, 0x0617,
        0xad94, 0x0092, 0xB87C, 0x89B1, 0xB87E, 0x5050, 0xB87C, 0x86E0,
        0xB87E, 0x809A, 0xB87C, 0x86E2, 0xB87E, 0xB34D, 0xB87C, 0x8FD2,
        0xB87E, 0x004B, 0xB87C, 0x8691, 0xB87E, 0x007D, 0xB87E, 0x00AF,
        0xB87E, 0x00E1, 0xB87E, 0x00FF, 0xB87C, 0x867F, 0xB87E, 0x0201,
        0xB87E, 0x0201, 0xB87E, 0x0201, 0xB87E, 0x0201, 0xB87E, 0x0201,
        0xB87E, 0x0201, 0xB87C, 0x86DA, 0xB87E, 0xCDCD, 0xB87E, 0xE6CD,
        0xB87E, 0xCDCD, 0xB87C, 0x8FE8, 0xB87E, 0x0368, 0xB87E, 0x033F,
        0xB87E, 0x1046, 0xB87E, 0x147D, 0xB87E, 0x147D, 0xB87E, 0x147D,
        0xB87E, 0x0368, 0xB87E, 0x033F, 0xB87E, 0x1046, 0xB87E, 0x147D,
        0xB87E, 0x147D, 0xB87E, 0x147D, 0xa436, 0x80dd, 0xa438, 0xf0AB,
        0xa436, 0x80df, 0xa438, 0xC009, 0xa436, 0x80e7, 0xa438, 0x401E,
        0xa436, 0x80e1, 0xa438, 0x120A, 0xa436, 0x86f2, 0xa438, 0x5094,
        0xa436, 0x8701, 0xa438, 0x5094, 0xa436, 0x80f1, 0xa438, 0x30CC,
        0xa436, 0x80f3, 0xa438, 0x0001, 0xa436, 0x80f5, 0xa438, 0x330B,
        0xa436, 0x80f8, 0xa438, 0xCB76, 0xa436, 0x8105, 0xa438, 0xf0D3,
        0xa436, 0x8107, 0xa438, 0x0002, 0xa436, 0x8109, 0xa438, 0xff0B,
        0xa436, 0x810c, 0xa438, 0xC86D, 0xB87C, 0x8a32, 0xB87E, 0x0400,
        0xa6f8, 0x0000, 0xa6f8, 0x0000, 0xa436, 0x81bc, 0xa438, 0x1300,
        0xa846, 0x2410, 0xa86A, 0x0801, 0xa85C, 0x9680, 0xa436, 0x841D,
        0xa438, 0x4A28, 0xa436, 0x8016, 0xa438, 0xBE05, 0xBF9C, 0x004A,
        0xBF96, 0x41FA, 0xBF9A, 0xDC81, 0xa436, 0x8018, 0xa438, 0x0700,
        0xa436, 0x8ff4, 0xa438, 0x01AE, 0xa436, 0x8fef, 0xa438, 0x0172,
        0xa438, 0x00dc, 0xc842, 0x0002, 0xFFFF, 0xFFFF
};

// Структура для LDPC декодера
typedef struct {
    int n;
    int m;
    int** H; 
} LDPCDecoder;

// Функции для LDPC декодера
LDPCDecoder* LDPCDecoder_create(int n, int m);
void LDPCDecoder_destroy(LDPCDecoder* decoder);
void LDPCDecoder_setParityCheckMatrix(LDPCDecoder* decoder, int** H_matrix);
void LDPCDecoder_decode(LDPCDecoder* decoder, int* received);


Simple5GDriver* Simple5GDriver_create() {
    Simple5GDriver* driver = (Simple5GDriver*)malloc(sizeof(Simple5GDriver));
    if (driver == NULL) {
        perror("Failed to allocate memory for driver");
        return NULL;
    }

    // Инициализация полей драйвера
    driver->udpSocket = -1;
    driver->rtnetlinkSocket = -1;
    driver->isConnected = 0; // false
    strcpy(driver->imsi, "123456789012345");
    strcpy(driver->apn, "internet");
    driver->gtpTeid = 0;
    strcpy(driver->iccid, "");
    driver->ifindex = 0;
    driver->qrtrSocket = -1;

    // Инициализация мьютекса
    if (pthread_mutex_init(&driver->mtx, NULL) != 0) {
        perror("Failed to initialize mutex");
        free(driver);
        return NULL;
    }

    return driver;
}


void Simple5GDriver_destroy(Simple5GDriver* driver) {
    if (driver == NULL) {
        return;
    }

    // Закрытие сокетов
    if (driver->udpSocket >= 0) {
        close(driver->udpSocket);
    }
    if (driver->rtnetlinkSocket >= 0) {
        close(driver->rtnetlinkSocket);
    }
    if (driver->qrtrSocket >= 0) {
        close(driver->qrtrSocket);
    }

    // Уничтожение мьютекса
    pthread_mutex_destroy(&driver->mtx);

    // Освобождение памяти
    free(driver);
}

void Simple5GDriver_initialize(Simple5GDriver* driver) {
    // Создание UDP сокета
    driver->udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (driver->udpSocket < 0) {
        Simple5GDriver_logError("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Настройка адреса сервера
    memset(&driver->serverAddr, 0, sizeof(driver->serverAddr));
    driver->serverAddr.sin_family = AF_INET;
    driver->serverAddr.sin_addr.s_addr = inet_addr("192.168.1.1"); // Замени на нужный IP
    driver->serverAddr.sin_port = htons(5000); // Замени на нужный порт

    // Создание RTNetlink сокета
    driver->rtnetlinkSocket = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (driver->rtnetlinkSocket < 0) {
        Simple5GDriver_logError("RTNetlink socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Создание QRTR сокета
    driver->qrtrSocket = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_QRTR);
    if (driver->qrtrSocket < 0) {
        Simple5GDriver_logError("Sorry, I couldn't create the QRTR socket. Please check if the QRTR module is loaded and if you have the necessary permissions to use it. Make sure you're running as root!");
        exit(EXIT_FAILURE);
    }

    // Запись в регистры (только если driver->tp инициализирован!)
    if (driver->tp > 0) { // Проверка, что driver->tp валидный
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        snprintf(ifr.ifr_name, IFNAMSIZ, "eth0"); // Имя интерфейса!!!

        // Запись для phy_mcu_ram_code_8126a_1_1
        for (int i = 0; i < sizeof(phy_mcu_ram_code_8126a_1_1) / sizeof(u16); i += 2) {
            u16 reg = phy_mcu_ram_code_8126a_1_1[i];
            u16 value = phy_mcu_ram_code_8126a_1_1[i + 1];

            ifr.ifr_data = (void*) &value;

            if (ioctl(driver->tp, SIOCSMIIREG, &ifr) < 0) {
                int err = errno;
                fprintf(stderr, "Oops! Something went wrong with the MDI register(1_1) (error code: 0x%x). 😔\n", err);
                perror("More details:");
                return;
            }
            printf("Запись в регистр 0x%04x значение 0x%04x\n", reg, value);
        }

        // Запись для phy_mcu_ram_code_8126a_1_2
        for (int i = 0; i < sizeof(phy_mcu_ram_code_8126a_1_2) / sizeof(u16); i += 2) {
            u16 reg = phy_mcu_ram_code_8126a_1_2[i];
            u16 value = phy_mcu_ram_code_8126a_1_2[i + 1];

            ifr.ifr_data = (void*) &value;

            if (ioctl(driver->tp, SIOCSMIIREG, &ifr) < 0) {
                int err = errno;
                fprintf(stderr, "Oops! Something went wrong with the MDI register (1_2) (error code: 0x%x). 😔\n", err);
                perror("More details:");
                return;
            }
            printf("Запись в регистр 0x%04x значение 0x%04x\n", reg, value);
        }
    } else {
        fprintf(stderr, "Warning: driver->tp not initialized. Skipping register writes.\n");
    }

    printf("5G Driver Initialized.\n");
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

char* sendATCommand(const char* command, const char* device) {
    // Открываем устройство в режиме чтения и записи
    int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("Ошибка открытия устройства");
        return NULL;
    }

    // Настройка терминального устройства
    struct termios tty;
    memset(&tty, 0, sizeof tty);
    if (tcgetattr(fd, &tty) != 0) {
        perror("Ошибка получения терминальных настроек");
        close(fd);
        return NULL;
    }

    // Установка скорости передачи данных
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    // Установка бита данных, стоп-бита и четности
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8 бит данных
    tty.c_iflag &= ~IGNBRK; // игнорировать разрывы
    tty.c_lflag = 0; // отключить режимы чтения и записи
    tty.c_oflag = 0; // отключить режимы записи

    // Установка терминальных настроек
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Ошибка установки терминальных настроек");
        close(fd);
        return NULL;
    }

    // Отправка AT-команды
    if (write(fd, command, strlen(command)) != strlen(command)) {
        perror("Ошибка отправки AT-команды");
        close(fd);
        return NULL;
    }

    // Чтение ответа
    char buffer[256];
    memset(buffer, 0, sizeof buffer);
    int bytes_read = read(fd, buffer, sizeof buffer - 1);
    if (bytes_read < 0) {
        perror("Ошибка чтения ответа");
        close(fd);
        return NULL;
    }

    // Закрытие устройства
    close(fd);

    // Возвращаем ответ в виде строки
    char* response = malloc(bytes_read + 1);
    if (response == NULL) {
        perror("Ошибка выделения памяти");
        return NULL;
    }
    strncpy(response, buffer, bytes_read);
    response[bytes_read] = '\0';

    return response;
}


int get5GSignalStrength(const char* interfaceName) {
    char command[256];
    char line[256];
    FILE *fp;

    snprintf(command, sizeof(command), "iwconfig %s | grep 'Signal level'", interfaceName);

    fp = popen(command, "r");
    if (fp == NULL) {
        perror("Failed to run iwconfig");
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        // Парсинг строки для извлечения значения RSSI
        char* signalLevelStr = strstr(line, "Signal level=");
        if (signalLevelStr != NULL) {
            int rssi = atoi(signalLevelStr + strlen("Signal level="));
            pclose(fp);
            return rssi;
        }
    }

    pclose(fp);
    return -1; // RSSI не найден
}



void Simple5GDriver_sendData(Simple5GDriver* driver, const char* data) {
    if (!driver || !data) {
        Simple5GDriver_logError("Invalid input arguments");
        return;
    }

    pthread_mutex_lock(&driver->mtx);

    size_t dataLen = strlen(data);
    ssize_t sentBytes = sendto(driver->udpSocket, data, dataLen, 0,
                               (const struct sockaddr*)&driver->serverAddr, sizeof(driver->serverAddr));

    if (sentBytes < 0) {
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), "Send failed: %s", strerror(errno));
        Simple5GDriver_logError(errorMsg);
    } else if (sentBytes != dataLen) {
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), "Partial send: %zd of %zu bytes sent", sentBytes, dataLen);
        Simple5GDriver_logError(errorMsg);
    } else {
        printf("Sent %zd bytes: %s\n", sentBytes, data);
        Simple5GDriver_optimizeNetwork(driver);
    }

    pthread_mutex_unlock(&driver->mtx);
}


void Simple5GDriver_receiveData(Simple5GDriver* driver) {
    char buffer[1024];
    socklen_t addrLen = sizeof(driver->serverAddr);
    ssize_t len = recvfrom(driver->udpSocket, buffer, sizeof(buffer), 0,
                          (struct sockaddr*)&driver->serverAddr, &addrLen);

    if (len > 0) {
        buffer[len] = '\0';
        printf("Received: %s\n", buffer);
        Simple5GDriver_handleResponse(driver, buffer);

        if (strstr(buffer, "signalStrength")) {
            int signalStrength = atoi(strstr(buffer, ":") + 1);
            printf("Signal Strength: %d dBm\n", signalStrength);
        }
    } else if (len == 0) {
        Simple5GDriver_logError("Connection closed by peer");
    } else {
        Simple5GDriver_logError("Receive failed");
    }
}

void Simple5GDriver_handleResponse(Simple5GDriver* driver, const char* response) {
    printf("Handling response: %s\n", response);

    if (strstr(response, "create_tunnel_response")) {
        driver->isConnected = 1; // true
        printf("Tunnel created successfully.\n");
    } else if (strstr(response, "error")) {
        Simple5GDriver_logError("Error in response");
    } else if (strstr(response, "ICCID")) {
        strcpy(driver->iccid, strstr(response, ":") + 1);
        // Удаление пробелов и символов новой строки в конце строки
        char *end = driver->iccid + strlen(driver->iccid) - 1;
        while (end > driver->iccid && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) {
            *end-- = '\0';
        }

        printf("Received ICCID: %s\n", driver->iccid);
    }
}


void Simple5GDriver_setupGTP(Simple5GDriver* driver) {
    driver->gtpTeid = rand();
    printf("Setting up GTP tunnel with TEID: %u\n", driver->gtpTeid);

    // Формирование JSON строки для отправки
    char data[100]; 
    snprintf(data, sizeof(data), "{ \"type\": \"create_tunnel\", \"teid\": \"%u\" }", driver->gtpTeid);

    Simple5GDriver_sendData(driver, data); 
}

void Simple5GDriver_configureVLAN(Simple5GDriver* driver, const char* bondInterface, int vlanId) {
    char command[200]; 
    snprintf(command, sizeof(command), "ip link add link %s name %s.%d type vlan id %d", 
             bondInterface, bondInterface, vlanId, vlanId);
    int result = system(command);
    if (result != 0) {
        Simple5GDriver_logError("Failed to configure VLAN");
    }
    snprintf(command, sizeof(command), "ip link set dev %s.%d up", bondInterface, vlanId);
    result = system(command);
    if (result != 0) {
        Simple5GDriver_logError("Failed to bring up VLAN interface");
    }

    printf("Configured VLAN: %d on interface: %s\n", vlanId, bondInterface);
}

void Simple5GDriver_configureBonding(Simple5GDriver* driver, char** slaveInterfaces, int numInterfaces) {
    for (int i = 0; i < numInterfaces; i++) {
        char command[100];

        snprintf(command, sizeof(command), "ip link set dev %s master bond0", slaveInterfaces[i]);
        int result = system(command);
        if (result != 0) {
            Simple5GDriver_logError("Failed to enslave interface to bond");
        }

        snprintf(command, sizeof(command), "ip link set dev %s up", slaveInterfaces[i]);
        result = system(command);
        if (result != 0) {
            Simple5GDriver_logError("Failed to bring up slave interface");
        }
    }

    int result = system("ip link add bond0 type bond mode 802.3ad");
    if (result != 0) {
        Simple5GDriver_logError("Failed to create bond interface");
    }

    result = system("ip link set dev bond0 up");
    if (result != 0) {
        Simple5GDriver_logError("Failed to bring up bond interface");
    }

    printf("Configured bonding with interfaces: ");
    for (int i = 0; i < numInterfaces; i++) {
        printf("%s ", slaveInterfaces[i]);
    }
    printf("\nBonding interface 'bond0' created and brought up.\n");
}


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int Simple5GDriver_createVirtualInterface(Simple5GDriver* driver, const char *devname) {
    int fd, err;
    struct ifreq ifr;

    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        perror("open(/dev/net/tun) failed");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI; // or iff_tap if ethernet
    strncpy(ifr.ifr_name, devname, IFNAMSIZ);

    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
        perror("ioctl(TUNSETIFF) failed");
        close(fd);
        return -1;
    }

    printf("Created virtual interface: %s\n", ifr.ifr_name);

        //  Получение ifindex
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
                perror("ioctl(SIOCGIFINDEX) failed");
                close(fd);
                return -1;
            }

        driver->ifindex = ifr.ifr_ifindex;
    printf("Interface index: %d\n", driver->ifindex);

    // for devs: DO NOT CLOSE FD!
    driver->tun_fd = fd;

    return 0;
}



int Simple5GDriver_addRoute(Simple5GDriver* driver, const char *dest, const char *gateway, const char *dev) {
    struct rtentry route;
    memset(&route, 0, sizeof(route));

    // Destination address
    inet_aton(dest, &((struct sockaddr_in *)&route.rt_dst)->sin_addr);

    // Gateway address
    inet_aton(gateway, &((struct sockaddr_in *)&route.rt_gateway)->sin_addr);

    // Network mask
    inet_aton("255.255.255.0", &((struct sockaddr_in *)&route.rt_genmask)->sin_addr);

    // Flags
    route.rt_flags = RTF_UP | RTF_GATEWAY;

    // Interface index
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, dev);
    if (ioctl(driver->rtnetlinkSocket, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX) failed");
        return -1;
    }
    route.rt_dev = ifr.ifr_name;

    // Add the route
    if (ioctl(driver->rtnetlinkSocket, SIOCADDRT, &route) < 0) {
        perror("ioctl(SIOCADDRT) failed");
        return -1;
    }

    printf("Added route: %s via %s dev %s\n", dest, gateway, dev);
    return 0;
}

void Simple5GDriver_establish5GConnection(Simple5GDriver* driver) {
    printf("Establishing 5G connection...\n");

    // Создание виртуального интерфейса
    if (Simple5GDriver_createVirtualInterface(driver, "nettap0") < 0) {
        Simple5GDriver_logError("Failed to create virtual interface");
        return;
    }

    // Настройка IP-адреса для виртуального интерфейса
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("10.0.0.1");

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, "nettap0");
    memcpy(&ifr.ifr_addr, &addr, sizeof(addr));

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket() failed");
        return;
    }

    if (ioctl(sockfd, SIOCSIFADDR, &ifr) < 0) {
        perror("ioctl(SIOCSIFADDR) failed");
        close(sockfd);
        return;
    }

    close(sockfd);

    // Активация виртуального интерфейса
    system("ip link set nettap0 up");

    // Добавление маршрута по умолчанию через виртуальный интерфейс
    if (Simple5GDriver_addRoute(driver, "0.0.0.0", "10.0.0.1", "nettap0") < 0) {
        Simple5GDriver_logError("Failed to add default route");
        return;
    }

    driver->isConnected = 1; // true
    printf("5G connection established!\n");
}

void Simple5GDriver_connectTo5G(Simple5GDriver* driver) {
    Simple5GDriver_establish5GConnection(driver);

    // Формирование JSON строки для отправки
    char data[100]; 
    snprintf(data, sizeof(data), "{ \"type\": \"connect\", \"imsi\": \"%s\", \"apn\": \"%s\" }", 
             driver->imsi, driver->apn); 

    Simple5GDriver_sendData(driver, data);
    Simple5GDriver_receiveData(driver);

    // Создание потока для обновления данных
    pthread_t dataUpdateThread;
    if (pthread_create(&dataUpdateThread, NULL, Simple5GDriver_startDataUpdate, driver) != 0) {
        Simple5GDriver_logError("Failed to create data update thread");
    }
}

void Simple5GDriver_fetchICCID(Simple5GDriver* driver) {
    const char* atCommand = "AT+CCID\r";
    Simple5GDriver_sendData(driver, atCommand);
    Simple5GDriver_receiveData(driver);
}

void* Simple5GDriver_startDataUpdate(void* arg){

    Simple5GDriver* driver = (Simple5GDriver*)arg;

    while (driver->isConnected) {
        Simple5GDriver_fetchICCID(driver);
        Simple5GDriver_sendUserData(driver);
        Simple5GDriver_receiveData(driver);
        Simple5GDriver_optimizeNetwork(driver);
        sleep(1); // Задержка 1 секунда
    }

    return NULL;
}

void Simple5GDriver_sendUserData(Simple5GDriver* driver) {
    static int userIdCounter = 1;
    // Генерация случайных данных (можно заменить на реальные данные)
    int location = rand() % 100;
    int status = rand() % 2; 

    // Формирование JSON строки для отправки
    char userData[200];
    snprintf(userData, sizeof(userData), 
             "{ \"user_id\": \"%d\", \"location\": \"%d\", \"status\": \"%s\" }", 
             userIdCounter++, location, (status ? "active" : "inactive"));

    Simple5GDriver_sendData(driver, userData);
}

void Simple5GDriver_optimizeNetwork(Simple5GDriver* driver) {
    if (!driver->isConnected) {
        return;
    }

    int currentLoad = rand() % 100;
    if (currentLoad > 80) {
        Simple5GDriver_sendData(driver, "{ \"type\": \"optimize\", \"action\": \"reduce_load\" }");
        Simple5GDriver_logError("Network load high. Optimization initiated.");
    } else {
        Simple5GDriver_sendData(driver, "{ \"type\": \"optimize\", \"action\": \"normal\" }");
        Simple5GDriver_logError("Network load normal. No action needed.");
    }
}


void blockICMP() {
    if (system("iptables -A INPUT -p icmp --limit 100/sec --limit-burst 10 -j ACCEPT") == -1) {
        perror("Failed to add iptables rule 1");
    }

     if(system("iptables -A INPUT -p icmp -j DROP") == -1) {
        perror("Failed to add iptables rule 2");
    }

    printf("ICMP flood protection rules added\n");
}


void Simple5GDriver_createNetworkSlice(Simple5GDriver* driver, const char* sliceName, int bandwidth) {
    char data[200];
    snprintf(data, sizeof(data), "{ \"type\": \"create_slice\", \"name\": \"%s\", \"bandwidth\": \"%d\" }",
             sliceName, bandwidth);
    Simple5GDriver_sendData(driver, data);
}

void Simple5GDriver_deployEdgeApplication(Simple5GDriver* driver, const char* appName) {
    char data[100];
    snprintf(data, sizeof(data), "{ \"type\": \"deploy_app\", \"name\": \"%s\" }", appName);
    Simple5GDriver_sendData(driver, data);
}

void Simple5GDriver_enableBeamforming(Simple5GDriver* driver, const char* userId) {
    char data[100];
    snprintf(data, sizeof(data), "{ \"type\": \"beamforming\", \"user_id\": \"%s\" }", userId);
    Simple5GDriver_sendData(driver, data);
}


LDPCDecoder* LDPCDecoder_create(int n, int m) {
    LDPCDecoder* decoder = (LDPCDecoder*)malloc(sizeof(LDPCDecoder));
    if (decoder == NULL) {
        perror("Failed to allocate memory for LDPC decoder");
        return NULL;
    }
    decoder->n = n;
    decoder->m = m;

    // Выделение памяти для матрицы H
    decoder->H = (int**)malloc(m * sizeof(int*));
    if (decoder->H == NULL) {
        perror("Failed to allocate memory for H matrix");
        free(decoder);
        return NULL;
    }
    for (int i = 0; i < m; i++) {
        decoder->H[i] = (int*)malloc(n * sizeof(int));
        if (decoder->H[i] == NULL) {
            perror("Failed to allocate memory for H matrix row");
            // Освобождение ранее выделенной памяти
            for (int j = 0; j < i; j++) {
                free(decoder->H[j]);
            }
            free(decoder->H);
            free(decoder);
            return NULL;
        }
    }

    return decoder;
}

void LDPCDecoder_destroy(LDPCDecoder* decoder) {
    if (decoder == NULL) {
        return;
    }

    // Освобождение памяти матрицы H
    for (int i = 0; i < decoder->m; i++) {
        free(decoder->H[i]);
    }
    free(decoder->H);

    free(decoder);
}

void LDPCDecoder_setParityCheckMatrix(LDPCDecoder* decoder, int** H_matrix) {
    for (int i = 0; i < decoder->m; i++) {
        for (int j = 0; j < decoder->n; j++) {
            decoder->H[i][j] = H_matrix[i][j];
        }
    }
}

void LDPCDecoder_decode(LDPCDecoder* decoder, int* received) {
    for (int iter = 0; iter < 10; ++iter) {
        for (int j = 0; j < decoder->m; ++j) {
            int sum = 0;
            for (int i = 0; i < decoder->n; ++i) {
                if (decoder->H[j][i] == 1) {
                    sum += received[i];
                }
            }
            if (sum % 2 != 0) {
                for (int i = 0; i < decoder->n; ++i) {
                    if (decoder->H[j][i] == 1) {
                        received[i] ^= 1; 
                    }
                }
            }
        }
    }
}


int main() {

    if (KERNEL_VERSION(6, 8, 0) <= LINUX_VERSION_CODE) {
        printf("\033[0;33mWarning:\033[0m You have noticed the Linux kernel version 6.8 or later, so not all functions will be available in this driver. Good luck!\n");
    }

    // Инициализация генератора случайных чисел
    srand(time(NULL)); 

    Simple5GDriver* driver = Simple5GDriver_create();
    Simple5GDriver_initialize(driver);
    Simple5GDriver_connectTo5G(driver);

    // LDPC Decoder
    LDPCDecoder* ldpcDecoder = LDPCDecoder_create(N, M);

    // Расширенная матрица проверки четности (статическое выделение памяти)
    int H_matrix[M][N] = {
        {1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1},
        {0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0},
        {1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1},
        {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0},
        {0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0}
    };

    // Приведение H_matrix к типу int** для передачи в LDPCDecoder_setParityCheckMatrix
    int **H_matrix_ptr = (int **)malloc(M * sizeof(int *));
    for (int i = 0; i < M; i++) {
        H_matrix_ptr[i] = H_matrix[i];
    }


    LDPCDecoder_setParityCheckMatrix(ldpcDecoder, H_matrix_ptr);

    int receivedMessage[N] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0};

    LDPCDecoder_decode(ldpcDecoder, receivedMessage);

    printf("Decoded message: ");
    for (int i = 0; i < N; i++) {
        printf("%d", receivedMessage[i]);
    }
    printf("\n");

    blockICMP();

    // Освобождение памяти, выделенной для H_matrix_ptr
    free(H_matrix_ptr);

    // Освобождение ресурсов драйвера и декодера
    LDPCDecoder_destroy(ldpcDecoder);
    Simple5GDriver_destroy(driver);

    return 0;
}

