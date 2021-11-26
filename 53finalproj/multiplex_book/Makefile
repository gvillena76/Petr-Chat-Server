CC = gcc

CSAPP_INC = include
CSAPP_SRC = src

CFLAGS = -Og -Wall -I $(CSAPP_INC) 
LDLIBS = -lpthread -lm


all: select echoservers echoclient

csapp.o: setup
	$(CC) $(CFLAGS) -c -o build/$@ $(CSAPP_SRC)/csapp.c $(LDLIBS) 

echo.o: setup
	$(CC) $(CFLAGS) -c -o build/$@ $(CSAPP_SRC)/echo.c $(LDLIBS) 

setup:
	mkdir -p bin/ build/

echoclient: setup csapp.o
	$(CC) $(CFLAGS)  -o bin/$@ $(CSAPP_SRC)/echoclient.c build/* $(LDLIBS)

echoservers: setup echo.o csapp.o
	$(CC) $(CFLAGS)  -o bin/$@ $(CSAPP_SRC)/echoservers.c build/* $(LDLIBS)
	  
select: setup echo.o csapp.o
	$(CC) $(CFLAGS)  -o bin/$@ $(CSAPP_SRC)/select.c build/* $(LDLIBS)

clean:
	rm -rf bin build