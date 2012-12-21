CC = gcc

LIBS = -lm

FLAGS =  -g -O2
CFLAGS = ${FLAGS}

all: odr_main time_client time_server

time_client: time_client.o api_layer.o
	${CC} ${FLAGS} -o client_skasanavesi time_client.o api_layer.o ${LIBS}

time_server: time_server.o api_layer.o
	${CC} ${FLAGS} -o server_skasanavesi time_server.o api_layer.o ${LIBS}

odr_main: odr_main.o odr_helpers.o get_hw_addrs.o
	${CC} ${FLAGS} -o odr_skasanavesi odr_main.o odr_helpers.o get_hw_addrs.o ${LIBS}

odr_main.o: odr_main.c
	${CC} ${CFLAGS} -c odr_main.c

odr_helpers.o: odr_helpers.c
	${CC} ${CFLAGS} -c odr_helpers.c

get_hw_addrs.o: get_hw_addrs.c
	${CC} ${FLAGS} -c get_hw_addrs.c
	
time_client.o: time_client.c
	${CC} ${CFLAGS} -c time_client.c

time_server.o: time_server.c
	${CC} ${CFLAGS} -c time_server.c

api_layer.o: api_layer.c
	${CC} ${CFLAGS} -c api_layer.c


clean:
	rm odr_main.o odr_helpers.o get_hw_addrs.o time_client.o time_server.o api_layer.o odr_skasanavesi client_skasanavesi server_skasanavesi
