#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include "common.h"

#define SHMV_LOCK     while(sem_cmd(sem_id, VESSELS_INFO_SEM, WAIT, 0) == -1 && errno == EINTR); block_signals(1, SIGTSTP);
#define SHMV_UNLOCK   sem_cmd(sem_id, VESSELS_INFO_SEM, SIGNAL, 0); unblock_signals(1, SIGTSTP);


/*_________global_var_________*/

/* global pointers to shm */
port_supply *array_port_supply, *my_supply_pointer;
port_demand *array_port_demand, *my_demand_pointer;
double *time_left;                                          /* used to calculate trasport updated by main */
/* pointers for dump */
vessels_info *info_vessels;
vessel_position_info *info_position_vessels;
goods_info *array_goods_info;
ports_info *info_ports;
weather_info *info_weather;

int vessel_index;
/* coordinates */
double x, y;
/* IDs */
int sem_id, sim_time, init_goods;
/* used to update expiration date while navigating */
supply *supply_on_board;

/*_________prototypes_________*/

double
t_time(double x, double y, double x1, double y1);    /* calculate travel time from current position to destination */
int calculate_max_weight(port_supply *departure, port_demand *arrival, double time);    /* calculate the maximum transportable weight within "time" */
int calculate_transport();
void sail_to_port(port_supply *departure, port_demand *arrival, int quantity);
int search_vessel_in_info_position(int pid);
int find_first_free_position();
void sigtstp_handler(int signum);
void sigterm_handler(int signum);
void readSettingsFromFile();
void sigusr1_handler(int signum);
void sigsegv_handler(int signum);
void sigint_handler(int signum);

/*-----------------------------------------------------------------------------------------------------------------------*/

int main(int argc, char *argv[]) {
    /* set handlers */
    set_handler(SIGTSTP, sigtstp_handler);
    set_handler(SIGTERM, sigterm_handler);
    set_handler(SIGSEGV, sigsegv_handler);
    set_handler(SIGUSR1, sigusr1_handler);
    /* convert command line arguments */
    x = atof(argv[1]);
    y = atof(argv[2]);
    sim_time = atoi(argv[3]);
    init_goods = atoi(argv[4]);
    init_ports = atoi(argv[5]);
    init_vessels = atoi(argv[6]);
    /* get segments */
    getSupplySeg(&array_port_supply);
    getDemandSeg(&array_port_demand);
    getPortsInfoSeg(&info_ports);
    getVesselsInfoSeg(&info_vessels);
    getGoodsInfoSeg(&array_goods_info);
    getVesselPositionInfoSeg(&info_position_vessels);
    getTime(&time_left);
    getWeatherInfoSeg(&info_weather);
    /* get info about vessels from file settings */
    readSettingsFromFile();
    /* get global sem  */
    sem_id = get_sem();
    day = 1;

    SHMV_LOCK
    /* adding pid in the vessels' info_position_vessels array */
    vessel_index = search_vessel_in_info_position(getpid());
    info_position_vessels[vessel_index].vessel_pid = getpid();
    SHMV_UNLOCK

    sem_cmd(sem_id, START_MAIN, SIGNAL, 0);  /* unlock the main process after creating vessels */
    /* stop the process until it is unlocked by the main */
    sem_cmd(sem_id, START_VESSELS, WAIT, 0);
    while (1) {
        if (calculate_transport() < 0) {
            pause();
        }
    }
}

int calculate_transport() {
    int i, j, load_weight = 0;
    short found = 0;
    /* departure: index of the offer, arrival: index of demand */
    int departure, arrival, quantity_to_load;
    /* check the minimum expiration date */
    while (sem_cmd(sem_id, VESSELS_SEM, WAIT, 0) == -1 && errno == EINTR);
    /* if the minimum expiration date is today the delivery has priority */
    i = 0;
    while (array_port_supply[i].port_pid != 0 && !found) {
        j = 0;
        while (array_port_demand[j].port_pid != 0) {
            if (array_port_demand[j].good_type == array_port_supply[i].good_type) {   /* check if exist demand */
                if ((load_weight = calculate_max_weight(&array_port_supply[i], &array_port_demand[j],
                                                        array_port_supply[i].expiration_date - day + *time_left)) > 0) {
                    departure = i;
                    arrival = j;
                    quantity_to_load = load_weight / array_port_supply[i].unit_weight;
                    found = 1;
                    break;
                }
            }
            j++;
        }
        i++;
    }
    if (quantity_to_load > 0) {
        block_signals(1, SIGTSTP);              /* mask SIGTSTP signal when perform write in shm */
        /* book the supply and demand in ports */
        array_port_supply[departure].quantity -= quantity_to_load;
        array_port_demand[arrival].quantity -= quantity_to_load;
        sem_cmd(sem_id, VESSELS_SEM, SIGNAL, 0);
        unblock_signals(1, SIGTSTP);
        sail_to_port(&array_port_supply[departure], &array_port_demand[arrival], quantity_to_load);
    } else {
        sem_cmd(sem_id, VESSELS_SEM, SIGNAL, 0);
    }
    return -1;
}

