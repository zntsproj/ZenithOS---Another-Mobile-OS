#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <linux/udp.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/inet.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/jiffies.h>
#include <linux/rfkill.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/wwan.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/completion.h>
#include <linux/kmod.h>
#include <linux/uaccess.h>

#ifndef RFKILL_TYPE_CELLULAR
#define RFKILL_TYPE_CELLULAR 7
#endif

#define DRIVER_NAME "fiveg_driver"
#define PROC_FILENAME "fiveg_driver"

#define ICCID_REGISTER_OFFSET 0x100
#define ICCID_LENGTH 20
#define ANTENNA_POWER_REGISTER_OFFSET 0x200
#define QMI_DEVICE_PATH "/dev/cdc-wdm0"
#define MAX_QMI_OUTPUT_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ne5link, MAIN DEV of ZenithOS.");
MODULE_DESCRIPTION("5G driver");

struct fiveg_connection {
    struct socket *sock;
    struct sockaddr_in server_addr;
    struct rfkill *rfkill;
    char *mec_server_address;
    int mec_server_port;
    char qmi_device[64];
    struct proc_dir_entry *proc_file;
};

struct fiveg_command_context {
    struct completion comp;
    char *output_buffer;
    int exit_code;
};

static int fiveg_send_qmi_command(const char *command, char *output, size_t output_len);
static int fiveg_run_command(const char *command, char *output, size_t output_len);

static struct wwan_port *fiveg_wwan_port;
static struct fiveg_connection *conn;
static struct wwan_port_caps fiveg_wwan_port_caps;

static const struct wwan_port_ops fiveg_wwan_port_ops = {
    .start = NULL,
    .stop = NULL,
};

static bool fiveg_rfkill_set_block(void *data, bool blocked) {
    struct fiveg_connection *conn = data;
    char output[256];
    int ret;

    if (blocked) {
        ret = fiveg_send_qmi_command("radio off", output, sizeof(output));
    } else {
        ret = fiveg_send_qmi_command("radio on", output, sizeof(output));
    }
    if (ret < 0) {
        printk(KERN_ERR "Failed to set radio state: %d\n", ret);
        return false;
    }
    return true;
}

static const struct rfkill_ops fiveg_rfkill_ops = {
    .set_block = fiveg_rfkill_set_block,
};

static void __iomem *base_register;

static int fiveg_send_qmi_command(const char *command, char *output, size_t output_len) {
    int ret;
    char full_command[256];

    snprintf(full_command, sizeof(full_command), "qmi-cli --device=%s %s", conn->qmi_device, command);

    ret = fiveg_run_command(full_command, output, output_len);
    if (ret < 0) {
        printk(KERN_ERR "Failed to run qmi command: %s, error: %d\n", command, ret);
        return ret;
    }
    printk(KERN_INFO "QMI command output: %s\n", output);
    return 0;
}

static void fiveg_command_complete(struct subprocess_info *sub_info) {
    struct fiveg_command_context *ctx = sub_info->data;

    printk(KERN_INFO "Command completed\n");

    complete(&ctx->comp);
}

