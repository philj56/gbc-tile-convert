FLAGS=-Wall -Wextra -O3 -flto -march=native

.PHONY: all
all: gbctc

default: all


gbctc: main.o
	${CC} $< -o $@ -lpng ${FLAGS}

main.o : main.c
	${CC} -c -o $@ $< ${FLAGS}

.PHONY: install
install: gbctc
	install -D gbctc -t ${DESTDIR}/usr/bin/

clean:
	rm gbctc
	rm -f *.o