void sail_to_port(port_supply *departure, port_demand *arrival, int quantity) {
    pid_t departure_pid;
    int i, docks_id, msgq_id, check_weight, departure_good, departure_exp;
    double time;
    /* store infos */
    departure_pid = departure->port_pid;
    departure_good = departure->good_type;
    departure_exp = departure->expiration_date;
    /* used to calculate check_weight since in shm quantity already decremented */
    my_supply_pointer = malloc(sizeof(port_supply));
    my_demand_pointer = malloc(sizeof(port_demand));
    memcpy(my_supply_pointer, departure, sizeof(port_supply));
    memcpy(my_demand_pointer, arrival, sizeof(port_demand));
    my_supply_pointer->quantity += quantity;
    my_demand_pointer->quantity += quantity;
    /* update info vessels */
    SHMV_LOCK
    info_vessels->traveling_unloaded += 1;

    info_position_vessels[vessel_index].in_port = 0;
    SHMV_UNLOCK
    /* travel to departure port */
    time = t_time(x, y, departure->x, departure->y);
    working(time);
    /* update position */
    x = departure->x;
    y = departure->y;
    /* update info vessels*/
    SHMV_LOCK
    info_vessels->traveling_unloaded -= 1;
    info_vessels->in_port += 1;
    info_position_vessels[vessel_index].in_port = 1;
    SHMV_UNLOCK
    /* get access to dock */
    docks_id = get_docks(departure->port_pid);
    while (sem_cmd(docks_id, DOCKS, WAIT, 0) == -1 && errno == EINTR);
    check_weight = calculate_max_weight(my_supply_pointer, my_demand_pointer,my_supply_pointer->expiration_date - day + *time_left);
    free(my_supply_pointer);
    my_supply_pointer = NULL;
    free(my_demand_pointer);
    my_demand_pointer = NULL;

    /* check if after having waited it is still possible to transport some quantity, else re-calculate transport */
    if (check_weight > 0) {
        check_weight = check_weight / departure->unit_weight;
        SHMV_LOCK
        departure->quantity += quantity - check_weight;
        arrival->quantity += quantity - check_weight;
        SHMV_UNLOCK
        quantity = check_weight;
        /*  performs loading operations */
        time = (departure->unit_weight * quantity) / load_speed;
        working(time);

        supply_on_board = malloc(sizeof(supply));
        supply_on_board->expiration_date = departure->expiration_date;
        supply_on_board->good_type = departure->good_type;
        supply_on_board->quantity = quantity;
        supply_on_board->lot_id = getpid();

        /* release dock */
        sem_cmd(docks_id, DOCKS, SIGNAL, 0);
        /* update info vessels and array goods info in shm */
        SHMV_LOCK
        for (i = 0; i < init_ports; i++) {
            if (info_ports[i].pid == departure->port_pid) {
                info_ports[i].goods_sent += quantity;
            }
        }
        for (i = 0; i < init_goods; i++) {
            if (array_goods_info[i].good_type == supply_on_board->good_type) {
                array_goods_info[i].on_vessels += supply_on_board->quantity;
            }
        }
        info_vessels->traveling_loaded += 1;
        info_vessels->in_port -= 1;
        info_position_vessels[vessel_index].in_port = 0;
        SHMV_UNLOCK
        /* travel to arrival port */
        time = t_time(x, y, arrival->x, arrival->y);
        working(time);
        /* update position */
        x = arrival->x;
        y = arrival->y;

        /* update info */
        SHMV_LOCK
        info_vessels->traveling_loaded -= 1;
        info_vessels->in_port += 1;
        info_position_vessels[vessel_index].in_port = 1;
        SHMV_UNLOCK

        /* get access to arrival dock */
        docks_id = get_docks(arrival->port_pid);
        while (sem_cmd(docks_id, DOCKS, WAIT, 0) == -1 && errno == EINTR);
        /* check if after having waited at the docks the supply on board hasn't expired */
        if (supply_on_board != NULL) {
            time = (departure->unit_weight * quantity) / load_speed;
            working(time);
        }
        /* check if it's not expired during unload operations */
        if (supply_on_board != NULL) {
            block_signals(1, SIGTSTP);
            msgq_id = get_msg(arrival->port_pid);
            if (snd_msg(msgq_id, supply_on_board) == -1 &&
                errno == EAGAIN) {  /* if reached the limit of the message queue */
                sem_cmd(sem_id, VESSELS_INFO_SEM, WAIT, 0);
                for (i = 0; i < init_goods; i++) {
                    if (array_goods_info[i].good_type == supply_on_board->good_type) {
                        array_goods_info[i].delivered_to_ports += quantity; /* it's not ship's business if the port can't receive */
                        array_goods_info[i].on_vessels -= quantity;
                    }
                }
                free(supply_on_board);
                supply_on_board = NULL;
                info_vessels->in_port -= 1;
                info_position_vessels[vessel_index].in_port = 0;
                sem_cmd(docks_id, DOCKS, SIGNAL, 0);
                SHMV_UNLOCK
                calculate_transport();
            }
            /* update shm infos */
            sem_cmd(sem_id, VESSELS_INFO_SEM, WAIT, 0);
            for (i = 0; i < init_goods; i++) {
                if (array_goods_info[i].good_type == supply_on_board->good_type) {
                    array_goods_info[i].delivered_to_ports += quantity;
                    array_goods_info[i].on_vessels -= quantity;
                }
            }
            free(supply_on_board);
            supply_on_board = NULL;
        }
            /* if expired waiting at the dock resume the quantity */
        else {
            SHMV_LOCK
            arrival->quantity += quantity;
        }
        info_vessels->in_port -= 1;
        info_position_vessels[vessel_index].in_port = 0;
        sem_cmd(docks_id, DOCKS, SIGNAL, 0);
        SHMV_UNLOCK
        calculate_transport();
    } else {
        SHMV_LOCK
        /* since the offer could be expired and replaced by another check if it's still the same */
        if (departure->port_pid == departure_pid && departure->good_type == departure_good &&
            departure->expiration_date == departure_exp) {
            departure->quantity += quantity;
        }
        arrival->quantity += quantity;
        info_vessels->in_port -= 1;
        info_position_vessels[vessel_index].in_port = 0;
        sem_cmd(docks_id, DOCKS, SIGNAL, 0);
        SHMV_UNLOCK
        calculate_transport();
    }
}

