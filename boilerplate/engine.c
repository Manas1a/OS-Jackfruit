#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "monitor_ioctl.h"   // 🔥 IMPORTANT

#define MONITOR_DEVICE "/dev/container_monitor"

#define STACK_SIZE (1024 * 1024)
#define CONTROL_PATH "/tmp/mini_runtime.sock"

static volatile int keep_running = 1;

typedef enum { CONTAINER_RUNNING, CONTAINER_EXITED, CONTAINER_KILLED } container_state_t;

typedef struct container_record {
    char id[32];
    pid_t pid;
    container_state_t state;
    int stop_requested;
    int hard_killed;
    struct container_record *next;
} container_record_t;

typedef struct {
    char id[32];
    char rootfs[256];
    char cmd[256];
    int log_fd;
} child_cfg;

container_record_t *containers = NULL;
pthread_mutex_t lock;

/* ================= SIGNAL ================= */
void handle(int s){ keep_running = 0; }

/* ================= CHILD ================= */
int child(void *arg){
    child_cfg *c=arg;

    sethostname(c->id,strlen(c->id));

    if(chroot(c->rootfs)!=0){ perror("chroot"); exit(1); }
    if(chdir("/")!=0){ perror("chdir"); exit(1); }

    mkdir("/proc",0555);
    mount("proc","/proc","proc",0,NULL);

    execl("/bin/sh","sh","-c",c->cmd,NULL);
    perror("exec");
    return 1;
}

/* ================= ADD ================= */
void add_container(char *id,pid_t pid){
    container_record_t *r=malloc(sizeof(*r));
    strcpy(r->id,id);
    r->pid=pid;
    r->state=CONTAINER_RUNNING;
    r->stop_requested=0;
    r->hard_killed=0;

    pthread_mutex_lock(&lock);
    r->next=containers;
    containers=r;
    pthread_mutex_unlock(&lock);
}

/* ================= REAP ================= */
void reap(){
    int st; pid_t p;

    while((p=waitpid(-1,&st,WNOHANG))>0){
        pthread_mutex_lock(&lock);

        container_record_t *c=containers;

        while(c){
            if(c->pid==p){
                if(WIFSIGNALED(st)){
                    int sig=WTERMSIG(st);
                    if(sig==SIGKILL && !c->stop_requested)
                        c->hard_killed=1;
                    c->state=CONTAINER_KILLED;
                } else c->state=CONTAINER_EXITED;
                break;
            }
            c=c->next;
        }

        pthread_mutex_unlock(&lock);
    }
}

/* ================= SUPERVISOR ================= */
void supervisor(char *root){
    int s=socket(AF_UNIX,SOCK_STREAM,0);

    struct sockaddr_un a={0};
    a.sun_family=AF_UNIX;
    strcpy(a.sun_path,CONTROL_PATH);

    unlink(CONTROL_PATH);
    bind(s,(void*)&a,sizeof(a));
    listen(s,5);

    fcntl(s,F_SETFL,O_NONBLOCK);

    signal(SIGINT,handle);
    signal(SIGTERM,handle);

    pthread_mutex_init(&lock,NULL);

    printf("Supervisor started\n");

    while(keep_running){

        reap();

        int client_fd = accept(s,NULL,NULL);
        if(client_fd < 0){
            usleep(100000);
            continue;
        }

        char buf[256]={0};
        read(client_fd,buf,sizeof(buf));

        if(strncmp(buf,"start",5)==0){

            char id[32], rfs[128], cmd[128];
            sscanf(buf,"start %s %s %127[^\n]",id,rfs,cmd);

            child_cfg *cfg=malloc(sizeof(*cfg));
            strcpy(cfg->id,id);
            strcpy(cfg->rootfs,rfs);
            strcpy(cfg->cmd,cmd);

            char *st=malloc(STACK_SIZE);

            pid_t pid=clone(child,st+STACK_SIZE,
                            CLONE_NEWPID|CLONE_NEWUTS|SIGCHLD,cfg);

            add_container(id,pid);

            /* ===== 🔥 CORRECT IOCTL ===== */
            int fd = open(MONITOR_DEVICE, O_RDWR);

            if(fd < 0){
                perror("OPEN DEVICE FAILED");
            } else {
                struct monitor_request req;

                req.pid = pid;
                strcpy(req.container_id, id);
                req.soft_limit_bytes = 50 * 1024 * 1024;
                req.hard_limit_bytes = 100 * 1024 * 1024;

                if(ioctl(fd, MONITOR_REGISTER, &req) < 0){
                    perror("IOCTL FAILED");
                } else {
                    printf("Registered PID %d with kernel\n", pid);
                }

                close(fd);
            }

            printf("Started %s (%d)\n",id,pid);
        }

        else if(strncmp(buf,"ps",2)==0){
            pthread_mutex_lock(&lock);

            container_record_t *c=containers;

            printf("ID\tPID\tSTATE\n");

            while(c){
                char *reason="running";

                if(c->state==CONTAINER_EXITED) reason="exited";
                else if(c->stop_requested) reason="stopped";
                else if(c->hard_killed) reason="hard_limit_killed";

                printf("%s\t%d\t%s\n",c->id,c->pid,reason);

                c=c->next;
            }

            pthread_mutex_unlock(&lock);
        }

        else if(strncmp(buf,"stop",4)==0){

            char id[32];
            sscanf(buf,"stop %s",id);

            pthread_mutex_lock(&lock);

            container_record_t *c=containers;

            while(c){
                if(strcmp(c->id,id)==0){
                    c->stop_requested=1;
                    kill(c->pid,SIGTERM);
                    break;
                }
                c=c->next;
            }

            pthread_mutex_unlock(&lock);
        }

        close(client_fd);
    }

    close(s);
    unlink(CONTROL_PATH);
}

/* ================= CLIENT ================= */
void send_req(char *msg){
    int s=socket(AF_UNIX,SOCK_STREAM,0);

    struct sockaddr_un a={0};
    a.sun_family=AF_UNIX;
    strcpy(a.sun_path,CONTROL_PATH);

    connect(s,(void*)&a,sizeof(a));
    write(s,msg,strlen(msg));

    close(s);
}

/* ================= MAIN ================= */
int main(int c,char *v[]){

    if(strcmp(v[1],"supervisor")==0)
        supervisor(v[2]);

    else if(strcmp(v[1],"start")==0){
        char b[256];
        snprintf(b,sizeof(b),"start %s %s %s",v[2],v[3],v[4]);
        send_req(b);
    }

    else if(strcmp(v[1],"ps")==0)
        send_req("ps");

    else if(strcmp(v[1],"stop")==0){
        char b[256];
        snprintf(b,sizeof(b),"stop %s",v[2]);
        send_req(b);
    }
}
