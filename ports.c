#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <time.h>
#include <sys/shm.h>
#include <signal.h>
#include "common.h"

struct index {
    int val;
    struct index *next;
};

/*_________global_var_________*/

struct index *offers_list = NULL, *demands_list = NULL;
/* pointers to shm */
good_t *array_goods;
port_supply *array_port_supply;
port_demand *array_port_demand;
int *ports_number;      /* counts the running port */
/* global pointers to shm for dump */
ports_info *info_ports;
goods_info *array_goods_info;
weather_info *info_weather;
/* coordinates */
double x, y;
/* IDs */
pid_t mypid;
int sem_id, docks_id, msgqid;
/* utility shm pointers */
int *ports_number;
int *ports_hit;
long *supply_daily_fill, *demand_daily_fill;

/*_________prototypes_________*/

void insert_createable(struct index **head, int val);       /* insert createable index in offers list */
int remove_createable(struct index **head, int val);        /* insert daycreateable index in demands list */
int same_type_offer(int type, int expiration);       /* ensure to create a unique lot of same type with the same expiration date */
int same_type_demand(int type);                      /* same for demand */
void freeIndexList(struct index *head);              /* free the indexes lists */
void sigtstp_handler(int signum);
void sigterm_handler(int signum);
void sigint_handler(int signum);
void sigsegv_handler(int signum);
void sigusr1_handler(int signum);
void readSettingsFromFile();

/*--------------------------------------------------------------------------------------------------------------------*/

