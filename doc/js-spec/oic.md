IoT Web API
===========

Abstract
--------
This document presents a JavaScript API for [OIC](http://www.openinterconnect.org/) [Core Specification](http://openinterconnect.org/resources/specifications/).

Introduction
------------
**IoT** (Internet of Things) is the name given for the complex environment in which diverse *resources*, implemented in *devices*, can be accessed remotely, and can notify subscribers with data and state changes. The resources act as servers, and communication with them may involve different protocols. Therefore, resources may also be represented by devices which translate between resource-specific and standard protocols. These devices are called *gateways*, or OIC intermediary devices.

Standardization is done by the [OIC](http://www.openinterconnect.org/) (Open Interconnect Consortium), which currently specifies:

  - the Core Framework for OIC core architecture, interfaces, protocols and services to enable OIC profiles implementation for IoT usages

  - Application Profiles Specification documents specify the OIC profiles to enable IoT usages for different market segments such as home, industrial, healthcare, and automotive.

Multiple providers/solutions can share a physical hardware *platform*.
A platform may host multiple physical or virtual *devices*.
Devices run the OIC software stack, and are addressable endpoints of communication. A device hosts multiple physical or virtual *resources*.
A resource represents *sensors* and *actuators*.
A given sensor or actuator may be represented by multiple properties. A read-only property in a resource belongs to a sensor input, whereas a read-write property belongs to the state of an actuator.

The devices support for resources can be extended via installable software modules called *applications*.

This API enables writing the applications that implement resources and business logic.

OIC high level Web API design notes
-----------------------------------
This API uses [Promises](http://www.ecma-international.org/ecma-262/6.0/#sec-promise-objects).

### Device
Code using this API is deployed to a device, which has one or more resources. In this version of the API it is assumed that the execution context of the code is separated for each device.

Therefore the **API entry point** is an object that exposes the local device functionality, and can be requested with a ```require``` statement which invokes the constructor. When the object is garbage collected, the implementations should clean up native state.

**Device identification** is UUID. Each device has an associated address + port.

### Device discovery
**Device discovery** uses endpoint discovery: multicast request "GET /oic/res" to "All CoAP nodes" (```224.0.1.187``` for IPv4 and ```FF0X::FD``` for IPv6, port 5683). The response lists devices and their resources (at least URI, resource type, interfaces, and media types). Since this is basically a resource discovery, it is merged with resource discovery.

### Presence
When a device is constructed, the implementation should announce its presence (together with the resources it contains).

Adding an event listener to the 'devicefound' event should turn on presence observing, i.e. the API implementation should make a request to watch presence notifications, and fire a 'devicefound' event when a presence notification is received.

### Resource
**Resource identification** is URL path, relative to a given device. A URL composed of the ```oic``` scheme, the device ID as host and the resource path can also be used for identifying a resource: ```oic://<deviceID>/<resourcePath>```. However, this specification uses the device ID and resource ID separately.

On each device there are special resources, implementing device discovery, resource discovery, platform discovery, etc. Platform needs to be discoverable on a resource with a fixed URI ```/oic/p```, device on ```/oic/d``` and resources on ```/oic/res```. This API encapsulates these special resources and the hardcoded/fixed URIs by explicit function names and parameters.

Resource representation properties shall not be created and deleted one by one. Only a full resource can be created and deleted.

### Resource discovery
**Resource discovery** is based on the existence of resources (directories) set up for discovery. It can be achieved in 3 ways:
- direct discovery through peer inquiry (unicast or multicast)
- indirect discovery based on a 3rd party directory (a server for resource discovery)
- presence advertisement: the resource enabling discovery is local to the initiator, and it is maintained by presence notifications.

Implementations should encapsulate the resource discovery type, and should map the DiscoveryOptions values to the best suited protocol request(s).

### Notifications
Implementations should support automatic notifications for changed resources that have observers. However, a "manual" notify API is also exposed on the OicServer interface, for experimental purpose.

When adding the first event listener to ```onchange``` on a resource, implementations should set the observe flag on all subsequent retrieve protocol requests.

When removing the last event listener from ```onchange``` on a resource, implementations should clear that flag on all subsequent retrieve protocol requests.

### Error handling
The error names map to [DOMError](https://heycam.github.io/webidl/#idl-DOMException-error-names). The following error names may be used when rejecting Promises: ```NotSupportedError```, ```SecurityError```, ```TimeoutError```, ```NotFoundError```, ```NoModificationAllowedError```, ```InvalidModificationError```, ```TypeMismatchError```, ```InvalidStateError```, ```InvalidAccessError```, ```InvalidNodeTypeError```, ```NotReadableError```, ```IndexSizeError```, ```DataCloneError```.

When a method in this API cannot be implemented for lack of support, reject the method with ```NotSupportedError```.

When a method in this API cannot be invoked for security reasons (e.g. lack of permission), reject the method with ```SecurityError```.

When an attempt is made to access (read, write, update) a resource representation property that does not exist, the method should be rejected with ```NotFoundError``` if the property is missing, an ```InvalidAccessError``` if the operation is not supported, or ```TypeMismatchError``` if the type or value is wrong.

Web IDL of the JavaScript API
-----------------------------
### OIC Stack
The API entry point is the local OIC stack executed on an OIC device. Multiple devices may be run on a given hardware platform, each having different network address, UUID and resources.

When the constructor is invoked without parameters, the device is started in the default (server) role.
```javascript
var oic = require('oic')();
```
If the OIC functionality is forced in client-only mode,  the ```OicServer``` API is not available on it (all server methods fail with ```NotSupportedError```).
```javascript
var oic = require('oic')('client');
```

The ```require()``` call returns an ```OIC``` object initialized with the local device and platform information.

The OIC object contains functionality to read device and platform information, discover remote devices and resources, add and remove local resources, create, retrieve, update and delete remote resources.
```javascript
enum OicRole { "client", "server" };

[Constructor(optional OicRole role = "server")]
interface OIC: EventTarget {
  readonly attribute OicDevice device;  // getter for local device info
  readonly attribute OicPlatform platform;  // getter for local platform info
};
```
All the following interfaces are included in OIC, but are described separately.
```javascript
OIC implements OicServer;
OIC implements OicClient;
OIC implements OicDiscovery;
OIC implements OicPresence;
```
The following information is exposed on the local ```/oic/d``` (device) resource.
```javascript
[NoInterfaceObject]
interface OicDevice {
  readonly attribute USVString uuid;
  readonly attribute USVString url;  // host:port
  readonly attribute DOMString name;
  readonly attribute sequence<DOMString> dataModels;
    // list of <vertical>.major.minor, e.g. vertical = “Smart Home”
  readonly attribute DOMString coreSpecVersion;   // core.<major>.<minor>
  readonly attribute OicRole role;
  // setters may be supported later
};
```
The following information is exposed on the local ```/oic/p``` (platform) resource.
```javascript
[NoInterfaceObject]
interface OicPlatform {
  readonly attribute DOMString id;
  readonly attribute DOMString osVersion;
  readonly attribute DOMString model;
  readonly attribute DOMString manufacturerName;
  readonly attribute USVString manufacturerUrl;
  readonly attribute Date manufactureDate;
  readonly attribute DOMString platformVersion;
  readonly attribute DOMString firmwareVersion;
  readonly attribute USVString supportUrl;
  // setters may be supported later
};
```

### OIC Resource
The resources are identified by a device UUID and a resource path relative to that device.
```javascript
dictionary OicResourceId {
  USVString deviceId;  // UUID
  USVString path;  // resource path (short form)
};
```
The properties of an OIC resource used in the application data model are represented as a JSON-serializable dictionary, which contains a snapshot of the custom properties of a resource, together with the linked resources.
```javascript
dictionary OicResourceRepresentation {
  // DateTime timestamp;
  // other properties
};
```
The OIC resources are represented as follows.
```javascript
dictionary OicResourceInit {
  OicResourceId id;
  sequence<DOMString> resourceTypes;
  sequence<DOMString> interfaces;
  sequence<DOMString> mediaTypes;
  boolean discoverable;
  boolean observable;
  boolean secure;
  boolean slow;
  OicResourceRepresentation properties;
  sequence<OicResourceId> links;
};

[NoInterfaceObject]
interface OicResource: EventTarget {
  // gets the properties of OicResourceInit, all read-only
  readonly attribute ResourceId id;
  readonly attribute sequence<DOMString> resourceTypes;
  readonly attribute sequence<DOMString> interfaces;
  readonly attribute sequence<DOMString> mediaTypes;
  readonly attribute boolean discoverable;
  readonly attribute boolean observable;
  readonly attribute boolean slow;
  readonly attribute OicResourceRepresentation properties;
  readonly attribute sequence<OicResourceId> links;  // for resource hierarchies

  attribute EventHandler<OicResourceUpdateEvent> onchange;
  attribute EventHandler ondelete;  // simple event
};

interface OicResourceUpdateEvent: Event {
  readonly attribute OicResourceInit updates;  // partial dictionary
};
```
The ```OicResource``` objects can be created only by ```findResources()```.

Adding the first event listener to ```onchange``` should send an observation request.

Removing the last event listener from ```onchange``` should send an observation cancellation request.

An ```OicResourceUpdateEvent``` contains a partial dictionary which describes the changed properties of the resource. Implementations should fire this event only if at least ```id``` and one more property are specified.

### Discovery
Discovery is exposed by the following interface:
```javascript
[NoInterfaceObject]
interface OicDiscovery: EventTarget {
  // resource discovery: fire 'resourcefound' events on results
  Promise<void> findResources(optional OicDiscoveryOptions options);

  // get device info of a given remote device, using unicast
  // id is either a device UUID or a URL (host:port)
  Promise<OicDevice> getDeviceInfo(USVString id);

  // get platform info of a given remote device, using unicast
  Promise<OicPlatform> getPlatformInfo(USVString id);

  // multicast device discovery
  Promise<void> findDevices();  // fire a 'devicefound' event for each found

  // multicast platform discovery
  Promise<void> findPlatforms();  // fire a 'platformfound' event for each found

  attribute EventHandler<OicResourceEvent> onresourcefound;
  attribute EventHandler<OicDeviceEvent> ondevicefound;
  attribute EventHandler<OicPlatformEvent> onplatformfound;
  attribute EventHandler<OicErrorEvent> ondiscoveryerror;
}

dictionary OicDiscoveryOptions {
  USVString deviceId;      // if provided, make direct discovery
  DOMString resourceType;  // if provided, include this in the discovery request
  USVString resourcePath;  // if provided, filter the results locally
  // timeout could be included later, if needed
  // unsigned long long timeout = Infinity;  // in ms
};

interface OicResourceEvent : Event {
  readonly attribute OicResource resource;
};

interface OicDeviceEvent : Event {
  readonly attribute OicDevice device;
};

interface OicPlatformEvent : Event {
  readonly attribute OicPlatform platform;
};
```
When the ```findResources()``` method is invoked, run the following steps:
- Return a Promise object ```promise``` and continue [in parallel](https://html.spec.whatwg.org/#in-parallel).
- If the functionality is not supported, reject ```promise``` with ```NotSupportedError```.
- If there is no permission to use the method, reject ```promise``` with ```SecurityError```.
- Configure a discovery request on resources as follows:
  - If ```options.resourcePath``` is specified, filter results locally.
  - If ```options.deviceId``` is specified, make a direct discovery request to that device
  - If ```options.resourceType``` is specified, include it as the ```rt``` parameter in a new endpoint multicast discovery request ```GET /oic/res``` to "All CoAP nodes" (```224.0.1.187``` for IPv4 and ```FF0X::FD``` for IPv6, port ```5683```).
- If sending the request fails, reject the Promise with ```"NetworkError"```, otherwise resolve the Promise.
- When a resource is discovered, fire an ```onresourcefound``` event with an ```OicResourceEvent``` structure, containing an ```OicResource``` object created from the received information.
- If there is an error during the discovery protocol, fire a ```discoveryerror``` event on the device with that error.

When the ```findDevices()``` method is invoked, run the following steps:
- Return a Promise object ```promise``` and continue [in parallel](https://html.spec.whatwg.org/#in-parallel).
- If the functionality is not supported, reject ```promise``` with ```NotSupportedError```.
- If there is no permission to use the method, reject ```promise``` with ```SecurityError```.
- Send a multicast request for retrieving ```/oic/d``` and wait for the answer.
- If the sending the request fails, reject the Promise with ```"NetworkError"```, otherwise resolve the Promise.
- If there is an error during the discovery protocol, fire a ```discoveryerror``` event on the device with that error.
- When a device information is discovered, fire a ```deviceinfo``` event with an ```OicDeviceEvent``` structure, containing an ```OicDevice``` object.

When the ```getDeviceInfo(id)``` method is invoked, run the following steps:
- Return a Promise object ```promise``` and continue [in parallel](https://html.spec.whatwg.org/#in-parallel).
- If the functionality is not supported, reject ```promise``` with ```NotSupportedError```.
- If there is no permission to use the method, reject ```promise``` with ```SecurityError```.
- Send a direct discovery request ```GET /oic/d``` with the given id (which can be either a device UUID or a device URL, and wait for the answer.
- If there is an error during the request, reject ```promise``` with that error.
- When the answer is received, resolve ```promise``` with an ```OicDevice``` object created from the response.

When the ```getPlatformInfo(id)``` method is invoked, run the following steps:
- Return a Promise object ```promise``` and continue [in parallel](https://html.spec.whatwg.org/#in-parallel).
- If the functionality is not supported, reject ```promise``` with ```NotSupportedError```.
- If there is no permission to use the method, reject ```promise``` with ```SecurityError```.
- Send a direct discovery request ```GET /oic/p``` with the given id (which can be either a device UUID or a device URL, and wait for the answer.
- If there is an error during the request, reject ```promise``` with that error.
- When the answer is received, resolve ```promise``` with an ```OicPlatform``` object created from the response.

### OIC Presence
The client API for accessing OIC Presence functionality.
```javascript
[NoInterfaceObject]
interface OicPresence: EventTarget {
  Promise<void> subscribe(optional USVString url);
  Promise<void> unsubscribe(optional USVString url);
  attribute EventHandler<OicDeviceChangeEvent> ondevicechange;
};

// event received for device presence notifications
interface OicDeviceChangeEvent : Event {
  readonly attribute OicChangeType type;
  readonly attribute OicDevice device;
};

enum OicChangeType { "added", "deleted", "changed" };
```
When the ```subscribe()``` method is invoked, turn on presence listening for the specified ```deviceId```, or if it is not specified, for any device. The Promise may be rejected with ```NotSupportedError```.

### OIC Client
The OIC Client API provides functionality to access (CRUD) remote resources.
```javascript
[NoInterfaceObject]
interface OicClient {
  Promise<OicResource> create(OicResourceInit resource);
  Promise<OicResource> retrieve(OicResourceId id, optional Dictionary parameters);
  Promise<void> update(OicResourceInit resource);  // partial dictionary
  Promise<void> delete(OicResourceId id);
};
```
The ```create()``` and ```update()``` methods take a resource dictionary, in which at least the ```id``` and one more property MUST be specified. It returns a Promise that gets resolved with the new resource object when  positive response from the "create" network operation is received.

The ```retrieve()``` method can take an optional parameter representing the ```REST``` query parameters passed along with the ```GET``` request as a JSON-serializable dictionary. Implementations SHOULD validate this client input to fit OIC requirements. The semantics of the parameters are application specific (e.g. requesting a resource representation in metric or imperial units). Similarly, the properties of an OIC resource representation are application specific and are represented as a JSON-serializable dictionary.
The ```retrieve()``` method returns a Promise that gets resolved with the retrieved resource object.

The ```update()``` method takes a partial dictionary that contains the properties to be updated, or the whole object. It returns a Promise that gets resolved when positive response from the "update" network operation is received.

The ```delete()``` method takes only a resource ID, and its Promise gets resolved when positive response from the "delete" network operation is received.

### OIC Server
The server API provides functionality for handling requests.
```javascript
[NoInterfaceObject]
interface OicServer: EventTarget {
  Promise<OicResource> register(OicResourceInit resource);
  Promise<void> unregister(OicResourceId id);

  // handle CRUDN requests from clients
  attribute EventHandler<OicRequestEvent> onobserverequest;
  attribute EventHandler<OicRequestEvent> onunobserverequest;
  attribute EventHandler<OicRequestEvent> onretrieverequest;
  attribute EventHandler<OicRequestEvent> ondeleterequest;
  attribute EventHandler<OicResourceEvent>  onchangerequest;
  attribute EventHandler<OicResourceEvent>  oncreaterequest;

  // update notification could be done automatically in most cases,
  // but in a few cases manual notification is needed
  Promise<void> notify(OicResourceInit resource);
  // delete notifications should be made automatic by implementations

  // enable/disable presence for this device
  Promise<void> enablePresence(optional unsigned long long ttl);  // in ms
  Promise<void> disablePresence();
};

interface OicRequestEvent : Event {
  readonly attribute OicResourceId source;
  readonly attribute OicResourceId target;
  readonly attribute Dictionary queryOptions;

  Promise<void> sendResponse(optional OicResource? resource);
    // reuses request info (type, requestId, source, target) to construct response,
    // sends back “ok”, plus the resource object if applicable

  Promise<void> sendError(DOMString? name,
                          optional DOMString? message,
                          optional OicResourceInit resource);
    // reuses request info (type, requestId, source, target) to construct response,
};

interface OicResourceEvent : OicRequestEvent {
  readonly attribute OicResourceInit resource;
};
```
The ```queryOptions``` dictionary contains resource specific properties and values, usually described in the RAML definition of the resource. It comes from the query portion of the request URI.


### Error handling
Errors are reported using ```OicError``` objects, which extend [Error](http://www.ecma-international.org/ecma-262/6.0/#sec-error-objects) with OIC specific information.
```javascript
interface OicError: Error {
  readonly attribute USVString? deviceId;
  readonly attribute OicResourceInit? resource;
};
```
The following error event is used for protocol errors:
```javascript
interface OicErrorEvent: Event {
  OicError error;
};
```

Code Examples
-------------
### Getting device configuration

```javascript
var oic = require('oic')();
if (oic.device.uuid) {  // configuration is valid
  startServer();
  startClient();
} else {
  console.log("Error: device is not configured.");
}
```

### OIC Client controlling a remote red LED.
```javascript
// Discover a remote red light, start observing it, and make sure it's not too bright.
var red = null;
function startClient() {
  // discover resources
  oic.onresourcefound = function(event) {
    if(event.resource && event.resource.id.path === "/light/ambience/red") {
      red = event.resource;
      red.on('update', redHandler);
    }
  }
  oic.findResources({ resourceType: “oic.r.light” })
    .then( () => { console.log("Resource discovery started.");})
    .catch((e) => {
      console.log("Error finding resources: " + e.message);
    });
};

function redHandler(event) {
  var red = event.resource;
  if (!red)
    return;
  console.log("Update received on " + red.id);
  console.log("Running local business logic to determine further actions...");
  if (red.properties.dimmer > 0.5) {
    // do something, e.g. limit output
    oic.update({ id: red.id, red.properties.dimmer: 0.5 })
      .then(() => { console.log("Changed red light dimmer"); })
      .catch((e) => { console.log("Error changing red light"); });
  }
};
```

### OIC Server exposing a local blue LED.

```javascript
var lightResource = null;
function startServer() {
  // register the specific resources handled by this solution
  // which are not exposed by the device firmware
  oic.registerResource({
    id: { deviceId: oic.device.uuid; path: "/light/ambience/blue" },
    resourceTypes: [ "light" ],
    interfaces: [ "/oic/if/rw" ],
    discoverable: true,
    observable: true,
    properties: { color: "blue", dimmer: 0.2 }
  }).then((res) => {
    console.log("Local resource " + res.id.path + " has been registered.");
    lightResource = res;
    oic.on("updaterequest", onLightUpdate);
    oic.on("observerequest", onLightObserve);
    oic.on("deleterequest", onLightDelete);
    oic.on("retrieverequest", onLightRetrieve);
    oic.on("createrequest", onLightCreate);
    }
  }).catch((error) => {
    console.log("Error creating resource " + error.resource.id.path + " : " + error.message);
  });
};

function onLightRetrieve(event) {
  if (event.target.id.path === lightResource.id.path) {
    event.sendResponse(lightResource)
    .catch( (err) => {
        console.log("Error sending retrieve response.");
    });
  } else {
    event.sendError("NotFoundError", "", event.resource);
    .catch( (err) => {
          console.log("Error sending retrieve error response.");
      });
  }
};

function onLightUpdate(event) {
  // the implementation has by now updated this resource (lightResource)
  // this is a hook to update the business logic
  console.log("Resource " + event.target + " updated. Running the update hook.");
  // after local processing, do the notifications manually
  oic.notify(lightResource)
    .then( () => { console.log("Update notification sent.");})
    .catch( (err) => {
        console.log("No observers or error sending: " + err.name);
    });
};

function onLightObserve(event) {
  console.log("Resource " + event.target + " observed by " + event.source + ".");
  if (event.target.id.path === lightResource.id.path) {
    event.sendResponse(lightResource)
    .catch( (err) => {
        console.log("Error sending observe response.");
    });
  }
};

function onLightDelete(event) {
  console.log("Resource " + event.target + " has been requested to be deleted.");
  console.log("Running the delete hook.");
  // clean up local state
  // notification about deletion is automatic
  event.sendResponse()
  .catch( (err) => {
      console.log("Error sending delete response.");
  });
};

function onLightCreate(event) {
  event.sendError("NotSupportedError", "", event.resource);
}
```
