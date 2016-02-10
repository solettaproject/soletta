GPIO Web API for Soletta
========================

Introduction
------------
This document presents a JavaScript API based on the [Soletta GPIO API](http://solettaproject.github.io/docs/c-api/group__GPIO.html).

GPIO pins can be opened (configured), activated, reset, and closed.

Web IDL
-------
```javascript
// require returns a GPIO object
// var gpio = require('gpio');

enum GPIOPinDirection { "in", "out" };
enum GPIOActiveEdge { "none", "rising", "falling", "any"};

dictionary GPIOPinInit {
  unsigned long pin;
  GPIOPinDirection direction = "out";
  boolean activeLow = false;
  GPIOActiveEdge edge = "any";  // on which edge interrupts are generated
  // the polling interval in milliseconds, in case interrupts are not used
  unsigned long long poll = 0;  // no polling
  boolean pullup = false;  // set voltage to ground if pin is not yet set
};

[NoInterfaceObject]
interface GPIO: {
  Promise<GPIOPin> open(GPIOPinInit init);
};

[NoInterfaceObject]
interface GPIOPin: EventTarget {
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

The ```GPIOPin``` interface has all the properties of ```GPIOPinInit``` as read-only attributes.

There is one instance of ```GPIOPin``` object for every pin id.

Closing a pin will discard its ```GPIOPin``` object, i.e. clear all its listeners and properties.

The Soletta implementation of reading a pin returns the logical state (i.e. whether it is active or not). For simplicity and conformity with other GPIO APIs, ```read()``` returns the actual value (```true``` or ```false```), and similarly, ```write()``` writes the given physical value.

By default Soletta tries to allocate an interrupt for notifying value changes of an open GPIO pin. If this is not possible, it will try to poll the pin with a user-specified polling interval. This API uses ```Infinity``` as a default polling interval, meaning there is no polling by default. Also, if there are no listeners to the pin, polling should be disabled.

The API implementation should register a callback with  the Soletta function ```sol_gpio_open()```, regardless whether there are listeners to any ```GPIOPin``` objects.

#### Example
```javascript
  var gpio = require("gpio");
  var pin3 = null;

  gpio.open({
      pin: 3
      direction: "out",
      activeLow: true,  // inverse logic: active when value is "false"
      pullup: true,  // make the pin inactive by default (set voltage high)
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

  // Open pin 1 as input, with interrupts and no polling (the default settings).
  gpio.open({ pin: 1, direction: "in" })
  .then((pin) => {
     pin.onchange = function(event) {
         console.log("GPIO pin " + pin.pin " value changed to " + event.value);
     };
      console.log("Observing GPIO pin 1 changes.");
  });

  // Open pin 2 as input, with polling only, with listener.
  gpio.open({ pin:2, direction: "in", edge: "none", poll: 1000 })
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
