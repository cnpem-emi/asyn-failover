TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# Build the IOC application

PROD_IOC = asynFailoverApp
# asynFailoverApp.dbd will be created and installed
DBD += asynFailoverApp.dbd

# asynFailoverApp.dbd will be made up from these files:
asynFailoverApp_DBD += base.dbd
asynFailoverApp_DBD += calcSupport.dbd
asynFailoverApp_DBD += asSupport.dbd
asynFailoverApp_DBD += asyn.dbd
asynFailoverApp_DBD += stream.dbd
asynFailoverApp_DBD += drvAsynIPPort.dbd
asynFailoverApp_DBD += asynFailover.dbd

# Include dbd files from all support applications:
asynFailoverApp_LIBS += calc
asynFailoverApp_LIBS += autosave
asynFailoverApp_LIBS += asyn
asynFailoverApp_LIBS += stream
asynFailoverApp_LIBS += asynFailover

# Add all the support libraries needed by this IOC
#asynFailoverApp_LIBS += xxx

# asynFailoverApp_registerRecordDeviceDriver.cpp derives from asynFailoverApp.dbd
asynFailoverApp_SRCS += asynFailoverApp_registerRecordDeviceDriver.cpp

# Build the main IOC entry point on workstation OSs.
asynFailoverApp_SRCS_DEFAULT += asynFailoverAppMain.cpp
asynFailoverApp_SRCS_vxWorks += -nil-

# Add support from base/src/vxWorks if needed
#asynFailoverApp_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

# Finally link to the EPICS Base libraries
asynFailoverApp_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
