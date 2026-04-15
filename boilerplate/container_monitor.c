#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/list.h>

#define DEVICE_NAME "container_monitor"
#define IOCTL_REGISTER _IOW('p', 1, struct monitor_req)

struct monitor_req {
    pid_t pid;
    size_t soft_limit;
    size_t hard_limit;
};

struct node {
    pid_t pid;
    size_t soft;
    size_t hard;
    int warned;
    struct list_head list;
};

static LIST_HEAD(proc_list);
static DEFINE_MUTEX(lock);
static struct task_struct *thread;
static int major;

/* ================= IOCTL ================= */
static long dev_ioctl(struct file *f, unsigned int cmd, unsigned long arg){

    /* 🔥 FIX: validate command */
    if(cmd != IOCTL_REGISTER){
        return -EINVAL;
    }

    struct monitor_req req;

    if(copy_from_user(&req, (void __user *)arg, sizeof(req))){
        return -EFAULT;
    }

    printk(KERN_INFO "[container_monitor] REGISTER PID %d\n", req.pid);

    struct node *n = kmalloc(sizeof(*n), GFP_KERNEL);
    if(!n) return -ENOMEM;

    n->pid = req.pid;
    n->soft = req.soft_limit;
    n->hard = req.hard_limit;
    n->warned = 0;

    mutex_lock(&lock);
    list_add(&n->list, &proc_list);
    mutex_unlock(&lock);

    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = dev_ioctl,
};

/* ================= RSS ================= */
static unsigned long get_rss(struct task_struct *task){
    struct mm_struct *mm = task->mm;
    if(!mm) return 0;

    return get_mm_rss(mm) * PAGE_SIZE;
}

/* ================= MONITOR THREAD ================= */
static int monitor_fn(void *data){
    while(!kthread_should_stop()){

        mutex_lock(&lock);

        struct node *n, *tmp;
        list_for_each_entry_safe(n, tmp, &proc_list, list){

            struct task_struct *task =
                pid_task(find_vpid(n->pid), PIDTYPE_PID);

            if(!task){
                list_del(&n->list);
                kfree(n);
                continue;
            }

            unsigned long rss = get_rss(task);

            if(rss > n->soft && !n->warned){
                printk(KERN_INFO "[container_monitor] Soft limit exceeded PID %d\n", n->pid);
                n->warned = 1;
            }

            if(rss > n->hard){
                printk(KERN_INFO "[container_monitor] Hard limit exceeded PID %d\n", n->pid);

                send_sig(SIGKILL, task, 0);

                list_del(&n->list);
                kfree(n);
            }
        }

        mutex_unlock(&lock);

        msleep(1000);
    }
    return 0;
}

/* ================= INIT ================= */
static int __init init_mod(void){
    major = register_chrdev(0, DEVICE_NAME, &fops);

    printk(KERN_INFO "[container_monitor] Module loaded. Device: /dev/container_monitor\n");

    thread = kthread_run(monitor_fn, NULL, "monitor_thread");

    return 0;
}

/* ================= EXIT ================= */
static void __exit exit_mod(void){

    kthread_stop(thread);

    unregister_chrdev(major, DEVICE_NAME);

    printk(KERN_INFO "[container_monitor] Module unloaded.\n");
}

module_init(init_mod);
module_exit(exit_mod);
MODULE_LICENSE("GPL");
