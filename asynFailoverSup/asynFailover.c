/*asynFailover.c */
/***********************************************************************/

/* Connect to multiple lower level asyn ports and switch dynamically
 * when one disconnects.
 * 
 * Author: Dirk Zimoch
 */

#include <string.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsMutex.h>
#include <epicsAssert.h>
#include <iocsh.h>

#include "asynDriver.h"
#include "asynOctet.h"
#include "asynOption.h"
#include <epicsExport.h>

enum policy { policyCycle, policyStack};

typedef struct portPvt {
    asynUser*       pasynUser;
    asynCommon*     drvCommon;
    void*           pvtCommon;
    asynOption*     drvOption;
    void*           pvtOption;
    asynOctet*      drvOctet;
    void*           pvtOctet;
    struct failoverPvt* pvt;
    int             index;
    int             connected;
} portPvt;

typedef struct failoverPvt {
    epicsMutexId    mutex;
    asynUser*       pasynUser;
    asynInterface   ifCommon;
    asynInterface   ifOption;
    asynInterface   ifOctet;
    enum policy     policy;
    int             nPorts;
    portPvt*        current;
    portPvt         ports[1]; /* list */
} failoverPvt;

/* asynOctet methods */
/* communicate to the current low level port */

static void copyAsynUser(const asynUser* from, asynUser* to)
{
    /* leave userXXX alone */
    to->errorMessage       = from->errorMessage;
    to->errorMessageSize   = from->errorMessageSize;
    to->timeout            = from->timeout;
    to->reason             = from->reason;
    to->timestamp          = from->timestamp;
    to->auxStatus          = from->auxStatus;
    to->alarmStatus        = from->alarmStatus;
    to->alarmSeverity      = from->alarmSeverity;
}

static asynStatus writeIt(void *drvPvt, asynUser *pasynUser,
    const char *data, size_t numchars, size_t *nbytesTransfered)
{
    portPvt* current = ((failoverPvt *)drvPvt)->current;
    asynStatus status;

    copyAsynUser(pasynUser, current->pasynUser);
    status = current->drvOctet->write(current->pvtOctet, current->pasynUser,
        data, numchars, nbytesTransfered);
    copyAsynUser(current->pasynUser, pasynUser);
    printf("failover write [status %d, reason %d, switched=%d, nbytesTransfered=%zd]: %s\n",
        status, pasynUser->reason, current!=((failoverPvt *)drvPvt)->current, *nbytesTransfered, pasynUser->errorMessage);
    return status;
}

static asynStatus readIt(void *drvPvt, asynUser *pasynUser,
    char *data, size_t maxchars, size_t *nbytesTransfered, int *eomReason)
{
    portPvt* current = ((failoverPvt *)drvPvt)->current;
    asynStatus status;

    copyAsynUser(pasynUser, current->pasynUser);
    status = current->drvOctet->read(current->pvtOctet, current->pasynUser,
        data, maxchars, nbytesTransfered, eomReason);
    copyAsynUser(current->pasynUser, pasynUser);
    printf("failover read [status %d, reason %d, switched=%d, nbytesTransfered=%zd, eomReason=%d]: %s\n",
        status, pasynUser->reason, current!=((failoverPvt *)drvPvt)->current, *nbytesTransfered, *eomReason, pasynUser->errorMessage);
    return status;
}

static asynStatus flushIt(void *drvPvt, asynUser *pasynUser)
{
    portPvt* current = ((failoverPvt *)drvPvt)->current;
    asynStatus status;

    copyAsynUser(pasynUser, current->pasynUser);
    status = current->drvOctet->flush(current->pvtOctet, current->pasynUser);
    copyAsynUser(current->pasynUser, pasynUser);
    return status;
}

/* configure all low level port the same */

static asynStatus registerInterruptUser(void *drvPvt, asynUser *pasynUser,
    interruptCallbackOctet callback, void *userPvt, void **registrarPvt)
{
    failoverPvt *pvt = (failoverPvt *)drvPvt;
    asynStatus status = asynSuccess;
    int i;

    for (i = 0; i < pvt->nPorts; i++) {
        status = pvt->ports[i].drvOctet->registerInterruptUser(
            pvt->ports[i].pvtOctet,
            pasynUser, callback, userPvt, registrarPvt);
        if (status) break;
    }
    return status;
}

