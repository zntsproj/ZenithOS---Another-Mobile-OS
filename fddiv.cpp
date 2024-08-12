// Этот код предназначен для работы в ZenithOS, где есть root-права.
// Не рекомендуется запускать этот код на не рутированном устройстве, 
// так как он может не работать или привести к нежелательным последствиям.
//
// Код принимает пакеты FDDI на интерфейсе "fddi0". 
// Убедитесь, что этот интерфейс существует в вашей системе.
//
// Дополнительная информация:
// - Код выводит содержимое пакета в шестнадцатеричном виде.
// - Код подсчитывает количество полученных пакетов.
// - Код выводит статистику каждые 10 секунд.


// This code is intended to run on ZenithOS, which has root privileges.
// It is not recommended to run this code on a non-rooted device, 
// as it may not work or lead to undesirable consequences.
//
// The code receives FDDI packets on the "fddi0" interface. 
// Make sure this interface exists on your system.
//
// Additional information:
// - The code prints the contents of the packet in hexadecimal.
// - The code counts the number of packets received.
// - The code prints statistics every 10 seconds.auto 




#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include "fddi.h"
#include "if_hddi.h"
#include "fddi2.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

int main() {
  int fd = socket(PF_PACKET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket creation failed");
    return 1;
  }

  struct sockaddr_ll sll;
  memset(&sll, 0, sizeof(sll));
  sll.sll_family = AF_PACKET;
  sll.sll_ifindex = if_nametoindex("fddi0");
  sll.sll_protocol = htons(ETH_P_ALL); // Принимаем все протоколы

  if (bind(fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
    perror("bind failed");
    return 1;
  }

  // Переменные для подсчёта пакетов и времени
  unsigned long long packet_count = 0;
  time_t last_stats_time = time(NULL);

  // Дальнейшая обработка пакетов FDDI
  while (1) {
    char buffer[FDDI_K_LLC_LEN];
    int bytes_received = recv(fd, buffer, sizeof(buffer), 0);
    if (bytes_received < 0) {
      perror("recv failed");
      break;
    }

    // Обработка принятых данных
    printf("Received %d bytes on FDDI interface:\n", bytes_received);

    // Вывод содержимого пакета в шестнадцатеричном виде
    for (int i = 0; i < bytes_received; i++) {
      printf("%02x ", (unsigned char)buffer[i]);
      if ((i + 1) % 16 == 0) {
        printf("\n");
      }
    }
    printf("\n");

    // Подсчёт пакетов
    packet_count++;

    // Вывод статистики каждые 10 секунд
    time_t current_time = time(NULL);
    if (current_time - last_stats_time >= 10) {
      printf("Received %llu packets in the last 10 seconds\n", packet_count);
      packet_count = 0;
      last_stats_time = current_time;
    }
  }

  close(fd);
  return 0;
}