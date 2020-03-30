FLAGS=-Wall -Wextra -O3 -flto

.PHONY: all
all: gbctc

default: all


gbctc: main.o
	${CC} $< -o $@ -lpng ${FLAGS}

main.o : main.c
	${CC} -c -o $@ $< ${FLAGS}

clean:
	rm gbctc
	rm -f *.o
