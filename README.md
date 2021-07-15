# Asyn Port Redundancy Driver (asyn-failover)

Asyn driver that enables port redundancy, automatically switching between multiple ports defined by the user based on connection states.

This driver was heavily based on work by [Dirk Zimoch](https://github.com/paulscherrerinstitute/StreamDevice/commits?author=dirk-zimoch) and all files that contain external contributions were marked as such.

## Installation

### Requirements

* EPICS base (R3.15 or newer is recommended)
* asyn (R4-29 or newer is recommended)

### Optional dependencies

The following dependencies are optional, but highly recommended as they bring major functionality improvements:

* StreamDevice

These dependencies complement IOC functionality (albeit in a smaller way), but they can also be safely removed:

* Calc
* Autosave

If you need to add/remove dependencies to build the `.dbd` files, you will need to edit `configure/RELEASE` and `asynFailoverApp/src/Makefile`.

### Installation

Make sure the contents of `configure/RELEASE` point to the desired modules, then build the files with `make`:

```bash
make
``` 

### Utilization

_st.cmd_
```
#!/opt/epics-R3.15.8/modules/asyn-redundant/bin/linux-x86_64/asynFailoverApp

epicsEnvSet("STREAMDEVICE", "/opt/epics-R3.15.8/modules/StreamDevice-2.8.18")
epicsEnvSet("IOC", "/epicsIoc")
epicsEnvSet("STREAM_PROTOCOL_PATH", "$(IOC)/protocol")
epicsEnvSet("AVAILABLE", "0")

cd $(IOC)

dbLoadDatabase "/opt/epics-R3.15.8/modules/asyn-redundant/dbd/asynFailoverApp.dbd"
asynFailoverApp_registerRecordDeviceDriver pdbbase

# Create and configure two regular ports
drvAsynIPPortConfigure("L0", "10.0.6.49:6379")
drvAsynIPPortConfigure("L1", "10.0.6.66:6379")
asynSetOption("L0", -1, "disconnectOnReadTimeout", "Y")
asynSetOption("L1", -1, "disconnectOnReadTimeout", "Y")

# Failover syntax: <dynamic/output port> <port 1> <port 2> <...> <port n>
asynFailoverConfig("F0","L0", "L1") 

dbLoadRecords("database/float.db", "PORT=F0, RECORD_NAME=RedundancyAive:Test")

iocInit
```

## Config

### Options

Options are configured through `asynSetOption` (syntax: `asynSetOption(port, address, key, value)`)

#### Policy (key: 'policy')
* Default: `cycle`

Governs the behavior of the driver when a new port is connected/disconnected.

##### Values
* `stack` gives priority to ports created first (in the example above, `L0`) and will swap over to ports with higher priority as soon as they're connected
* `cycle` does not have any sort of priority; once a port connects, it remains connected until it is disconnected, only then a different port is assigned its place

## Notes & FAQ

_Q: What triggers a port switch?_

The driver responds to connect/disconnect events. That means that events such as timeouts won't be registered, therefore, it's necessary to make any event you wish to trigger a port change an explicit disconnect. One such example is featured above, where ports will disconnect on timeouts. If autoConnect is set to true for the given port, the driver will automatically update the connection status of the port, even if it disconnected beforehand.

_Q: Can I reuse the ports I've used to create the failover port in a different record?_

Preferrably, no. If you're not careful about the connect/disconnect timings of the port, it may severely hamper the failover functionality by triggering the events used to listen for port connections, effectively disabling it altogether.

_Q: What kinds of ports are supported?_

All asyn-compatible ports are supported. However, the program only implements IP port support by default. In order to change that, add or remove dependencies in `configure/RELEASE` and `asynFailoverApp/src/Makefile`.

## Performance

Switching between ports is instantaneous when the failover port is already connected. However, when all ports are disconnected, switching times may unfortunately take longer, due to communication protocol initialization procedures and how EPICS deals with port connections/disconnections. Furthermore, if you're using timeouts as triggers for switching ports, switches started by timeout events will only occur once SCAN fires and the reply times out.
