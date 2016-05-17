GPIO Web API for Soletta™ Framework
========================

Introduction
------------
This document presents a JavaScript API based on the [Soletta GPIO API](http://solettaproject.github.io/docs/c-api/group__GPIO.html).

GPIO pins can be opened (configured), activated, reset, and closed.

Web IDL
-------
```javascript
// require returns a GPIO object
// var gpio = require('soletta/gpio');

[NoInterfaceObject]
interface GPIO: {
  Promise<GPIOPin> open(GPIOInit init);
};

dictionary GPIOInit {
  unsigned long pin;
  DOMString name;
  GPIOPinDirection direction = "out";
  boolean activeLow = false;
  GPIOActiveEdge edge = "any";  // on which edge interrupts are generated
  // the polling interval in milliseconds, in case interrupts are not used
  unsigned long long poll = 0;  // no polling
  GPIOPull pull;
  boolean raw = false;
};

enum GPIOPinDirection { "in", "out" };
enum GPIOActiveEdge { "none", "rising", "falling", "any"};
enum GPIOPull { "up", "down"};

[NoInterfaceObject]
interface GPIOPin: EventTarget {
  // has all the properties of GPIOInit as read-only attributes
  Promise<boolean> read();
  Promise<void> write(boolean value);
  Promise<void> close();
  attribute EventHandler<GPIOEvent> onchange;
};

interface GPIOEvent: Event {
  readonly attribute boolean value;
};
```
This API transparently uses the underlying platform's GPIO numbering and does not do any mapping on its own. Soletta is using the Linux (sysfs) GPIO numbering.

The ```open()``` method returns a ```GPIOPin``` object, enabling the possibility to set listeners to each pin, to read, write and close the pin.

In ```GPIOInit```, either ```pin``` or ```name``` MUST be specified. The latter only works if a pin multiplexer module is present.

The ```GPIOPin``` interface has all the properties of ```GPIOInit``` as read-only attributes.

There is one instance of ```GPIOPin``` object for every pin id.

The ```pull``` property of ```GPIOPin``` represents the default pin value: if ```pull == 'up'```, then the default pin value is logical 1, if ```pull == 'down'```, the default pin value is logical 0, and if ```pull === undefined```, then the implementation does not set an internal pullup or pulldown resistor, so it will rely on the existence of an external one to set the default state, or otherwise fail opening the pin.

The ```raw``` property of ```GPIOInit``` when ```true```, then the pin can be opened only using a ping number, not a name, and a pin multiplexer is not used, i.e. the [```sol_gpio_open_raw()```](http://solettaproject.github.io/docs/c-api/group__GPIO.html#gaaa42e7c282343b6b59a6080d6958818c) method is used.

Closing a pin will discard its ```GPIOPin``` object, i.e. clear all its listeners and properties.

The Soletta implementation of reading a pin returns the logical state (i.e. whether it is active or not). For simplicity and conformity with other GPIO APIs, ```read()``` returns the actual value (```true``` or ```false```), and similarly, ```write()``` writes the given physical value.

By default Soletta tries to allocate an interrupt for notifying value changes of an open GPIO pin. If this is not possible, it will try to poll the pin with a user-specified polling interval. This API uses ```0``` as a default polling interval, meaning there is no polling by default. Also, if there are no listeners to the pin, polling should be disabled.

The API implementation should register a callback with  the Soletta function ```sol_gpio_open()```, regardless whether there are listeners to any ```GPIOPin``` objects.

#### Example
```javascript
  var gpio = require("soletta/gpio");
  var pin3 = null;

  gpio.open({
      pin: 3
      direction: "out",
      activeLow: true,  // inverse logic: active when value is "false"
      pull: "up",  // make the pin inactive by default (set voltage high)
      edge: "rising" })  // only interested when it becomes inactive
  .then((pin) => {
    console.log("GPIO pin " + pin.pin + " open for writing.");
    pin3 = pin;  // save the handle
    pin.write(true);  // explicitly set the pin to inactive state
     pin.onchange = function(event) {
        console.log("GPIO pin " + pin.pin " value changed to " + event.value);
     };
  }).catch((error) => {
    console.log("Could not open GPIO pin 3 for writing.");
  });

  // Open pin A1 as input, with interrupts and no polling (the default settings).
  gpio.open({ name: 'A1', direction: "in" })
  .then((pin) => {
     pin.onchange = function(event) {
         console.log("GPIO pin " + pin.pin " value changed to " + event.value);
     };
      console.log("Observing GPIO pin A1 changes.");
  });

  // Open pin 2 as input, with polling only, with listener.
  gpio.open({ pin: 2, direction: "in", edge: "none", poll: 1000 })
  .then((pin) => {
     pin.onchange = function(event) {
       if (event.value && pin3)
          pin3.write(false)  // activate pin 3
          .then(() => {
              setTimeout(() => {
                pin3.write(true);  // deactivate pin 3 after 100 ms
              }, 100);

            });
     };
      console.log("Observing GPIO pin 2 by polling only.");
  });

```