int main(int argc, char *argv[]) {

    int i, index_goods, n_goods, life, type_good, counter_offers, counter_demands, init_goods;
    int mydaily_supply, mydaily_demand, daily_supply_produced = 0, daily_demand_produced = 0;
    struct index *index_list_head;

    /* set handlers */
    set_handler(SIGTSTP, sigtstp_handler);
    set_handler(SIGTERM, sigterm_handler);
    set_handler(SIGINT, sigint_handler);
    set_handler(SIGSEGV, sigsegv_handler);
    set_handler(SIGUSR1, sigusr1_handler);
    /* convert command line arguments */
    x = atof(argv[1]);
    y = atof(argv[2]);
    init_ports = atoi(argv[3]);
    init_goods = atoi(argv[4]);

    readSettingsFromFile();
    /* get segments */
    getGoodsSeg(&array_goods);
    getSupplySeg(&array_port_supply);
    getDemandSeg(&array_port_demand);
    getPortsInfoSeg(&info_ports);
    getGoodsInfoSeg(&array_goods_info);
    getPortsNumber(&ports_number);
    getSupplyDailyFill(&supply_daily_fill);
    getDemandDailyFill(&demand_daily_fill);
    getWeatherInfoSeg(&info_weather);

    mypid = getpid();
    /* set relative pointer in shared memory */
    ports_hit = (int *) (info_weather + 3);
    /* get global sem and specific docks*/
    sem_id = get_sem();
    docks_id = get_docks(mypid);
    /* get the msgqid for the port*/
    msgqid = get_msg(mypid);
    /* set the seed for rand() */
    srand(time(NULL));
    /* count the ports */

    day = 1;
    sem_cmd(sem_id, START_MAIN, SIGNAL, 0);
    /* stop process until it gets the start signal */
    sem_cmd(sem_id, START_PORTS, WAIT, 0);

    /* this section manages the creating of offers and demands and performs the dump */
    while (1) {
        /* initialize list of indexes of createable supply and demands */
        counter_offers = 0;
        for (i = 0; i < init_goods; i++) {
            insert_createable(&offers_list, i);
            counter_offers++;
        }
        counter_demands = 0;
        for (i = 0; i < init_goods; i++) {
            insert_createable(&demands_list, i);
            counter_demands++;
        }
        /* create supply */
        sem_cmd(sem_id, PORTS_SUPPLY_SEM, WAIT, 0);
        i = 0;
        while (array_port_demand[i].port_pid != 0) {
            if (array_port_demand[i].port_pid == mypid) {
                if ((remove_createable(&offers_list, array_port_demand[i].good_type - 1)) == 0) {
                    counter_offers--;
                }
            }
            i++;
        }
        *ports_number -= 1;
        if (counter_offers > 0 && *supply_daily_fill > 0) {
            if (init_goods <= 3 && *ports_number <= init_ports / 2) {
                mydaily_supply = *supply_daily_fill / 2;
            } else {
                mydaily_supply =
                        (rand() % (*supply_daily_fill)) / 10;    /* calculate the supply daily fill for the port */
            }
            if (*ports_number <= 1) {
                mydaily_supply = *supply_daily_fill;            /* last port makes sure that the daily fill is fulfilled */
            }
            while (mydaily_supply > 0) {
                /* calculate random type of good and quantity to be created and expiration date */
                index_goods = (rand() % (counter_offers));
                index_list_head = offers_list;
                for (i = 0; i < index_goods; i++) {
                    index_list_head = index_list_head->next;
                }
                index_goods = index_list_head->val;
                if ((mydaily_supply / array_goods[index_goods].weight) > 0) {
                    n_goods = (rand() % (mydaily_supply / array_goods[index_goods].weight) + 1);
                } else {
                    n_goods = 1;    /* create one unit if the weight of the good exceeds mydaily_supply */
                }
                life = (day - 1) + array_goods[index_goods].lifetime;
                type_good = array_goods[index_goods].good_type;
                if (!same_type_offer(type_good, life)) {
                    /* update the offers table in shared memory*/
                    for (i = 0; i < so_fill; i++) {
                        if (array_port_supply[i].port_pid == 0) {
                            array_port_supply[i].port_pid = mypid;
                            array_port_supply[i].x = x;
                            array_port_supply[i].y = y;
                            array_port_supply[i].good_type = type_good;
                            array_port_supply[i].quantity = n_goods;
                            array_port_supply[i].unit_weight = array_goods[index_goods].weight;
                            array_port_supply[i].expiration_date = life;
                            break;
                        }
                    }
                } else {
                    /* if same type adds the quantity to the existing lot */
                    i = 0;
                    while (array_port_supply[i].port_pid != 0) {
                        if ((array_port_supply[i].port_pid == mypid) && (array_port_supply[i].good_type == type_good) &&
                            (array_port_supply[i].expiration_date == life)) {
                            array_port_supply[i].quantity += n_goods;
                            break;
                        }
                        i++;
                    }
                    /* update values */
                }
                mydaily_supply -= (array_goods[index_goods].weight) * n_goods;
                daily_supply_produced += (array_goods[index_goods].weight) * n_goods;
                /* update the total production of the port */

            }
            for (i = 0; i < init_ports; i++) {
                if (info_ports[i].pid == mypid) {
                    info_ports[i].total_produced += daily_supply_produced;
                }
            }
            *supply_daily_fill -= daily_supply_produced;
        }
        daily_supply_produced = 0;
        sem_cmd(sem_id, PORTS_SUPPLY_SEM, SIGNAL, 0);

        /* create demand */
        sem_cmd(sem_id, PORTS_DEMAND_SEM, WAIT, 0);
        /* remove indexes of goods in shm offers table from demands list */
        i = 0;
        while (array_port_supply[i].port_pid != 0) {
            if (array_port_supply[i].port_pid == mypid) {
                if ((remove_createable(&demands_list, array_port_supply[i].good_type - 1)) == 0) {
                    counter_demands--;
                }
            }
            i++;
        }
        if (counter_demands > 0 && *demand_daily_fill > 0) {
            if (*ports_number <= init_ports / 10) {
                mydaily_demand = *demand_daily_fill / 2;
            } else {
                mydaily_demand = (rand() % (*demand_daily_fill)) / 10;
            }
            if (*ports_number <= 1) {
                mydaily_demand = *demand_daily_fill;
            }
            while (mydaily_demand > 0) {
                /* calculate random type of good and quantity to be created and expiration date */
                index_goods = (rand() % (counter_demands));
                index_list_head = demands_list;
                for (i = 0; i < index_goods; i++) {
                    index_list_head = index_list_head->next;
                }
                index_goods = index_list_head->val;
                if ((mydaily_demand / array_goods[index_goods].weight) > 0) {
                    n_goods = (rand() % (mydaily_demand / array_goods[index_goods].weight) + 1);
                } else {
                    n_goods = 1;    /* create at least one pallet if the weight of the good exceed mydaily_supply */
                }
                type_good = array_goods[index_goods].good_type;
                if (!same_type_demand(type_good)) {
                    /* if I'm creating a new demand I update the demands table in shared memory */
                    for (i = 0; i < so_fill; i++) {
                        if (array_port_demand[i].port_pid == 0) {
                            array_port_demand[i].port_pid = mypid;
                            array_port_demand[i].x = x;
                            array_port_demand[i].y = y;
                            array_port_demand[i].good_type = type_good;
                            array_port_demand[i].quantity = n_goods;
                            break;
                        }
                    }
                } else {
                    /* if I'm producing the same demand I add the quantity to the existing lot */
                    i = 0;
                    while (array_port_demand[i].port_pid != 0) {
                        if ((array_port_demand[i].port_pid == mypid) && (array_port_supply[i].good_type == type_good)) {
                            array_port_demand[i].quantity += n_goods;
                            break;
                        }
                        i++;
                    }
                }
                /* update values */
                mydaily_demand -= array_goods[index_goods].weight * n_goods;
                daily_demand_produced += array_goods[index_goods].weight * n_goods;
            }
            for (i = 0; i < init_ports; i++) {
                if (info_ports[i].pid == mypid) {
                    info_ports[i].total_requested += daily_demand_produced;
                }
            }
            /* update the total demand of the port */
            *demand_daily_fill -= daily_demand_produced;
        }
        daily_demand_produced = 0;
        /* after the daily supply and demand have been created unblock main process */
        sem_cmd(sem_id, START_MAIN, SIGNAL, 0);
        sem_cmd(sem_id, PORTS_DEMAND_SEM, SIGNAL, 0);
        pause();
    }
}