double t_time(double x, double y, double x1, double y1) {
    double tmp;
    tmp = (sqrt(pow((x1 - x), 2) + pow((y1 - y), 2)) / vessels_speed);
    return tmp;
}

int calculate_max_weight(port_supply *departure, port_demand *arrival, double time) {
    double tmp, max_load_time;
    int max_load_weight, current_weight, quantity;

    if (arrival->quantity == 0) {
        max_load_weight = 0;
    } else {
        tmp = (sqrt(pow((departure->x - x), 2) + pow((departure->y - y), 2)) / vessels_speed)
                + (sqrt(pow((arrival->x - departure->x), 2) + pow((arrival->y - departure->y), 2)) / vessels_speed);
        max_load_time = (time - tmp) / 2;
        max_load_weight = max_load_time * load_speed;
        if (max_load_weight > 0) {
            current_weight = departure->quantity * departure->unit_weight;
            /* current weight represents the weight of all good which type is departure->good_type of the departure port */
            max_load_weight = max_load_weight < current_weight ? max_load_weight : current_weight;
            if (max_load_weight > vessels_capacity) {
                max_load_weight = vessels_capacity / departure->unit_weight * departure->unit_weight;
            } else {
                max_load_weight = max_load_weight / departure->unit_weight * departure->unit_weight;
            }
            quantity = max_load_weight / departure->unit_weight;
            if (quantity > arrival->quantity) {
                max_load_weight = arrival->quantity * departure->unit_weight;
            }
        }
    }
    return max_load_weight;
}

