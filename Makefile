# Makefile para cliente FTP 

CLTOBJ= SarangoJ-clienteFTP.o connectsock.o connectTCP.o passivesock.o passiveTCP.o errexit.o

all: SarangoJ-clienteFTP 

SarangoJ-clienteFTP:	${CLTOBJ}
	cc -o SarangoJ-clienteFTP ${CLTOBJ}

clean:
	rm $(CLTOBJ) 
