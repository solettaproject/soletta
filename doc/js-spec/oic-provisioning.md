OIC Provisioning
================

When a device (OIC stack instance) is started, it should obtain a unique device id, preferably a globally unique id, but anyway unique in the operating network of the device.

The [OIC](http://www.openinterconnect.org/) [specifications](http://openinterconnect.org/developer-resources/specs/), notably the OIC Security specification describe processes for this.

Use case: we have a given HW board and multiple sensors connected to it. This is an OIC Platform.
The OIC stack runs a main loop with Node.js and all the protocols in one execution context.

Let's assume we run an instance of the OIC stack for an application (e.g. "fan.js") running on a device.

The UA (User Agent) has to ensure unique device id’s at least *within the platform*, before the onboarding process. If the device supportes OIC Security mechanisms, then during onboarding, the OIC Owner Transfer Method (OTM) is responsible to set a globally unique device id. The devices need to store that id and use it from there on.

Since in the example ```fan.js``` cannot write a local file, it needs to use an API for a persistent storage hosted on the platform and accessed by the execution context running the device and app.

So the propose is to implement a persistent storage to be a simple key-dictionary structure, where the key is the filename, e.g. "fan.js", and the value is private data to the UA, and specific to the app (the file name):

```javascript
// Storage-internal representation of settings.
dictionary OicDeviceStoredSettings {
    OicDevice deviceInit;
    sequence<OicResource> resources;
    boolean provisioned;  // if false, needs to generate device UUID
    boolean supportsOTM;  // if true, needs to run onboarding
    boolean boarded;      // if true, the device has a globally unique id
                          // if false, it has a locally unique id (best effort)
};
```
The client app can provide the following configuration:
```javascript
dictionary OicDeviceSettings {  // part of a future version of the OIC API
    OicDevice device;  // most important property: device.uuid
    sequence<OicResourceInit> resources;
};
```
The UA may prepend the specified device.uuid with the file name in order to use it later as a seed to the UUID generation.

For example, in order to use a new fan connected to the platform, the following client code is needed.
```javascript
// bootstrapping configuration of the device
var fanDeviceInit = {
    uuid: "fan0",  // temporary, or default value
    name: "Living room ventillation fan",
    model: "AXP0001",
    coreSpecVersion: "core.0.99",
    osVersion: "Soletta x.2",
    manufacturerName: "Sol"
};

// bootstrapping configuration of resources
var fanResourceInit =  {
    id: { deviceId: "fan0", path: "/fan/livingroom" },
    resourceTypes: [ "oic.r.fan" ],
    interfaces: [ "oic.if.b" ],
    mediaTypes: [ "application/json", "text/plain" ],
    discoverable: true,
    observable: true,
    secure: false,
    slow: false,
    properties: {
        on: true,
        speed: "10" // rpm
    };
};

// bootstrapping configuration
var config = {
    deviceInit: fanDeviceInit;
    resources: [ fanResourceInit ];
};

var fan = require("oic")("server", config);

fan.onupdaterequest = function(event) {
    // ...
};

fan.register(fanResourceInit).then(fanResource) {
    console.log("Fan registered: " fanResource.id.deviceId +
fanResource.id.path);
    // ...
});
```
This was the programmatic way to bootstrapping. This process could be replaced by an automated process of self-discovery, and remote configuration by OTM or other process, but for now let's assume the programmatic model. The following algorithmic steps should be done by UA implementations.

### Bootstrapping steps

Assumption: the default image which is booted on the device contains code for some provisioning support, either device-local, or supporting a form of remote configuration, in this case OIC OTM.

1. Start up the platform (HW).
  1. The UA opens (or creates) a permanent storage (either the security storage or our replacement if not built with security module).
  2. The UA opens (or creates an empty) configuration entry with the key as the JS file name, e.g. "fan.js".
  3. If there is data associated with the key ("fan.js"), then it is a ```OicDeviceStoredSettings``` structure, private to the UA.
  4. Start up each device, and run the following steps for each.

2. If the device is not provisioned, and supports remote provisioning (OTM),
  1. Find a configuration server (OIC Onboarding Tool), establish a secure connection.
  2. Request/check a device UUID as ```deviceID```.
  3. Provision the ```/oic/sec/doxm``` resource with ```deviceID``` using the OTM process.
  4. Save the obtained deviceID in the persistent storage.
  5. Continue with provisioning, e.g. ```/oic/sec/cred```, ACLs, etc.
   ...
  6. Save the provisioned info and set the ```provisioned``` and ```boarded``` flags in the permanent storage.
  7. If any of the steps above fail, then fail starting the device (i.e. if security is enabled and doesn't work then fail).
  8. continue with step 4.

3. If not provisioned, and does not support tool provisioning, then use the ```OicDeviceSettings``` bootstrap configuration provided by the client through the API.
  1. Read the device.uuid, prepend it with the file name, and and eventually other info, e.g. the resource paths of the device) in order to obtain a seed.
  2. Generate a UUID using that seed and save it as deviceID in the permanent storage.
  3. Save all other provisioned info in the permanent storage and set the ```provisioned``` flag in the permanent storage.

4. Read the provisioned info: device info, resources etc.

5. Create ```/oic/res```, ```/oic/d```, ```/oic/p``` resources based on provisioned information.

...
(Client code is executed).

#### Note
When the resource representation (own properties) of a resource is updated, the new values could be saved in the permanent storage by the UA, so that when next time the device is powered up, the resource is initialized with the last set values.
