TARGETS = smtp pop3 echoserver

all: $(TARGETS)

echoserver: echoserver.cc
	g++ $^ -lpthread -g -o $@

smtp: smtp.cc email.cc
	g++ $^ -lpthread -g -o $@

pop3: pop3.cc
	g++ $^ -I/usr/local/opt/openssl/include -L/usr/local/opt/openssl/lib -lcrypto -lpthread -g -o $@

pack:
	rm -f submit-hw2.zip
	zip -r submit-hw2.zip *.cc *.h README Makefile

clean::
	rm -fv $(TARGETS) *~

realclean:: clean
	rm -fv submit-hw2.zip