static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
    void *registrarPvt)
{
    failoverPvt *pvt = (failoverPvt *)drvPvt;
    asynStatus status = asynSuccess;
    int i;

    for (i = 0; i < pvt->nPorts; i++) {
        status = pvt->ports[i].drvOctet->cancelInterruptUser(
            pvt->ports[i].pvtOctet,
            pasynUser, registrarPvt);
        if (status) break;
    }
    return status;
}

static asynStatus setInputEos(void *drvPvt, asynUser *pasynUser,
    const char *eos, int eoslen)
{
    failoverPvt *pvt = (failoverPvt *)drvPvt;
    asynStatus status = asynSuccess;
    int i;

    for (i = 0; i < pvt->nPorts; i++) {
        status = pvt->ports[i].drvOctet->setInputEos(
            pvt->ports[i].pvtOctet,
            pasynUser, eos, eoslen);
        if (status) break;
    }
    return status;
}

static asynStatus getInputEos(void *drvPvt, asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    portPvt* current = ((failoverPvt *)drvPvt)->current;

    return current->drvOctet->getInputEos(current->pvtOctet,
            pasynUser, eos, eossize, eoslen);
}

static asynStatus setOutputEos(void *drvPvt, asynUser *pasynUser,
    const char *eos, int eoslen)
{
    failoverPvt *pvt = (failoverPvt *)drvPvt;
    asynStatus status = asynSuccess;
    int i;

    for (i = 0; i < pvt->nPorts; i++) {
        status = pvt->ports[i].drvOctet->setOutputEos(
            pvt->ports[i].pvtOctet,
            pasynUser, eos, eoslen);
        if (status) break;
    }
    return status;
}

static asynStatus getOutputEos(void *drvPvt, asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    portPvt* current = ((failoverPvt *)drvPvt)->current;

    return current->drvOctet->getOutputEos(current->pvtOctet,
            pasynUser, eos, eossize, eoslen);
}

static asynOctet funcsOctet = {
    writeIt, readIt, flushIt,
    registerInterruptUser, cancelInterruptUser,
    setInputEos, getInputEos, setOutputEos, getOutputEos
};

/* asynOption methods */

static asynStatus getOption(void *drvPvt, asynUser *pasynUser,
    const char *key, char *val, int valSize)
{
    failoverPvt *pvt = (failoverPvt *)drvPvt;
    portPvt* current = ((failoverPvt *)drvPvt)->current;

    if (epicsStrCaseCmp(key, "policy") == 0) {
        switch (pvt->policy) {
            case policyCycle:
                strncpy(val, "cycle", valSize);
                break;
            case policyStack:
                strncpy(val, "stack", valSize);
                break;
        }
        return asynSuccess;
    }

    return current->drvOption->getOption(current->pvtOctet,
            pasynUser, key, val, valSize);
}

static asynStatus setOption(void *drvPvt, asynUser *pasynUser,
    const char *key, const char *val)
{
    failoverPvt *pvt = (failoverPvt *)drvPvt;
    asynStatus status = asynError;
    int i;

    if (epicsStrCaseCmp(key, "policy") == 0) {
        if (strcmp(val, "cycle") == 0)
            pvt->policy = policyCycle;
        else
        if (strcmp(val, "stack") == 0)
            pvt->policy = policyStack;
        else {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "Bad policy choice \"%s\" should be \"cycle\" or \"stack\"", val);
            return asynError;
        }
        return asynSuccess;
    }

    for (i = 0; i < pvt->nPorts; i++) {
        if (!pvt->ports[i].drvOption) continue;
        status = pvt->ports[i].drvOption->setOption(
            pvt->ports[i].pvtOption,
            pasynUser, key, val);
        if (status) break;
    }
    return status;
}

static asynOption funcsOption = {
    setOption, getOption
};


/* asynCommon methods */

static void report(void *drvPvt, FILE *fp, int details)
{
    failoverPvt *pvt = (failoverPvt *)drvPvt;
    int i;

    for (i = 0; i < pvt->nPorts; i++) {
        if (!pvt->ports[i].drvCommon) continue;
        pvt->ports[i].drvCommon->report(
            pvt->ports[i].pvtCommon,
            fp, details);
    }
}