int same_type_offer(int type, int expiration) {
    int i = 0;
    while (array_port_supply[i].port_pid != 0) {
        if ((array_port_supply[i].port_pid == mypid) && (array_port_supply[i].good_type == type) &&
            (array_port_supply[i].expiration_date == expiration)) {
            return 1;
        }
        i++;
    }
    return 0;
}

int same_type_demand(int type) {
    int i = 0;
    while (array_port_demand[i].port_pid != 0) {
        if ((array_port_demand[i].port_pid == mypid) && (array_port_demand[i].good_type == type)) {
            return 1;
        }
        i++;
    }
    return 0;
}

void insert_createable(struct index **head, int val) {
    struct index *new_index;
    if (*head == NULL) {
        new_index = (struct index *) malloc(sizeof(struct index));
        new_index->val = val;
        new_index->next = *head;
        *head = new_index;
    } else {
        insert_createable(&(*head)->next, val);
    }
}

int remove_createable(struct index **head, int val) {
    struct index *tmp;
    if (*head == NULL) {
        return -1;
    }
    if ((*head)->val == val) {
        tmp = *head;
        *head = (*head)->next;
        free(tmp);
        return 0;
    } else {
        return remove_createable(&(*head)->next, val);
    }
}

void freeIndexList(struct index *head) {
    struct index *tmp;
    while (head != NULL) {
        tmp = head;
        head = head->next;
        free(tmp);
    }
}