static int fiveg_run_command(const char *command, char *output, size_t output_len) {
    struct subprocess_info *sub_info;
    struct fiveg_command_context *ctx;
    char *argv[] = {"/bin/sh", "-c", (char *)command, NULL};
    char *envp[] = {"HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL};
    int ret;

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;
    init_completion(&ctx->comp);
    ctx->output_buffer = kmalloc(MAX_QMI_OUTPUT_SIZE, GFP_KERNEL);
    if (!ctx->output_buffer) {
        kfree(ctx);
        return -ENOMEM;
    }

    sub_info = call_usermodehelper_setup(argv[0], argv, envp, GFP_KERNEL,
                                        fiveg_command_complete, NULL, ctx);
    if (!sub_info) {
        kfree(ctx->output_buffer);
        kfree(ctx);
        return -ENOMEM;
    }

    ret = call_usermodehelper_exec(sub_info, UMH_NO_WAIT);
    if (ret < 0) {
        kfree(ctx->output_buffer);
        kfree(ctx);
        return ret;
    }

    wait_for_completion_interruptible(&ctx->comp);

    size_t copy_len = min((size_t)output_len - 1, strlen(ctx->output_buffer));
    strncpy(output, ctx->output_buffer, copy_len);
    output[copy_len] = '\0';

    kfree(ctx->output_buffer);
    kfree(ctx);
    return ret; // Возвращаем ret из call_usermodehelper_exec
}




static char *fiveg_get_iccid(void) {
    char *iccid;
    int i;

    iccid = kmalloc(ICCID_LENGTH + 1, GFP_KERNEL);
    if (!iccid) {
        return NULL;
    }

    for (i = 0; i < ICCID_LENGTH; i++) {
        iccid[i] = readb(base_register + ICCID_REGISTER_OFFSET + i);
    }

    iccid[ICCID_LENGTH] = '\0';

    return iccid;
}

static int fiveg_connect(struct fiveg_connection *conn, const char *ip, int port, struct device *dev) {
    int ret;

    conn->rfkill = rfkill_alloc("5g-modem", dev, RFKILL_TYPE_CELLULAR, &fiveg_rfkill_ops, conn);
    if (!conn->rfkill) {
        printk(KERN_ERR "Failed to allocate rfkill switch\n");
        return -ENOMEM;
    }

    ret = rfkill_register(conn->rfkill);
    if (ret < 0) {
        rfkill_destroy(conn->rfkill);
        printk(KERN_ERR "Failed to register rfkill switch\n");
        return ret;
    }

    ret = sock_create_kern(&init_net, PF_INET, SOCK_DGRAM, IPPROTO_UDP, &conn->sock);
    if (ret < 0) {
        printk(KERN_ERR "Failed to create socket: %d\n", ret);
        rfkill_unregister(conn->rfkill);
        rfkill_destroy(conn->rfkill);
        return ret;
    }
    memset(&conn->server_addr, 0, sizeof(conn->server_addr));
    conn->server_addr.sin_family = AF_INET;
    conn->server_addr.sin_port = htons(port);
    ret = in4_pton(ip, -1, (u8 *)&conn->server_addr.sin_addr.s_addr, '\0', NULL);

    if (ret != 1) {
        printk(KERN_ERR "Invalid IP address: %s\n", ip);
        sock_release(conn->sock);
        rfkill_unregister(conn->rfkill);
        rfkill_destroy(conn->rfkill);
        return -EINVAL;
    }

    return 0;
}

static ssize_t antenna_power_show(struct device *dev, struct device_attribute *attr, char *buf) {
    u8 power_state = readb(base_register + ANTENNA_POWER_REGISTER_OFFSET);
    return sprintf(buf, "%u\n", power_state);
}

static ssize_t antenna_power_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    unsigned long power_state;
    int ret;

    ret = kstrtoul(buf, 0, &power_state);
    if (ret)
        return ret;

    if (power_state == 0) {
        writeb(0, base_register + ANTENNA_POWER_REGISTER_OFFSET);
    } else if (power_state == 1){
        writeb(1, base_register + ANTENNA_POWER_REGISTER_OFFSET);
    }

    return count;
}
static DEVICE_ATTR(antenna_power, 0644, antenna_power_show, antenna_power_store);

static int fiveg_proc_show(struct seq_file *m, void *v) {
    char iccid[ICCID_LENGTH + 1];
    char signal_strength[256];

    memset(iccid, 0, sizeof(iccid));
    memset(signal_strength, 0, sizeof(signal_strength));
    strncpy(iccid, fiveg_get_iccid(), ICCID_LENGTH);

    fiveg_send_qmi_command("nas get-signal-strength", signal_strength, sizeof(signal_strength));

    seq_printf(m, "ICCID: %s\n", iccid);
    seq_printf(m, "Signal Strength: %s\n", signal_strength);

    return 0;
}