static asynStatus connect(void *drvPvt, asynUser *pasynUser)
{
    failoverPvt *pvt = (failoverPvt *)drvPvt;
    asynStatus status = asynSuccess;
    int i;
    int success = 0;

    for (i = 0; i < pvt->nPorts; i++) {
        if (!pvt->ports[i].drvCommon) continue;
        if (!pvt->ports[i].connected) {
            status = pvt->ports[i].drvCommon->connect(
                pvt->ports[i].pvtCommon,
                pasynUser);
            if (status == asynSuccess) success = 1;
        } else success = 1;
    }
    return success ? asynSuccess : status;
}

static asynStatus disconnect(void *drvPvt, asynUser *pasynUser)
{
    failoverPvt *pvt = (failoverPvt *)drvPvt;
    asynStatus status = asynSuccess;
    int i;
    int success = 1;

    printf("failover disconnect\n");
    for (i = 0; i < pvt->nPorts; i++) {
        if (!pvt->ports[i].drvCommon) continue;
        if (pvt->ports[i].connected) {
            status = pvt->ports[i].drvCommon->disconnect(pvt->ports[i].pvtCommon,pasynUser);
            printf("port %d disconnect -> %d\n", i, status);
            if (status != asynSuccess) success = 0;
            }
        }
    return success ? asynSuccess : status;
}

static asynCommon funcsCommon = {
    report, connect, disconnect
};

static void exceptionHandler(asynUser *pasynUser, asynException exception) {
    portPvt *port = (portPvt *)pasynUser->userPvt;
    failoverPvt *pvt = port->pvt;

    printf("exceptionHandler %d (asynExcCon %d)\n", exception, asynExceptionConnect);
    /* make sure to serialize events */
    if (exception == asynExceptionConnect) {
        epicsMutexLock(pvt->mutex);
        pasynManager->isConnected(pasynUser, &port->connected);
        if (!port->connected) {
            printf("port %d disconnected\n", port->index);
            if (pvt->current == port) {
                /* current port disconnected */
                int i = port->index;
                while (1) {
                    /* find next connected port */
                    if (++i >= pvt->nPorts) i = 0;
                    if (i == port->index) break;
                    if (pvt->ports[i].connected) {
                        pvt->current = &pvt->ports[i];
                        printf("now connected to port %d\n", pvt->current->index);
                        epicsMutexUnlock(pvt->mutex);
                        return;
                    }
                }
                /* no connected port found */
                printf("report disconnected\n");
                pasynManager->exceptionDisconnect(pvt->pasynUser);
            }
        } else {
            printf("port %d connected\n", port->index);
            if (pvt->current == port) {
                /* current port connected */
                printf("report connected\n");
                pasynManager->exceptionConnect(pvt->pasynUser);
            } else {
                /* other port connected */
                if ((pvt->policy == policyStack &&
                    pvt->current > port) || !pvt->current->connected) {
                    /* change back */
                    pvt->current = port;
                    printf("now connected to port %d\n", pvt->current->index);
                }
            }
        }
        epicsMutexUnlock(pvt->mutex);
    }
}

