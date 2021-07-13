TOP=..
include $(TOP)/configure/CONFIG
#=======================================

# Build asyn failover driver as a library for an IOC:
LIBRARY_IOC += asynFailover

# Library Source files
asynFailover_SRCS += asynFailover.c

# Link with the asyn and base libraries
asynFailover_LIBS += asyn
asynFailover_LIBS += calc
asynFailover_LIBS += $(EPICS_BASE_IOC_LIBS)

# Install .dbd
DBD += asynFailover.dbd

#=======================================
include $(TOP)/configure/RULES
