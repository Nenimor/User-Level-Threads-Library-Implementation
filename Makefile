LIBSRC= uthreads.cpp Thread.h

TAR= tar
TARFLAGS= -cvf
TARNAME= ex2.tar
TARSRCS= $(LIBSRC) Makefile README



all:
	g++ -std=c++11 -g -I. -c -o uthreads.o uthreads.cpp
	ar rv libuthreads.a uthreads.o Thread.h
	ranlib libuthreads.a
	

main:
	g++ -c -o main.o main.cpp
	g++ -Wall -std=c++11 -g -I. main.o uthreads.o -L -luthreads -o main


valgrind:
	valgrind --leak-check=full --show-possibly-lost=yes --show-reachable=yes --undef-value-errors=yes --track-origins=yes --dsymutil=yes -v ./main
	


tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)

clean:
	-rm -f *.o *.a *.tar main