static void asynFailoverConfig(const char* portName, int nPorts, char** const ports)
{
    asynInterface* pasynInterface;
    failoverPvt *pvt;
    asynUser *pasynUser;
    asynStatus status;
    int i;

    if (nPorts < 2) {
        fprintf(stderr, "asynFailoverConfig: Need at least 2 ports to do failover\n");
        return;
    }

    pvt = callocMustSucceed(1,
        sizeof(failoverPvt) + (nPorts-1) * sizeof(portPvt),
        "asynFailoverConfig");

    pasynUser = pasynManager->createAsynUser(NULL, NULL);
    assert(pasynUser!= NULL);
    pasynUser->userPvt = pvt;
    pvt->pasynUser = pasynUser;
    pvt->mutex = epicsMutexMustCreate();
    pvt->policy = policyCycle;
    pvt->nPorts = nPorts;

    for (i = 0; i < nPorts; i++) {
        pasynUser = pasynManager->createAsynUser(NULL, NULL);
        assert(pasynUser!= NULL);
        pasynUser->userPvt = &pvt->ports[i];
        pvt->ports[i].pasynUser = pasynUser;
        pvt->ports[i].pvt = pvt;
        pvt->ports[i].index = i;
        status = pasynManager->connectDevice(pasynUser, ports[i], -1);
        if (status != asynSuccess) {
            fprintf(stderr, "asynFailoverConfig: port \"%s\" not found.\n", ports[i]);
            return;
        }
        pasynInterface = pasynManager->findInterface(pasynUser, asynOctetType, 1);
        if (!pasynInterface)
        {
            fprintf(stderr, "asynFailoverConfig: port \"%s\" is no asynOctet.\n", ports[i]);
            return;
        }
        pvt->ports[i].drvOctet = pasynInterface->pinterface;
        pvt->ports[i].pvtOctet = pasynInterface->drvPvt;

        pasynInterface = pasynManager->findInterface(pasynUser, asynOptionType, 1);
        if (pasynInterface)
        {
            pvt->ports[i].drvOption = pasynInterface->pinterface;
            pvt->ports[i].pvtOption = pasynInterface->drvPvt;
        }

        pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
        if (pasynInterface)
        {
            pvt->ports[i].drvCommon = pasynInterface->pinterface;
            pvt->ports[i].pvtCommon = pasynInterface->drvPvt;
        }

        status = pasynManager->exceptionCallbackAdd(pasynUser, exceptionHandler);
        if (status != asynSuccess) {
            fprintf(stderr, "asynFailoverConfig: cannot add exception handler to port \"%s\".\n", ports[i]);
            return;
        }
        pasynManager->isConnected(pasynUser, &pvt->ports[i].connected);
        if (!pvt->current && pvt->ports[i].connected)
            pvt->current = &pvt->ports[i];
    }

    status = pasynManager->registerPort(portName, ASYN_CANBLOCK, 1, 0, 0);
    if (status != asynSuccess) {
        fprintf(stderr, "asynFailoverConfig: Can't register myself.\n");
        return;
    }

    pvt->ifCommon.interfaceType = asynCommonType;
    pvt->ifCommon.pinterface = &funcsCommon;
    pvt->ifCommon.drvPvt = pvt;
    status = pasynManager->registerInterface(portName, &pvt->ifCommon);
    if (status != asynSuccess) {
        fprintf(stderr, "asynFailoverConfig: Can't register common interface.\n");
        return;
    }

    pvt->ifOption.interfaceType = asynOptionType;
    pvt->ifOption.pinterface = &funcsOption;
    pvt->ifOption.drvPvt = pvt;
    status = pasynManager->registerInterface(portName, &pvt->ifOption);
    if (status != asynSuccess) {
        fprintf(stderr, "asynFailoverConfig: Can't register option interface.\n");
        return;
    }

    pvt->ifOctet.interfaceType = asynOctetType;
    pvt->ifOctet.pinterface = &funcsOctet;
    pvt->ifOctet.drvPvt = pvt;
    status = pasynManager->registerInterface(portName, &pvt->ifOctet);
    if (status != asynSuccess) {
        fprintf(stderr, "asynFailoverConfig: Can't register octet interface.\n");
        return;
    }
    status = pasynManager->connectDevice(pvt->pasynUser, portName, -1);
    if (status != asynSuccess) {
        fprintf(stderr, "asynFailoverConfig: Can't connect to myself.\n");
        return;
    }
    if (pvt->current)
        pasynManager->exceptionConnect(pvt->pasynUser);
    else
        pvt->current = &pvt->ports[0];
}

/* register asynFailover*/
static const iocshArg asynFailoverConfigArg0 = { "master port name", iocshArgString };
static const iocshArg asynFailoverConfigArg1 = { "port1",iocshArgString };
static const iocshArg asynFailoverConfigArg2 = { "port2 ...",iocshArgArgv };
static const iocshArg *asynFailoverConfigArgs[] = {
    &asynFailoverConfigArg0, &asynFailoverConfigArg1, &asynFailoverConfigArg2 };
static const iocshFuncDef asynFailoverConfigFuncDef =
    { "asynFailoverConfig", 3, asynFailoverConfigArgs };

static void asynFailoverConfigCallFunc(const iocshArgBuf *args)
{
    asynFailoverConfig(args[0].sval, args[2].aval.ac, args[2].aval.av);
}

static void asynFailoverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&asynFailoverConfigFuncDef, asynFailoverConfigCallFunc);
    }
}
epicsExportRegistrar(asynFailoverRegister);
