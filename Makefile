CFLAGS = -std=c89 -Wall -Wpedantic -D_GNU_SOURCE

DEBUG = -g -O0

MAIN = main.o common.o

PORTS = ports.o common.o

VESSELS = vessels.o common.o

WEATHER = weather.o common.o

OPTIONS =

all: main ports vessels weather

main: $(MAIN)
	$(CC) $(MAIN) $(CFLAGS) -o Maritime_transit_of_goods_project
	
ports: $(PORTS)
	$(CC) $(PORTS) $(CFLAGS) -o ports

vessels: $(VESSELS)
	$(CC) $(VESSELS) -lm $(CFLAGS) -o vessels

weather: $(WEATHER)
	$(CC) $(WEATHER) -lm $(CFLAGS) -o weather
	
	
common.o: common.o common.h
	gcc -c $(CFLAGS) common.c
	
main_debug: main.c common.o
	$(CC) $(DEBUG) main.c common.o $(CFLAGS) -o main_debug
	
ports_debug: ports.c common.o
	$(CC) $(DEBUG) ports.c common.o $(CFLAGS) -o ports
	
vessels_debug: vessels.c common.o
	$(CC) $(DEBUG) vessels.c common.o -lm $(CFLAGS) -o vessels

clean:
	rm -f *.o main main_debug ports vessels weather Maritime_transit_of_goods_project logbook.txt *~
	
run:
	./Maritime_transit_of_goods_project
