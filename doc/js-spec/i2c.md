I2C Web API for Soletta
=======================

Introduction
------------
This document presents a JavaScript API based on the [Soletta I2C API](http://solettaproject.github.io/docs/c-api/group__I2C.html).

Web IDL
-------
```javascript
// require returns an I2C object
// var i2c = require('i2c');

[NoInterfaceObject]
interface I2C {
  Promise<I2CBus> open(I2CBusInit init);
};

typedef (boolean or DOMString or sequence<octet>) I2CData;
enum I2CBusSpeed { "10kbps", "100kbps", "400kbps", "1000kbps", "3400kbps" };

dictionary I2CBusInit {
  octet bus;
  octet device;
  boolean raw = false;
};

[NoInterfaceObject]
interface I2CBus {
  readonly attribute boolean busy;  // implement a getter
  I2CBusSpeed speed;
  Promise<I2CReader> read(octet register, octet iterations = 1);
  Promise<I2CWriter> write(I2CData data, optional octet register);
  Promise<void> writeBit(boolean data);
  void close();
};

[NoInterfaceObject]
interface I2CReader: EventTarget {
  readonly attribute DOMString id;
  readonly attribute I2CBus bus;
  readonly attribute octet? register;
  readonly attribute octet iterations;

  void abort();
  attribute EventHandler<I2CReadEvent> onfinished;
  attribute EventHandler onerror;
};

interface I2CReadEvent: Event {
  readonly attribute ArrayBuffer data;
  readonly attribute unsigned long length;
};

[NoInterfaceObject]
interface I2CWriter: EventTarget {
  readonly attribute DOMString id;
  readonly attribute I2CBus bus;
  readonly attribute octet? register;

  void abort();
  attribute EventHandler<I2CWriteEvent> onfinished;
  attribute EventHandler onerror;
};

interface I2CWriteEvent: Event {
  readonly attribute unsigned long length;
};

```
The ```I2CBus``` object has all the properties of ```I2CBusInit``` as readonly attributes.

The callback provided to the Soletta C API for [write() methods](http://solettaproject.github.io/docs/c-api/group__I2C.html#gaf328baecae0e32b78fe133d67273ed9a) will receive the number of bytes sent, but this API does not use it: in the case of success it's already known, in case of error the operation needs to be repeated anyway, with different settings or data length. Additional error information may be conveyed by ```Error.message``` when ```write()``` is rejected.

#### Example
```javascript
  var i2c = require('i2c');

  // generic read
  i2c.open({ bus: 0x08, device: 0x45, speed: "400kbps"})
  .then(bus => {
    var reader = bus.read();
    reader.onfinished = (event) => {
      console.log("Read " + event.length + " bytes.");
    };
  }).catch(err => {
    console.log("Could not open I2C bus ");
  });

  // register read (multiple times)
  i2c.open({ bus: 0x02, device: 0x11, speed: "100kbps"})
  .then(bus => {
    bus.read(0x16, 3).onfinished = (event) => {
      console.log("Read register 0x16.");
    };
  });

  // generic write
  i2c.open({ bus: 0x04, device: 0x15, speed: "1000kbps"})
  .then(bus => {
    var writer = bus.write("To be sent as byte array.");
    writer.onfinished = (event) => {
      console.log("Written " + event.length + " bytes.");
    };
    // if doesn't finish in 2 seconds, abort the write
    setTimeout(() => { writer.abort(); }, 2000);
  });

  // write to a register
  i2c.open({ bus: 0x04, device: 0x15, speed: "1000kbps"})
  .then(bus => {
    bus.write([0, 1, 2, 3, 4, 5], 0x05).onfinished = (event) => {
      console.log("Written " + event.length + " bytes to register 0x15");
    };
  });

  // write a single bit
  i2c.open({ bus: 0x14, device: 0x18, speed: "10kbps"})
  .then(bus => {
    bus.writeBit(false).then(() => { console.log("Bit reset."); });
  });

```