int search_vessel_in_info_position(int vessel_pid) {
    int j;
    for (j = 0; j < init_vessels; j++) {
        if (info_position_vessels[j].vessel_pid == vessel_pid) {
            return j;
        }
    }

    return find_first_free_position();
}

int find_first_free_position() {
    int j = 0;
    for (; j < init_vessels; j++) {
        if (info_position_vessels[j].vessel_pid == 0) {
            return j;
        }
    }
    return j;
}

void sigtstp_handler(int signum) {
    int i;
    block_signals(1, SIGTERM);
    sem_cmd(sem_id, HANDLER_V_SEM, WAIT, 0);
    if (supply_on_board != NULL && supply_on_board->expiration_date == day) {
        for (i = 0; i < init_goods; i++) {
            if (array_goods_info[i].good_type == supply_on_board->good_type) {
                array_goods_info[i].expired_on_vessels += supply_on_board->quantity;
                array_goods_info[i].on_vessels -= supply_on_board->quantity;
            }
        }
        free(supply_on_board);
        supply_on_board = NULL;
    }
    day++;
    sem_cmd(sem_id, START_MAIN, SIGNAL, 0);
    sem_cmd(sem_id, HANDLER_V_SEM, SIGNAL, 0);
    unblock_signals(1, SIGTERM);
    sem_cmd(sem_id, START_VESSELS, WAIT, 0);
}

void sigterm_handler(int signum) {
    shmdt(array_port_supply);
    shmdt(array_port_demand);
    shmdt(info_ports);
    shmdt(info_vessels);
    shmdt(array_goods_info);
    shmdt(time_left);
    shmdt(info_position_vessels);
    if (supply_on_board != NULL) {
        free(supply_on_board);
    }
    if (my_supply_pointer != NULL) {
        free(my_supply_pointer);
    }
    if (my_demand_pointer != NULL) {
        free(my_demand_pointer);
    }
    exit(0);
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

        if (strcmp(key, "SO_SPEED") == 0) {
            vessels_speed = atoi(value);
        } else if (strcmp(key, "SO_CAPACITY") == 0) {
            vessels_capacity = atoi(value);
        } else if (strcmp(key, "SO_FILL") == 0) {
            so_fill = atoi(value);
        } else if (strcmp(key, "SO_LOADSPEED") == 0) {
            load_speed = atoi(value);
        } else if (strcmp(key, "SO_STORM_DURATION") == 0) {
            storm_duration = atoi(value);
        }

    }
    free(key);
    free(value);
    if (fclose(stream) == EOF) {
        perror("fclose error");
        exit(EXIT_FAILURE);
    }
}

void sigusr1_handler(int signum) {
    /* struct timespec req, rem; */
    double time;
    block_signals(3, SIGTSTP, SIGTERM, SIGCHLD);
    sem_cmd(sem_id, WEATHER_SEM, WAIT, 0);
    /* storm_duration; 1s : 24h = 0,042s : 1h */
    time = 42000000 / 1e9 * storm_duration;
    working(time);
    info_weather->vessels_hit_num += 1;
    sem_cmd(sem_id, WEATHER_SEM, SIGNAL, 0);
    unblock_signals(3, SIGTSTP, SIGTERM, SIGCHLD);
}

void sigsegv_handler(int signum) {
    fprintf(stderr, "I'm the ship %d, I received SEGFAULT", getpid());
    sem_cmd(sem_id, START_MAIN, SIGNAL, 0);
    shmdt(array_port_supply);
    shmdt(array_port_demand);
    shmdt(info_ports);
    shmdt(info_vessels);
    shmdt(array_goods_info);
    shmdt(time_left);
    shmdt(info_position_vessels);
    shmdt(info_weather);
    if (supply_on_board != NULL) {
        free(supply_on_board);
    }
    abort();
}

void sigint_handler(int signum) {
    fprintf(stderr, "I'm the ship %d, I received SIGINT", getpid());
    sem_cmd(sem_id, START_MAIN, SIGNAL, 0);
    shmdt(array_port_supply);
    shmdt(array_port_demand);
    shmdt(info_ports);
    shmdt(info_vessels);
    shmdt(array_goods_info);
    shmdt(time_left);
    shmdt(info_position_vessels);
    shmdt(info_weather);
    if (supply_on_board != NULL) {
        free(supply_on_board);
    }
    exit(EXIT_FAILURE);
}