static int fiveg_proc_open(struct inode *inode, struct file *file) {
    return single_open(file, fiveg_proc_show, NULL);
}

static const struct file_operations fiveg_proc_fops = {
    .open = fiveg_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int fiveg_probe(struct platform_device *pdev) {
    int ret;
    struct device *dev = &pdev->dev;
    struct resource *res;
    char *iccid;

    printk(KERN_INFO "%s: Probing...\n", DRIVER_NAME);

    conn = kzalloc(sizeof(*conn), GFP_KERNEL);
    if (!conn) {
        return -ENOMEM;
    }

    strncpy(conn->qmi_device, QMI_DEVICE_PATH, sizeof(conn->qmi_device) -1);
    conn->qmi_device[sizeof(conn->qmi_device) - 1] = '\0';

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        ret = -ENODEV;
        goto err_free_conn;
    }

    base_register = devm_ioremap_resource(dev, res);
    if (IS_ERR(base_register)) {
        ret = PTR_ERR(base_register);
        goto err_free_conn;
    }

    iccid = fiveg_get_iccid();
    if (iccid) {
        printk(KERN_INFO "ICCID: %s\n", iccid);
        kfree(iccid);
    } else {
        printk(KERN_ERR "Failed to read ICCID\n");
    }

    ret = fiveg_connect(conn, "192.168.1.100", 8944, dev);
    if (ret < 0) {
        goto err_free_conn;
    }

    ret = device_create_file(dev, &dev_attr_antenna_power);
    if (ret) {
        dev_err(dev, "Failed to create sysfs attributes\n");
        goto err_rfkill;
    }

    conn->proc_file = proc_create(PROC_FILENAME, 0644, NULL, &fiveg_proc_fops);
    if (!conn->proc_file) {
        printk(KERN_ERR "Failed to create /proc/%s file\n", PROC_FILENAME);
        goto err_sysfs;
    }

    fiveg_wwan_port_caps.frag_len = 4096;
    fiveg_wwan_port_caps.headroom_len = 0;

    fiveg_wwan_port = wwan_create_port(dev, WWAN_PORT_AT, &fiveg_wwan_port_ops, &fiveg_wwan_port_caps, conn);
    if (IS_ERR(fiveg_wwan_port)) {
        printk(KERN_ERR "Failed to create WWAN port\n");
        ret = PTR_ERR(fiveg_wwan_port);
        goto err_proc;
    }

    platform_set_drvdata(pdev, conn);
    printk(KERN_INFO "%s: Probed successfully\n", DRIVER_NAME);
    return 0;

err_proc:
    proc_remove(conn->proc_file);

err_sysfs:
    device_remove_file(dev, &dev_attr_antenna_power);

err_rfkill:
    rfkill_unregister(conn->rfkill);
    rfkill_destroy(conn->rfkill);
    sock_release(conn->sock);

err_free_conn:
    kfree(conn);
    return ret;
}

static void fiveg_remove(struct platform_device *pdev) {
    struct fiveg_connection *conn = platform_get_drvdata(pdev);

    if (fiveg_wwan_port) {
        wwan_remove_port(fiveg_wwan_port);
        printk(KERN_INFO "WWAN port removed\n");
    }

    if (conn->proc_file) {
        proc_remove(conn->proc_file);
        printk(KERN_INFO "/proc/%s file removed\n", PROC_FILENAME);
    }

    device_remove_file(&pdev->dev, &dev_attr_antenna_power);

    if (conn && conn->rfkill) {
        rfkill_unregister(conn->rfkill);
        rfkill_destroy(conn->rfkill);
    }
    if (conn && conn->sock) {
        sock_release(conn->sock);
    }
    kfree(conn);

    printk(KERN_INFO "%s: Removed\n", DRIVER_NAME);
}

static struct platform_driver fiveg_driver = {
    .probe = fiveg_probe,
    .remove = fiveg_remove,
    .driver = {
        .name = DRIVER_NAME,
    },
};

module_platform_driver(fiveg_driver);
