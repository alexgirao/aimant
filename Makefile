
C_PROGS = aimant chargenx

all: $(C_PROGS)

%.o: %.c
	gcc -g -Wall -c -o $@ $<

$(C_PROGS):
	gcc -Wall -o $@ $^ -lrt

clean:
	file * | grep ' ELF.* \(executable\|relocatable\),' | cut -d: -f1 | xargs rm -fv

# depends

str.o: str.h
subprocess.o: dict.h
aimant.o: item.h

aimant: aimant.o subprocess.o getopt_x.o bsd-getopt_long.o debug0.o str.o
chargenx: chargenx.o getopt_x.o bsd-getopt_long.o debug0.o
