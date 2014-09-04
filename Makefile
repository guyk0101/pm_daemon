IDIR=.
CC=gcc
CFLAGS=-I$(IDIR) -g
LIBS=-lrt

OS=$(shell uname)
ifeq ($(OS), SunOS)
LIBS+=-lsocket
endif

ODIR=obj

_OBJ = pm_daemon.o file_utility.o ftpclient.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

pm_daemon: $(OBJ)
	$(CC) -o $@ $^ $(LIBS)

-include $(OBJ:.o=.d)

$(ODIR)/%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)
	@$(CC) -MM $(CFLAGS) $< > $*.d
	@sed 's/$(patsubst %.c,%.o,$<)/$(ODIR)\/$(patsubst %.c,%.o,$<)/g' $*.d > $(patsubst %.o,%.d,$@)
	@rm -f $*.d

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~
	rm -f $(ODIR)/*.d
	rm -f pm_daemon