void sigtstp_handler(int signum) {
    int i;
    supply lot;
    block_signals(2, SIGTSTP, SIGTERM);
    sem_cmd(sem_id, PORTS_SUPPLY_SEM, WAIT, 0);
    while (msgrcv(msgqid, &lot, sizeof(supply), 0, IPC_NOWAIT) != -1) {
        for (i = 0; i < init_ports; i++) {
            if (info_ports[i].pid == mypid) {
                info_ports[i].goods_received += lot.quantity;
            }
        }
    }
    day++;
    sem_cmd(sem_id, START_MAIN, SIGNAL, 0);
    sem_cmd(sem_id, PORTS_SUPPLY_SEM, SIGNAL, 0);
    unblock_signals(2, SIGTSTP, SIGTERM);
    sem_cmd(sem_id, START_PORTS, WAIT, 0);
}

void sigterm_handler(int signum) {
    shmdt(array_goods);
    shmdt(array_port_supply);
    shmdt(array_port_demand);
    shmdt(info_ports);
    shmdt(ports_number);
    shmdt(supply_daily_fill);
    shmdt(demand_daily_fill);
    shmdt(info_weather);
    freeIndexList(offers_list);
    freeIndexList(demands_list);
    rm_sem(docks_id);
    rm_msg(msgqid);
    exit(0);
}

void sigusr1_handler(int signum) {
    int i, j, alreadyHit = 0;
    double time;
    /* struct timespec req, rem; */
    block_signals(3, SIGTSTP, SIGTERM, SIGCHLD);
    sem_cmd(sem_id, WEATHER_SEM, WAIT, 0);
    /* storm_duration; 1s : 24h = 0,042s : 1h */
    time = 42000000 / 1e9 * swell_duration;
    working(time);

    i = 0;
    while (i < init_ports && alreadyHit == 0) { /* search in array if port is already hit */
        if (*(ports_hit + i) == mypid) {
            alreadyHit = 1;
        } else {
            i++;
        }
    }

    if (alreadyHit == 0) { /* port not hit - insert in array*/
        for (j = 0; j < init_ports; j++) {
            if (*(ports_hit + j) == 0) { /* first free position*/
                *(ports_hit + j) = mypid;
                break;
            }
        }
    }

    sem_cmd(sem_id, WEATHER_SEM, SIGNAL, 0);
    unblock_signals(3, SIGTSTP, SIGTERM, SIGCHLD);
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

        if (strcmp(key, "SO_DOCKS") == 0) {
            init_docks = atoi(value);
        } else if (strcmp(key, "SO_FILL") == 0) {
            so_fill = atoi(value);
        } else if (strcmp(key, "SO_SWELL_DURATION") == 0) {
            swell_duration = atoi(value);
        }
    }
    free(key);
    free(value);
    if (fclose(stream) == EOF) {
        perror("fclose error");
        exit(EXIT_FAILURE);
    }
}

void sigint_handler(int signum) {
    fprintf(stderr, "I'm the port %d, I received SIGINT", mypid);
    shmdt(array_goods);
    shmdt(array_port_supply);
    shmdt(array_port_demand);
    shmdt(info_ports);
    shmdt(ports_number);
    shmdt(supply_daily_fill);
    shmdt(demand_daily_fill);
    freeIndexList(offers_list);
    freeIndexList(demands_list);
    rm_sem(docks_id);
    rm_msg(msgqid);
    exit(EXIT_FAILURE);
}

void sigsegv_handler(int signum) {
    fprintf(stderr, "I'm the port %d, I received SEGFAULT\n", getpid());
    shmdt(array_goods);
    shmdt(array_port_supply);
    shmdt(array_port_demand);
    shmdt(info_ports);
    shmdt(ports_number);
    shmdt(supply_daily_fill);
    shmdt(demand_daily_fill);
    freeIndexList(offers_list);
    freeIndexList(demands_list);
    rm_sem(docks_id);
    rm_msg(msgqid);
    sem_cmd(sem_id, START_MAIN, SIGNAL, 0);
    exit(EXIT_FAILURE);
}
