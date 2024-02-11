#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stddef.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/signal.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "common.h"

#define DEFAULT_SO_VESSELS 2
#define DEFAULT_SO_PORTS 4
#define DEFAULT_SO_GOODS 10
#define DEFAULT_SO_DAYS 10

/*_______________prototypes_____________________*/

void readSettingsFromFile();    /* read settings from the file "settings" */
void createPorts();             /* create init_ports ports and initialize ports info */
void createVessels();           /* create init_vessels vessels and initialize vessels info */
void createWeather();           /* create weather process */
void openLog();                 /* open and initialize the log file */
void closeAndFlushLog();        /* close and flush the log */
void shutdown();

void sig_handler(int signum);

void sigchld_handler(int signum);

void sigsegv_handler(int signum);

void sigusr1_handler(int signum);

/*_______________global variables_______________*/

int min_life, max_life, size, sim_time, init_goods;
double init_map;
int *shm_ids;                                       /* contains ids of segments, useful to detach and remove shm seg */
pid_t *array_vessels_pid, *array_ports_hit_pid;     /* used to start and stop vessels */
/* global sem */
int sem_id;
/* log */
FILE *log_file = NULL;
/* shm */
ports_info *info_ports;
vessels_info *info_vessels;
weather_info *info_weather;
vessel_position_info *info_position_vessels;
good_t *array_goods;
port_supply *array_supply;
port_demand *array_demand;
goods_info *array_goods_info;
double *time_left;
/* variables to calculate the supply and demand daily fill for port */
long *supply_daily_fill, *demand_daily_fill;
int *ports_number;
int *ports_hit;

/*-----------------------------------------------------------------------------------------------------------------------*/

