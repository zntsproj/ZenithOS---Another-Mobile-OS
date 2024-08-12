#include <jni.h>
#include <string>
#include <android/log.h>
#include <fcntl.h>
#include "socket.h"
#include "irda.h"
#include <unistd.h> //  for  close()
#include <errno.h>  //  for  errno
#include <string.h> //  fot  strerror()

//ATTENTION, ALL COMMENTS WILL BE IN RUSSIAN NOW
#define TAG "IrDA_JNI"

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_app_IrDAController_sendIrDAData(JNIEnv *env, jobject /* this */, jstring data) {
    //  Проверяем,  что  переданная  строка  не  равна  null
    if (data == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Error: Data is null");
        return env->NewStringUTF("Error: Data is null");
    }

    //  Получаем  C-строку  из  Java-строки
    const char *nativeData = env->GetStringUTFChars(data, nullptr);

    //  Проверяем,  что  преобразование  прошло  успешно
    if (nativeData == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Error: Failed to convert data to C string");
        return env->NewStringUTF("Error: Failed to convert data to C string");
    }

    //  Проверяем  наличие  IrDA
    if (access("/dev/irda0", F_OK) == -1) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Error: IrDA device not found");
        return env->NewStringUTF("Error: IrDA device not found");
    }

    //  Получаем  файловый  дескриптор  устройства  IrDA
    int irda_device_fd = open("/dev/irda0", O_RDWR);

    if (irda_device_fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Error: Failed to open IrDA device: %s", strerror(errno));
        return env->NewStringUTF("Error: Failed to open IrDA device");
    }

    //  Отправляем  данные  через  IrDA
    int result = ioctl(irda_device_fd, IRDA_CMD_SEND_DATA, nativeData);

    if (result < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Error: Failed to send data via IrDA: %s", strerror(errno));
        close(irda_device_fd);
        return env->NewStringUTF("Error: Failed to send data via IrDA");
    }

    //  Закрываем  файловый  дескриптор
    close(irda_device_fd);

    //  Освобождаем  ресурсы
    env->ReleaseStringUTFChars(data, nativeData);

    return env->NewStringUTF("Data sent successfully");
}
