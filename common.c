#include <sys/types.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <time.h>
#include <sys/shm.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include "common.h"

/* global variables containing sizes or quantity of stuff init by user, useful for every process */
int so_fill, day, init_docks, vessels_speed, vessels_capacity, load_speed, init_ports, init_vessels, storm_duration,
        swell_duration;
size_t shm_good_size, shm_supply_size, shm_demand_size, shm_portsinfo_size, shm_vesselsinfo_size, shm_goodsinfo_size,
        shm_weatherinfo_size, shm_vessels_position_info_size;

/*________________functions:________________*/

/* semaphores */

int get_sem() {

    int sem_id;
    if ((sem_id = semget(SEM_KEY, 10, IPC_CREAT | IPC_EXCL | 0660)) == -1) {
        if (errno == EEXIST) {
            if ((sem_id = semget(SEM_KEY, 10, IPC_CREAT | 0660)) == -1) {
                perror("shmget error");
                exit(EXIT_FAILURE);
            }
            return sem_id;
        } else {
            perror("semget error");
            exit(EXIT_FAILURE);
        }
    }
    semctl(sem_id, PORTS_SUPPLY_SEM, SETVAL, 1);
    semctl(sem_id, PORTS_DEMAND_SEM, SETVAL, 1);
    semctl(sem_id, VESSELS_SEM, SETVAL, 1);
    semctl(sem_id, START_MAIN, SETVAL, 0);
    semctl(sem_id, VESSELS_INFO_SEM, SETVAL, 1);
    semctl(sem_id, START_PORTS, SETVAL, 0);
    semctl(sem_id, START_VESSELS, SETVAL, 0);
    semctl(sem_id, HANDLER_V_SEM, SETVAL, 1);
    semctl(sem_id, WEATHER_SEM, SETVAL, 1);
    semctl(sem_id, START_WEATHER, SETVAL, 0);
    return sem_id;
}

int get_docks(pid_t port_pid) {

    int sem_id;
    if ((sem_id = semget((pid_t) port_pid, 1, IPC_CREAT | IPC_EXCL | 0660)) == -1) {
        if (errno == EEXIST) {
            if ((sem_id = semget(port_pid, 1, 0660)) == -1) {
                perror("shmget error");
                exit(EXIT_FAILURE);
            }
            return sem_id;
        } else {
            perror("semget error");
            exit(EXIT_FAILURE);
        }
    }
    semctl(sem_id, DOCKS, SETVAL, init_docks);
    return sem_id;
}

void rm_sem(int sem_id) {

    if ((semctl(sem_id, 0, IPC_RMID)) == -1) {
        perror("semctl error");
        exit(EXIT_FAILURE);
    }
}

int sem_cmd(int sem_id, unsigned short sem_num, short sem_op, short sem_flg) {
    struct sembuf sops;
    sops.sem_flg = sem_flg;
    sops.sem_op = sem_op;
    sops.sem_num = sem_num;
    return semop(sem_id, &sops, 1);
}

/* shared memory */

int getGoodsSeg(good_t **array) {

    int shm_id;
    if ((shm_id = shmget(SHM_GOODS_KEY, shm_good_size, 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*array = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getSupplySeg(port_supply **array) {

    int shm_id;
    if ((shm_id = shmget(SHM_SUPPLY_KEY, shm_supply_size, 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*array = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getDemandSeg(port_demand **array) {

    int shm_id;
    if ((shm_id = shmget(SHM_DEMAND_KEY, shm_demand_size, 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*array = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getTime(double **time_var) {

    int shm_id;
    if ((shm_id = shmget(SHM_TIME_KEY, sizeof(double), 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*time_var = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getPortsInfoSeg(ports_info **info_ports) {
    int shm_id;
    if ((shm_id = shmget(SHM_PORTSINFO_KEY, shm_portsinfo_size, 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*info_ports = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getVesselsInfoSeg(vessels_info **info_vessels) {
    int shm_id;
    if ((shm_id = shmget(SHM_VESSELSINFO_KEY, shm_vesselsinfo_size, 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*info_vessels = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getGoodsInfoSeg(goods_info **info_goods) {
    int shm_id;
    if ((shm_id = shmget(SHM_GOODSINFO_KEY, shm_goodsinfo_size, 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*info_goods = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getPortsNumber(int **port_counter) {
    int shm_id;
    if ((shm_id = shmget(PORTS_COUNTER_KEY, sizeof(int), 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*port_counter = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getSupplyDailyFill(long **daily_supply) {
    int shm_id;
    if ((shm_id = shmget(DAILY_SUPPLY_KEY, sizeof(long), 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*daily_supply = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getDemandDailyFill(long **daily_demand) {
    int shm_id;
    if ((shm_id = shmget(DAILY_DEMAND_KEY, sizeof(long), 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*daily_demand = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

int getWeatherInfoSeg(weather_info **info_weather) {
    int shm_id;
    if ((shm_id = shmget(SHM_WEATHERINFO_KEY, shm_weatherinfo_size, 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*info_weather = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }

    return shm_id;
}

int getVesselPositionInfoSeg(vessel_position_info **info_position_vessels) {
    int shm_id;
    if ((shm_id = shmget(SHM_VESSELSPOSITIONINFO_KEY, shm_vessels_position_info_size, 0660 | IPC_CREAT)) == -1) {
        perror("shmget error");
        exit(EXIT_FAILURE);
    }
    if ((*info_position_vessels = shmat(shm_id, NULL, 0)) == (void *) -1) {
        perror("shmat error");
        exit(EXIT_FAILURE);
    }

    return shm_id;
}

/* message queues */

int get_msg(pid_t port_pid) {
    int msg_id;
    if ((msg_id = msgget((key_t) port_pid, IPC_CREAT | 0660)) == -1) {
        perror("msgget error");
    }
    return msg_id;
}

int snd_msg(int msgqid, supply *to_send) {
    int size;
    size = sizeof(supply) - sizeof(long);
    return msgsnd(msgqid, to_send, size, IPC_NOWAIT);
}

void rm_msg(int msqid) {
    if (msgctl(msqid, IPC_RMID, NULL) < 0) {
        perror("msg_ctl error");
        exit(EXIT_FAILURE);
    }
}

/* nanosleep */

void working(double time) {
    struct timespec req, rem;
    req.tv_sec = time;
    req.tv_nsec = (time - req.tv_sec) * 1e9;
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}

/* signals */

void block_signals(int count, ...) {
    sigset_t mask;
    va_list argptr;
    int i;

    sigemptyset(&mask);

    va_start(argptr, count);

    for (i = 0; i < count; i++) {
        sigaddset(&mask, va_arg(argptr,
        int));
    }

    va_end(argptr);

    sigprocmask(SIG_BLOCK, &mask, NULL);
}

void unblock_signals(int count, ...) {
    sigset_t mask;
    va_list argptr;
    int i;

    sigemptyset(&mask);

    va_start(argptr, count);

    for (i = 0; i < count; i++) {
        sigaddset(&mask, va_arg(argptr,
        int));
    }

    va_end(argptr);

    sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

struct sigaction set_handler(int sig, void (*func)(int)) {
    struct sigaction sa, sa_old;
    sigset_t mask;
    sigemptyset(&mask);
    sa.sa_handler = func;
    sa.sa_mask = mask;
    sa.sa_flags = 0;
    sigaction(sig, &sa, &sa_old);
    return sa_old;
}
