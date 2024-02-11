#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <sys/signal.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include "common.h"

/*_________global_var_________*/
int min = 1, max = 2, sem_id, choosed_event = 0;

/* shm */
ports_info *info_ports;
weather_info *info_weather;
vessel_position_info *info_position_vessels;

/*_________prototypes_________*/

void chooseEventRandomly(); /* random chooseEvent between 1 and 3 (1=storm, 2=swell) */
void storm();

void swell();

unsigned long getSeed(unsigned long a, unsigned long b, unsigned long c);

void sigtstp_handler(int signum);

int main(int argc, char *argv[]) {
    day = 1;
    set_handler(SIGTSTP, sigtstp_handler);

    /* get all ports to retrieve pid for a random port to be chosen */
    getPortsInfoSeg(&info_ports);
    getWeatherInfoSeg(&info_weather);
    getVesselPositionInfoSeg(&info_position_vessels);

    sem_cmd(sem_id, WEATHER_SEM, WAIT, 0);
    info_weather->weather_pid = getpid();
    sem_cmd(sem_id, WEATHER_SEM, SIGNAL, 0);

    init_ports = atoi(argv[1]);
    init_vessels = atoi(argv[2]);

    /* get global sem and specific docks*/
    sem_id = get_sem();
    srand(time(NULL));
    sem_cmd(sem_id, START_WEATHER, WAIT, 0);

    while (1) {
        if (choosed_event == 0) {
            chooseEventRandomly();
        } else {
            pause();
        }
    }
}

/*_________functions_________*/
void chooseEventRandomly() {
    int randEvent = rand() % (max - min + 1) + min;
    switch (randEvent) {
        case 1:
            storm();
            break;
        case 2:
            swell();
            break;
    }
}

void storm() {
    int randVesselIndex = rand() % (init_vessels);
    choosed_event = 1;
    /* unsigned long seed = getSeed(clock(), time(NULL), getpid()); */
    if (info_position_vessels[randVesselIndex].in_port == 0) {      /* if it is moving ... */
        __pid_t chosenVessel = info_position_vessels[randVesselIndex].vessel_pid;
        kill(chosenVessel, SIGUSR1);
    } else {
        storm();
    }
}

void swell() {
    int randPortIndex = rand() % (init_ports);
    __pid_t chosenPort = info_ports[randPortIndex].pid;
    choosed_event = 1;
    kill(chosenPort, SIGUSR1);
}

/**
 * Jenkins hash function
unsigned long getSeed(unsigned long a, unsigned long b, unsigned long c)
{
    a=a-b;  a=a-c;  a=a^(c >> 13);
    b=b-c;  b=b-a;  b=b^(a << 8);
    c=c-a;  c=c-b;  c=c^(b >> 13);
    a=a-b;  a=a-c;  a=a^(c >> 12);
    b=b-c;  b=b-a;  b=b^(a << 16);
    c=c-a;  c=c-b;  c=c^(b >> 5);
    a=a-b;  a=a-c;  a=a^(c >> 3);
    b=b-c;  b=b-a;  b=b^(a << 10);
    c=c-a;  c=c-b;  c=c^(b >> 15);
    return c;
}  */

void sigtstp_handler(int signum) {
    block_signals(2, SIGTSTP, SIGTERM);
    day++;
    choosed_event = 0;
    sem_cmd(sem_id, START_MAIN, SIGNAL, 0);
    unblock_signals(2, SIGTSTP, SIGTERM);
    sem_cmd(sem_id, START_WEATHER, WAIT, 0);
}

void sigterm_handler(int signum) {
    shmdt(info_weather);
    shmdt(info_ports);
    shmdt(info_position_vessels);
    exit(0);
}
