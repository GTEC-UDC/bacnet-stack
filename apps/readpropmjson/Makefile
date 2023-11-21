#Makefile to build BACnet Application using GCC compiler

# Executable file name
TARGET = readpropmjson
# BACnet objects that are used with this app
BACNET_OBJECT_DIR = $(BACNET_SRC_DIR)/bacnet/basic/object

CJSON_DIR = $(BACNET_DIR)/lib/cJSON

SRC = main.c \
	$(BACNET_OBJECT_DIR)/client/device-client.c \
	$(BACNET_OBJECT_DIR)/netport.c \
	$(CJSON_DIR)/cJSON.c

# TARGET_EXT is defined in apps/Makefile as .exe or nothing
TARGET_BIN = ${TARGET}$(TARGET_EXT)

# Statically-linked target
TARGET_STATIC_BIN = ${TARGET}-static$(TARGET_EXT)

OBJS += ${SRC:.c=.o}

ifeq ($(STATIC),1)
TARGETS = ${TARGET_BIN} ${TARGET_STATIC_BIN}
else
TARGETS = ${TARGET_BIN}
endif

all: ${BACNET_LIB_TARGET} Makefile ${TARGETS}

# Include cJSON path
CFLAGS += -I$(CJSON_DIR)

${TARGET_BIN}: ${OBJS} Makefile ${BACNET_LIB_TARGET}
	${CC} ${PFLAGS} ${OBJS} ${LFLAGS} -o $@
	size $@
	cp $@ ../../bin

${TARGET_STATIC_BIN}: ${OBJS} Makefile ${BACNET_LIB_TARGET}
	${CC} -static ${PFLAGS} ${OBJS} ${LFLAGS} -o $@
	size $@
	cp $@ ../../bin

${BACNET_LIB_TARGET}:
	( cd ${BACNET_LIB_DIR} ; $(MAKE) clean ; $(MAKE) -s )

.c.o:
	${CC} -c ${CFLAGS} ${INCLUDES} $*.c -o $@

.PHONY: depend
depend:
	rm -f .depend
	${CC} -MM ${CFLAGS} *.c >> .depend

.PHONY: clean
clean:
	rm -f core ${TARGET_BIN} ${TARGET_STATIC_BIN} ${OBJS} $(TARGET).map ${BACNET_LIB_TARGET}

.PHONY: include
include: .depend