int main() {
    struct timespec start_time, current_time;
    double elapsed_time, delay;     /* one second */
    int i, j, max, docks_id, busy_docks;
    pid_t winning_port_for_production, winning_port_for_demand;
    time_t t;
    struct tm *tm;

    set_handler(SIGINT, sig_handler);
    set_handler(SIGTERM, sig_handler);
    set_handler(SIGCHLD, sigchld_handler);
    set_handler(SIGSEGV, sigsegv_handler);
    set_handler(SIGUSR1, sigusr1_handler);
    t = time(NULL);
    tm = localtime(&t);
    /* read settings from file */
    readSettingsFromFile();

    /* initialize size for mem segments */
    shm_good_size = sizeof(good_t) * init_goods;
    shm_supply_size = sizeof(port_supply) * so_fill;
    shm_demand_size = sizeof(port_demand) * so_fill;
    shm_portsinfo_size = sizeof(ports_info) * init_ports;
    shm_vesselsinfo_size = sizeof(vessels_info); /* not necessary to have info per vessel */
    shm_goodsinfo_size = sizeof(goods_info) * init_goods;
    shm_weatherinfo_size = sizeof(info_weather) + (sizeof(int) * init_ports);
    shm_vessels_position_info_size = sizeof(vessel_position_info) * init_vessels;
    /* allocate memory for shm id's (need to remove shm segments) */
    shm_ids = calloc(12, sizeof(int));
    array_vessels_pid = calloc(init_vessels, sizeof(pid_t));

    /* get segments */
    shm_ids[0] = getGoodsSeg(&array_goods);
    shm_ids[1] = getSupplySeg(&array_supply);
    shm_ids[2] = getDemandSeg(&array_demand);
    shm_ids[3] = getPortsInfoSeg(&info_ports);
    shm_ids[4] = getVesselsInfoSeg(&info_vessels);
    shm_ids[5] = getGoodsInfoSeg(&array_goods_info);
    shm_ids[6] = getPortsNumber(&ports_number);
    shm_ids[7] = getTime(&time_left);
    shm_ids[8] = getSupplyDailyFill(&supply_daily_fill);
    shm_ids[9] = getDemandDailyFill(&demand_daily_fill);
    shm_ids[10] = getWeatherInfoSeg(&info_weather);
    shm_ids[11] = getVesselPositionInfoSeg(&info_position_vessels);
    /*get semaphore set */
    sem_id = get_sem();
    /* set the seed for rand() */
    srand(time(NULL));
    /* set relative pointer in shared memory */
    ports_hit = ((int *) info_weather + 3);
    /* initialize goods in shared memory */
    for (i = 0; i < init_goods; i++) {
        array_goods[i].good_type = i + 1; /* goods types starts from 1, zero indicates void good */
        array_goods[i].weight = (rand() % size) + 1;
        array_goods[i].lifetime = (rand() % (max_life - min_life + 1)) + min_life;
    }
    /* initialize goods info in shared memory */
    for (i = 0; i < init_goods; i++) {
        array_goods_info[i].good_type = i + 1; /* il tipo di merce parte da 1 la merce 0 è la merce vuota */
        array_goods_info[i].in_ports = 0;
        array_goods_info[i].on_vessels = 0;
        array_goods_info[i].expired_in_ports = 0;
        array_goods_info[i].expired_on_vessels = 0;
        array_goods_info[i].delivered_to_ports = 0;
    }
    /* update utility pointers in shm */
    createVessels();
    sem_cmd(sem_id, START_MAIN, WAIT_VESSELS, 0);
    createPorts();
    sem_cmd(sem_id, START_MAIN, WAIT_PORTS, 0);
    createWeather();

    /* this section manages the passing of days and performs the dump */
    day = 1;
    delay = 1; /* count 1 sec */
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    while (day <= sim_time) {
        *ports_number = init_ports;
        /* initialize daily fill */
        *supply_daily_fill += so_fill / sim_time;
        *demand_daily_fill += so_fill / sim_time;
        sem_cmd(sem_id, START_PORTS, init_ports, 0);
        sem_cmd(sem_id, START_MAIN, WAIT_PORTS, 0);
        /* check at the beginning of the day if exists demand and supply */
        if (array_supply[0].port_pid != 0 && array_demand[0].port_pid != 0) {
            elapsed_time = 0;
            sem_cmd(sem_id, START_VESSELS, init_vessels, 0);
            /* 	weather process starts */
            sem_cmd(sem_id, START_WEATHER, SIGNAL, 0);
            start_time = current_time;
            do {
                clock_gettime(CLOCK_MONOTONIC, &current_time);
                elapsed_time =
                        (current_time.tv_sec - start_time.tv_sec) + (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
                *time_left = delay - elapsed_time;
            } while (elapsed_time <= delay);
            /* stop processes at the end of the day */
            kill(info_weather->weather_pid, SIGTSTP);
            sem_cmd(sem_id, START_MAIN, WAIT, 0); /* wait for weather at value 1 */
            for (i = 0; i < init_vessels; i++) {
                kill(array_vessels_pid[i], SIGTSTP);
            }
            sem_cmd(sem_id, START_MAIN, WAIT_VESSELS, 0);

            for (i = 0; i < init_ports; i++) {
                kill(info_ports[i].pid, SIGTSTP);
            }
            sem_cmd(sem_id, START_MAIN, WAIT_PORTS, 0);

            /* update start time for the next iteration */
            openLog();
            fprintf(log_file, "END OF THE DAY %d:\n", day);

            /* update infos in array_goods_info and ports_info before performing the dump */
            i = 0;
            while (array_supply[i].port_pid != 0) {
                if (array_supply[i].quantity > 0 && array_supply[i].expiration_date > day) {
                    for (j = 0; j < init_goods; j++) {
                        /* goods in port */
                        if (array_supply[i].good_type == array_goods_info[j].good_type) {
                            array_goods_info[j].in_ports += array_supply[i].quantity;
                        }
                    }
                }
                i++;
            }
            i = 0;
            while (array_supply[i].port_pid != 0) {
                if (array_supply[i].expiration_date > day && array_supply[i].quantity > 0) {
                    for (j = 0; j < init_ports; j++) {
                        if (info_ports[j].pid == array_supply[i].port_pid) {
                            info_ports[j].goods_in_port += array_supply[i].quantity;
                        }
                    }
                }
                i++;
            }

            /* UPDATE PORTS INFO */
            fprintf(log_file, "\nINFO PORTS:\n");
            /* goods in port */
            for (i = 0; i < init_ports; i++) {
                if (info_ports[i].goods_in_port > 0) {
                    fprintf(log_file, "Port %d: %d crates of goods available for loading\n",
                            info_ports[i].pid, info_ports[i].goods_in_port);
                    info_ports[i].goods_in_port = 0;
                }
            }
            /* goods sent and received (it gets updated by the ship when it picks up the cargo at the port)*/
            for (i = 0; i < init_ports; i++) {
                if (info_ports[i].goods_sent > 0) {
                    fprintf(log_file, "Port %d: dispatched %d crates of goods\n",
                            info_ports[i].pid, info_ports[i].goods_sent);
                }
                if (info_ports[i].goods_received > 0) {
                    fprintf(log_file, "Port %d: received %d crates of goods\n",
                            info_ports[i].pid, info_ports[i].goods_received);
                }
            }
            /* docks and busy docks */
            fprintf(log_file, "Total docks for each port: %d\n", init_docks);
            for (i = 0; i < init_ports; i++) {
                docks_id = get_docks(info_ports[i].pid);
                busy_docks = init_docks - semctl(docks_id, DOCKS, GETVAL, 0);
                if (busy_docks > 0) {
                    fprintf(log_file, "Port %d: busy docks: %d\n", info_ports[i].pid, busy_docks);
                }
            }

            printf("Day %d:\nI updated the transport log with ports info\n", day);

            /* Update vessels info */
            fprintf(log_file, "\nINFO VESSELS:\n");
            fprintf(log_file, "Number of vessels traveling loaded: %d\n", info_vessels->traveling_loaded);
            fprintf(log_file, "Number of vessels traveling empty: %d\n", info_vessels->traveling_unloaded);
            fprintf(log_file, "Number of vessels in port: %d\n", info_vessels->in_port);

            printf("I updated the transport log with vessels info\n");

            /* UPDATE GOODS INFO */
            /* eliminate expired offers and subtract the expired quantity from infos array */
            i = 0;
            while (array_supply[i].port_pid != 0) {
                /* expired in ports */
                if (array_supply[i].expiration_date == day) {
                    for (j = 0; j < init_goods; j++) {
                        if (array_supply[i].good_type == array_goods_info[j].good_type) {
                            array_goods_info[j].expired_in_ports += array_supply[i].quantity;
                        }
                    }
                    /* delete from offers table */
                    array_supply[i].port_pid = 0;
                    array_supply[i].x = 0;
                    array_supply[i].y = 0;
                    array_supply[i].good_type = 0;
                    array_supply[i].quantity = 0;
                    array_supply[i].unit_weight = 0;
                    array_supply[i].expiration_date = 0;
                }
                i++;
            }
            fprintf(log_file, "\nINFO GOODS:\n");
            for (i = 0; i < init_goods; i++) {
                if (array_goods_info[i].in_ports > 0) {
                    fprintf(log_file, "Goods present at the port: %d crates of goods - type: %d\n",
                            array_goods_info[i].in_ports, array_goods_info[i].good_type);
                    array_goods_info[i].in_ports = 0;
                }
            }
            for (i = 0; i < init_goods; i++) {
                /* goods delivered */
                if (array_goods_info[i].delivered_to_ports > 0) {
                    fprintf(log_file, "Goods delivered: %d crates of goods - type: %d\n",
                            array_goods_info[i].delivered_to_ports, array_goods_info[i].good_type);
                }
            }
            for (i = 0; i < init_goods; i++) {
                /* on vessels travelling */
                if (array_goods_info[i].on_vessels > 0) {
                    fprintf(log_file, "Goods in transit: %d crates of goods - type: %d\n",
                            array_goods_info[i].on_vessels, array_goods_info[i].good_type);
                }
            }
            for (i = 0; i < init_goods; i++) {
                /* expired on vessels */
                if (array_goods_info[i].expired_on_vessels > 0) {
                    fprintf(log_file, "Expired goods on board: %d crates of goods - type: %d\n",
                            array_goods_info[i].expired_on_vessels, array_goods_info[i].good_type);
                }
            }
            for (i = 0; i < init_goods; i++) {
                if (array_goods_info[i].expired_in_ports > 0) {
                    fprintf(log_file, "Expired goods in port: %d crates of goods - type: %d\n",
                            array_goods_info[i].expired_in_ports, array_goods_info[i].good_type);
                }
            }

            fprintf(log_file, "\n");
            printf("I updated the transport log with goods info\n");


            fprintf(log_file, "\nINFO WEATHER:\n");
            fprintf(log_file, "Number of ships slowed down by the storm: %d\n", info_weather->vessels_hit_num);
            fprintf(log_file, "\nPorts affected by the swell:\n");
            for (j = 0; j < init_ports; j++) {
                if (*(ports_hit + j) != 0) { /* first free position in the array*/
                    fprintf(log_file, "Port %d\n", *(ports_hit + j));
                }
            }

            fprintf(log_file, "\n");
            printf("I updated the transport log with weather info\n");
            day++;
        } else {
            fprintf(stderr, "The supply or demand is equal to 0! Ending simulation!\n");
            shutdown();
        }
    }
    /* at the end of the simulation print the ports awards report */
    /* winning for production */
    max = info_ports[0].total_produced;
    for (i = 1; i < init_ports; i++) {
        if (info_ports[i].total_produced > max) {
            max = info_ports[i].total_produced;
        }
    }
    for (i = 0; i < init_ports; i++) {
        if (max == info_ports[i].total_produced) {
            winning_port_for_production = info_ports[i].pid;
        }
    }
    /* winning for demand */
    max = info_ports[0].total_requested;
    for (i = 1; i < init_ports; i++) {
        if (info_ports[i].total_requested > max) {
            max = info_ports[i].total_requested;
        }
    }
    for (i = 0; i < init_ports; i++) {
        if (max == info_ports[i].total_requested) {
            winning_port_for_demand = info_ports[i].pid;
        }
    }
    fprintf(log_file, "PORT AWARDS %d:\n", tm->tm_year + 1900);
    fprintf(log_file, "The winner of the port award for the most productive port is port number: %d\n",
            winning_port_for_production);
    fprintf(log_file, "The winner of the port award for the most demanding port is port number: %d\n",
            winning_port_for_demand);

    printf("The ports awards %d goes to:\nfor the most productive port wins the port: %d\nfor the most requesting port wins the port: %d\n",
           tm->tm_year + 1900, winning_port_for_production, winning_port_for_demand);

    shutdown();
    return 0;
}

void createPorts() {

    int i;
    double x, y = 0;
    char *str_coordinate_x, *str_coordinate_y, *str_init_ports, *str_init_goods;
    pid_t pid;
    char *args[] = {PORT_PATH, NULL, NULL, NULL, NULL, NULL};

    str_coordinate_x = (char *) malloc(sizeof(double));
    str_coordinate_y = (char *) malloc(sizeof(double));
    str_init_ports = (char *) malloc(sizeof(int));
    str_init_goods = (char *) malloc(sizeof(int));

    args[1] = str_coordinate_x;
    args[2] = str_coordinate_y;
    args[3] = str_init_ports;
    args[4] = str_init_goods;

    sprintf(str_init_ports, "%d", init_ports);
    sprintf(str_init_goods, "%d", init_goods);

    for (i = 0; i < init_ports; i++) {
        /* set coordinates for firsts 4 ports */
        if (i == 0) {
            x = 0;
            y = 0;
        } else if (i == 1) {
            x = init_map;
            y = 0;
        } else if (i == 2) {
            x = init_map;
            y = init_map;
        } else if (i == 3) {
            x = 0;
            y = init_map;
        } else {
            x = (((double) rand() / (double) (RAND_MAX)) * init_map);
            y = (((double) rand() / (double) (RAND_MAX)) * init_map);
        }

        sprintf(str_coordinate_x, "%f", x);
        sprintf(str_coordinate_y, "%f", y);

        switch (pid = fork()) {

            case -1:
                shutdown();
                break;

            case 0:
                execve(PORT_PATH, args, NULL);
                perror("Execve error: ");
                exit(EXIT_FAILURE);
                break;

            default:
                /* Update ports info for dump */
                info_ports[i].pid = pid;
                info_ports[i].goods_in_port = 0;
                info_ports[i].goods_sent = 0;
                info_ports[i].goods_received = 0;
                info_ports[i].n_docks = init_docks;
                info_ports[i].busy_docks = 0;
                info_ports[i].total_produced = 0;
                info_ports[i].total_requested = 0;
        }
    }
}

void createVessels() {
    int i;
    double x, y;
    char *str_coordinate_x, *str_coordinate_y, *str_sim_time, *str_init_goods, *str_init_ports, *str_init_vessels;
    pid_t pid;
    char *args[] = {VESSEL_PATH, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

    str_coordinate_x = (char *) malloc(sizeof(double));
    str_coordinate_y = (char *) malloc(sizeof(double));
    str_sim_time = (char *) malloc(sizeof(int));
    str_init_goods = (char *) malloc(sizeof(int));
    str_init_ports = (char *) malloc(sizeof(int));
    str_init_vessels = (char *) malloc(sizeof(int));

    args[1] = str_coordinate_x;
    args[2] = str_coordinate_y;
    args[3] = str_sim_time;
    args[4] = str_init_goods;
    args[5] = str_init_ports;
    args[6] = str_init_vessels;

    sprintf(str_sim_time, "%d", sim_time);
    sprintf(str_init_goods, "%d", init_goods);
    sprintf(str_init_ports, "%d", init_ports);
    sprintf(str_init_vessels, "%d", init_vessels);

    for (i = 0; i < init_vessels; i++) {

        x = (((double) rand() / (double) (RAND_MAX)) * init_map);
        y = (((double) rand() / (double) (RAND_MAX)) * init_map);

        sprintf(str_coordinate_x, "%f", x);
        sprintf(str_coordinate_y, "%f", y);

        switch (pid = fork()) {

            case -1:
                shutdown();
                break;

            case 0:
                execve(VESSEL_PATH, args, NULL);
                perror("Execve error: ");
                exit(1);
                break;

            default:
                /* update array vessels pid */
                array_vessels_pid[i] = pid;
        }
    }
}

void createWeather() {
    pid_t pid;
    char *str_init_ports, *str_init_vessels;
    char *args[] = {WEATHER_PATH, NULL, NULL, NULL};
    str_init_ports = (char *) malloc(sizeof(int));
    str_init_vessels = (char *) malloc(sizeof(int));
    args[1] = str_init_ports;
    args[2] = str_init_vessels;

    sprintf(str_init_ports, "%d", init_ports);
    sprintf(str_init_vessels, "%d", init_vessels);

    switch (pid = fork()) {

        case -1:
            shutdown();
            break;
        case 0:
            execve(WEATHER_PATH, args, NULL);
            perror("Execve error: ");
            exit(1);
            break;

        default:
            break;
    }
}

void readSettingsFromFile() {

    char *key, *value;
    FILE *stream;

    key = calloc(80, sizeof(char));
    value = calloc(80, sizeof(char));

    if ((stream = fopen(SETTINGS_PATH, "r")) == NULL) {
        perror("fopen error");
        exit(EXIT_FAILURE);
    }
    while (fscanf(stream, "%s %s", key, value) != EOF) {

        if (strcmp(key, "SO_VESSELS") == 0) {
            init_vessels = atoi(value);
        } else if (strcmp(key, "SO_PORTS") == 0) {
            init_ports = atoi(value);
            if (init_ports < 4) {
                printf("The number of ports must be >= 4\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_GOODS") == 0) {
            init_goods = atoi(value);
            if (init_goods < 1) {
                printf("The number of goods must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_DAYS") == 0) {
            sim_time = atoi(value);
            if (sim_time < 1) {
                printf("The number of simulation days must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_SIZE") == 0) {
            size = atoi(value);
            if (size < 1) {
                printf("The maximum weight of the goods must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_MIN_VITA") == 0) {
            min_life = atoi(value);
            if (min_life < 1) {
                printf("The minimum expiration date of the goods must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_MAX_VITA") == 0) {
            max_life = atoi(value);
            if (max_life < 1) {
                printf("The maximum expiration date of the goods must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_SPEED") == 0) {
            vessels_speed = atoi(value);
            if (vessels_speed < 1) {
                printf("The speed of the vessels must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_CAPACITY") == 0) {
            vessels_capacity = atoi(value);
            if (vessels_capacity < 1) {
                printf("The capacity of the vessels must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_DOCKS") == 0) {
            init_docks = atoi(value);
            if (init_docks < 1) {
                printf("SO_DOCKS represents the docks of the port, it must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_FILL") == 0) {
            so_fill = atoi(value);
            if (so_fill < 1) {
                printf("SO_FILL represents the quantity of goods produced during the simulation, it must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_LOADSPEED") == 0) {
            load_speed = atoi(value);
            if (load_speed < 1) {
                printf("The loading/unloading speed must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_STORM_DURATION") == 0) {
            storm_duration = atoi(value);
            if (storm_duration < 1) {
                printf("The storm duration must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_SWELL_DURATION") == 0) {
            swell_duration = atoi(value);
            if (swell_duration < 1) {
                printf("The swell duration must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else if (strcmp(key, "SO_LATO") == 0) {
            init_map = atof(value);
            if (init_map < 1) {
                printf("The size of the map must be >= 1\n");
                exit(EXIT_SUCCESS);
            }
        } else {
            fprintf(stderr, "variable %s not defined\n", key);
        }
    }
    free(key);
    free(value);
    if (fclose(stream) == EOF) {
        perror("fclose error");
        exit(EXIT_FAILURE);
    }
}

void openLog() {
    if (day == 1) {
        log_file = fopen("logbook.txt", (day == 1) ? "w" : "a");
        if (log_file == NULL) {
            fprintf(stderr, "Error opening file!\n");
            return;
        }
    }
}

void closeAndFlushLog() {
    fflush(log_file);
    fclose(log_file);
}

void shutdown() {
    int i;
    sigset_t mask;
    sigfillset(&mask);
    sigprocmask(SIG_SETMASK, &mask, NULL);

    if (log_file != NULL) {
        fprintf(log_file, "It was a pleasure playing with you, until next time!\n");
    }
    fprintf(stderr, "It was a pleasure playing with you, until next time!\n");
    do {
        if (info_ports != NULL) {
            for (i = 0; i < init_ports; i++) {
                kill(info_ports[i].pid, SIGTERM);
            }
        }
        if (array_vessels_pid != NULL) {
            for (i = 0; i < init_vessels; i++) {
                kill(array_vessels_pid[i], SIGTERM);
            }
        }
        if (info_weather->weather_pid != 0) {
            kill(info_weather->weather_pid, SIGTERM);

        }
    } while (wait(NULL) != -1);
    shmdt(array_goods);
    shmdt(array_supply);
    shmdt(array_demand);
    shmdt(info_ports);
    shmdt(info_vessels);
    shmdt(array_goods_info);
    shmdt(ports_number);
    shmdt(time_left);
    shmdt(supply_daily_fill);
    shmdt(demand_daily_fill);
    shmdt(info_weather);
    shmdt(info_position_vessels);
    for (i = 0; i < 12; i++) {
        shmctl(shm_ids[i], IPC_RMID, NULL);
    }
    semctl(sem_id, 0, IPC_RMID);
    free(shm_ids);
    closeAndFlushLog();
}

void sig_handler(int signum) {
    fprintf(stderr, "Received signal %s, ending simulation ...\n", strsignal(signum));
    shutdown();
}

void sigchld_handler(int signum) {
    fprintf(stderr, "Received sigchld\n");
}

void sigsegv_handler(int signum) {
    fprintf(stderr, "Received segmentation fault, ending simulation ...\n");
    shutdown();
}

void sigusr1_handler(int signum) {
    printf("Received sigusr1 fault, ending simulation\n");
    /*shutdown();*/
}