Platform Web API for Soletta
============================

Introduction
------------
This document presents a JavaScript API based on the [Soletta Platform API](http://solettaproject.github.io/docs/c-api/group__Platform.html).

Web IDL
-------
```javascript
// var platform = require('soletta/platform');

interface Platform {
  readonly attribute DOMString boardName;
  readonly attribute DOMString hostName;
  readonly attribute DOMString locale;
  readonly attribute DOMString machineId;  // UUID
  readonly attribute DOMString serialNumber;
  readonly attribute sequence<DOMString> mountPoints;
  readonly attribute DOMString osVersion;
  readonly attribute DOMString swVersion;

  readonly attribute PlatformState platformState;

  boolean setTarget(PlatformTarget target);

  DateTime getSystemClock();

  long watch(MonitoringTarget target, MonitorCallback callback);
  boolean unwatch(long id);

  boolean startService(DOMString serviceName);
  boolean stopService(DOMString serviceName);
  boolean restartService(DOMString serviceName);
  ServiceState getServiceState(DOMString serviceName);

  boolean unmount(DOMString mountPoint);
  boolean setTimeZone(DOMString timezone);

  boolean setLocale(LocaleCategory category, DOMString locale);
  boolean applyLocale(LocaleCategory category);
};

enum ServiceState {
  "active", "reloading", "inactive", "failed", "activating", "deactivating", "unknown"
};

enum PlatformState {
  "initializing", "running", "degraded", "maintenance", "stopping", "unknown"
};

enum PlatformTarget {
  "poweroff", "reboot", "suspend", "emergency", "rescue", "default"
};

enum MonitoringTarget {
  "hostname", "locale", "service", "platform-state", "clock", "timezone"
};

enum LocaleCategory {
  "language", "address", "collate", "ctype", "identification", "measurement",
  "messages", "monetary", "name", "numeric", "paper", "telephone", "time", "unknown"
};

callback HostNameCallback = void (DOMString hostname);
callback LocaleCallback = void (LocaleCategory category, DOMString locale);
callback ServiceStateCallback = void (DOMString service, ServiceState state);
callback PlatformStateCallback = void (PlatformState state);
callback SystemClockCallback = void (DateTime adjustedTime);
callback TimeZoneCallback = void (DOMString timezone);

typedef (HostNameCallback or LocaleCallback or ServiceStateCallback,
  PlatformStateCallback, SystemClockCallback, TimeZoneCallback) MonitorCallback;

```

The ```callback``` parameter of the ```watch()``` method MUST correspond to the ```target``` parameter.

| target           | callback type         |
| ---------------- | :-------------------- |
| 'hostname'       | HostNameCallback      |
| 'locale'         | LocaleCallback        |
| 'service'        | ServiceStateCallback  |
| 'platform-state' | PlatformStateCallback |
| 'clock'          | SystemClockCallback   |
| 'timezone'       | TimeZoneCallback      |


#### Examples
```javascript
var platform = require("soletta/platform");

var myService = 'neard';
if (platform.getServiceState(myService) != 'active') {
  if (platform.startService(myService))]
    console.log("Service " + myService + "started.");
}

console.log("Board name: " + platform.boardName);
console.log("Platform id: " + platform.machineId);
console.log("OS version: " + platform.osVersion);
console.log("Host name: " + platform.hostName);

var watch = platform.watch("hostname", function(hostname) {
  console.log("Host name changed to: " + hostname);
});

```
