#ifndef __COMMON_H
#define __COMMON_H

/*_____PATH_MACROS______*/

#define PORT_PATH "./ports"
#define VESSEL_PATH "./vessels"
#define WEATHER_PATH "./weather"
#define SETTINGS_PATH "./settings.txt"

/*______SEM_MACROS______*/

#define SEM_KEY 91543
/* keys for docks are the pids of the ports in order to have a specific sem for port */

/* to use with sem_cmd() to specify semop */
#define WAIT - 1
#define SIGNAL 1
#define WAIT_VESSELS -init_vessels
#define WAIT_PORTS -init_ports

/* to use with sem_cmd() to specify sem_num */
#define PORTS_SUPPLY_SEM 0
#define PORTS_DEMAND_SEM 1
#define VESSELS_SEM 2
#define START_MAIN 3
#define VESSELS_INFO_SEM 4
#define START_PORTS 5
#define START_VESSELS 6
#define HANDLER_V_SEM 7
#define WEATHER_SEM 8
#define START_WEATHER 9
#define DOCKS 0

/*______SHM_MACROS______ */

#define SHM_TIME_KEY 054271
#define SHM_GOODS_KEY 012412
#define SHM_SUPPLY_KEY 023717
#define SHM_DEMAND_KEY 012543
#define SHM_PORTSINFO_KEY 024371
#define SHM_VESSELSINFO_KEY 037744
#define SHM_GOODSINFO_KEY 034762
#define SHM_WEATHERINFO_KEY 035720
#define SHM_VESSELSPOSITIONINFO_KEY 034210
#define ARRAYVESSELPID_KEY 065143
#define PORTS_COUNTER_KEY 065214
#define VESSELS_COUNTER_KEY 025341
#define GOODS_KEY 017364
#define SIMTIME_KEY 032357
#define MAINPID_KEY 065321
#define DAILY_SUPPLY_KEY 012743
#define DAILY_DEMAND_KEY 065423

/* key for demand queue of each port is the pid of the port */

/*------------------------------------------------------------------------------------------------------------------------------------------*/

/*_______structs_______*/

/* supply is implemented as msg queues */

typedef struct {
    long lot_id;
    int good_type;
    int quantity;
    int expiration_date;
} supply;

/* SHARED MEMORY INFOS */

typedef struct {
    int good_type;
    int weight;
    int lifetime;
} good_t;

typedef struct {
    pid_t port_pid;
    double x, y;
    int good_type;
    int quantity;
    int unit_weight;
    int expiration_date;
} port_supply;

typedef struct {
    pid_t port_pid;
    double x, y;
    int good_type;
    int quantity;
} port_demand;

typedef struct {
    int traveling_loaded;
    int traveling_unloaded;
    int in_port;
} vessels_info;

typedef struct {
    pid_t vessel_pid;
    int in_port; /* boolean: 0=false, 1=true */
} vessel_position_info;

typedef struct {
    pid_t pid;
    int goods_in_port;
    int goods_sent;
    int goods_received;
    int n_docks;
    int busy_docks;
    int total_produced;
    int total_requested;
} ports_info;

typedef struct {
    int good_type;
    int in_ports;
    int on_vessels;
    int expired_on_vessels;
    int expired_in_ports;
    int delivered_to_ports;
} goods_info;

typedef struct {
    int weather_pid;
    int vessels_hit_num;
    int vessels_sunk_num;
    int ports_hit;
} weather_info;

/* global variables containing sizes or quantity of stuff init by user useful to every process */
extern int so_fill, day, init_docks, vessels_speed, vessels_capacity, load_speed, init_ports, init_vessels,
        storm_duration, swell_duration;
extern size_t shm_good_size, shm_supply_size, shm_demand_size, shm_portsinfo_size, shm_vesselsinfo_size,
        shm_goodsinfo_size, shm_weatherinfo_size, shm_vessels_position_info_size;

/*_________functions_prototypes:_________*/

/* semaphores */

/* get semaphore set */
int get_sem();

/* get the semaphore to access docks of the port specified in pid_port */
int get_docks(pid_t port_pid);

/* remove semaphore set */
void rm_sem(int sem_id);

/* performs the operation specified by sem_op on the sem_numth semaphore of the sem_id semaphore's set */
int sem_cmd(int sem_id, unsigned short sem_num, short sem_op, short sem_flg);


/* shared memory */

/* get + attach shm segment for goods */
int getGoodsSeg(good_t **goods_array);

/* get + attach shm segment for ports_supply */
int getSupplySeg(port_supply **supply_array);

/* get + attach shm segment for ports_demand */
int getDemandSeg(port_demand **demand_array);

/* get + attach shm segment for time variable sharing */
int getTime(double **time_var);

/* get + attach shm segment for ports info */
int getPortsInfoSeg(ports_info **info_ports);

/* get + attach shm segment for vessels info */
int getVesselsInfoSeg(vessels_info **info_vessels);

/* get + attach shm segment for goods info */
int getGoodsInfoSeg(goods_info **info_goods);

/* get + attach shm segment for weather info */
int getWeatherInfoSeg(weather_info **info_weather);

/* get + attach shm segment for vessel position info */
int getVesselPositionInfoSeg(vessel_position_info **info_position_vessels);

/* get + attach shm for array vessels pid */
int getArrayVesselsPidSeg(pid_t **array_vessels_pid);

/* get + attach shm for the port counter */
int getPortsNumber(int **port_counter);

int getGoodsNumber(int **goods_number);

int getSimTime(int **sim_time);

int getSupplyDailyFill(long **daily_supply);

int getDemandDailyFill(long **daily_demand);

/* messaged queues */

/* get the msg queue of the port specified by port_pid */
int get_msg(pid_t port_pid);

/* send a message(supply) to the queue specified by msgqid and check the errors */
int snd_msg(int msgqid, supply *to_send);

/* receive a message(supply) from the queue */
void rcv_msg(int msqid, supply *to_receive, long lot_id);   /* da fare */

/* remove a message queue */
void rm_msg(int msqid);


/* nanosleep */

/* simulate travelling and loading/unloading: calculate sec eand nsec and call nanosleep */

void working(double time);


/* signals */

/* function from "progetto esempio" used to block signals */
void block_signals(int count, ...);

/* function from "progetto esempio" used to unblock signals */
void unblock_signals(int count, ...);

/* function from "progetto esempio " used to set new handler 
returns the old sigaction struct */
struct sigaction set_handler(int sig, void (*func)(int));

#endif

