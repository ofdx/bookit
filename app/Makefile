APP=ofdx_bookit

GPP=g++ -std=c++20 -I../dmitigr_fcgi/src/ -I../renényffenegger/

all: ${APP}

clean:
	rm -f ${APP} res.h

run: all stop
	sleep 1
	( ./${APP} "datapath ../../bookit_data/" ) &

stop:
	( killall -q ${APP} ) &

# BookIt reservation tool
${APP}: res.h main.cc ofdx_fcgi.h ncsa.h ../renényffenegger/rene.o
	${GPP} -o ${APP} main.cc ../renényffenegger/rene.o

# Base64 encoded resource files, which can be used by including res.h
res.h: resource/* builder.sh
	./builder.sh
