I2C Web API for Soletta™ Framework
=======================

Introduction
------------
This document presents a JavaScript API based on the [Soletta I2C API](http://solettaproject.github.io/docs/c-api/group__I2C.html).

Web IDL
-------
```javascript
// require returns an I2C object
// var i2c = require('soletta/i2c');

[NoInterfaceObject]
interface I2C {
  Promise<I2CBus> open(I2CInit init);
};

enum I2CBusSpeed { "10kbps", "100kbps", "400kbps", "1000kbps", "3400kbps" };

dictionary I2CInit {
  octet bus;
  I2CBusSpeed speed;
  boolean raw = false;
};

[NoInterfaceObject]
interface I2CBus {
  // has all the properties of I2CInit as read-only attributes
  readonly attribute boolean busy;  // implement a getter
  Promise<sequence<octet>> read(octet device,
                                unsigned long size,
                                optional octet register,
                                optional repetitions = 1);
  Promise<void> write(octet device, (USVString or sequence<octet>) data, optional octet register);
  Promise<void> writeBit(octet device, boolean data);
  void abort();  // abort all current read / write operations
  void close();
};

```
The ```I2CBus``` object has all the properties of ```I2CInit``` as readonly attributes.

I2C allows only one operation at a time on a given bus, either read or write. The ```abort()``` method cancels the current ongoing pending operation on the bus (either a read or a write), and their ```Promise``` SHOULD be rejected with ```AbortError```.

If the ```read()``` or ```write()``` or ```writeBit()``` methods are invoked while there is an already ongoing operation, they SHOULD reject with a ```QuotaExceededError```. Otherwise if there is an error, they SHOULD reject with ```NetworkError``` for communication errors, ```TimeoutError``` for timeout, ```TypeMismatchError``` if parameters are invalid, or ```SecurityError``` if the operation is not allowed.

The ```open()``` method resolves with an object for controlling a given I2C bus.

The ```read()``` method combines [```sol_i2c_set_slave_address()```](http://solettaproject.github.io/docs/c-api/group__I2C.html#gae778e276d19675d0113711629a9cb40a) with all Soletta read related methods.
The ```device``` parameter specifies the slave address of the operation. The ```size``` parameter represents the number of octets to be read. The ```register``` parameter is optional. The ```repetitions``` parameter is only relevant when ```register``` is provided, and maps to [```sol_i2c_read_register_multiple()```](http://solettaproject.github.io/docs/c-api/group__I2C.html#gabf3bc641d763b31d2e0db61761a67c5b).
Implementations SHOULD resolve the ```Promise``` with the read byte array, or reject with an Error.

The ```write()``` method combines [```sol_i2c_set_slave_address()```](http://solettaproject.github.io/docs/c-api/group__I2C.html#gae778e276d19675d0113711629a9cb40a) with [```sol_i2c_write()```](http://solettaproject.github.io/docs/c-api/group__I2C.html#gaf328baecae0e32b78fe133d67273ed9a) and [```sol_i2c_write_register()```](http://solettaproject.github.io/docs/c-api/group__I2C.html#ga6da92cd3bac0a28234f3f95865afa6cb) methods.
The ```device``` parameter specifies the slave address of the operation.
The ```data``` parameter MAY be an array of numbers with values between 0 and 255, or an ```ArrayBuffer``` or a ```Buffer```.

If the ```register``` parameter is provided, the write is done on the register. If the ```data``` parameter is ```USVString```, then implementations SHOULD convert it to byte array before transmitting.

The callback provided to the Soletta C API for [write() methods](http://solettaproject.github.io/docs/c-api/group__I2C.html#gaf328baecae0e32b78fe133d67273ed9a) will receive the number of bytes sent, but this API does not use it: in the case of success it's already known, in case of error the operation needs to be repeated anyway, with different settings or data length. Additional error information may be conveyed by ```Error.message``` when ```write()``` is rejected.

The ```writeBit()``` method writes only one bit, and maps to the [```sol_i2c_write_quick()```](http://solettaproject.github.io/docs/c-api/group__I2C.html#ga07bd4788ce4eb74e1d0e395a98e5c4be) method.

#### Example
```javascript
  var i2c = require('soletta/i2c');

  // generic read
  i2c.open({ bus: 0x08, speed: "400kbps"})
  .then(bus => {
    bus.read(0x45, 4).then( () => {
      console.log("Read 4 bytes from slave address 0x45.");
    });
  }).catch(err => {
    console.log("Could not open I2C bus ");
  });

  // register read (multiple times)
  i2c.open({ bus: 0x02, speed: "100kbps"})
  .then(bus => {
    bus.read(0x11, 4, 0x16).then(() => {
      console.log("Read 4 bytes from device 0x11 register 0x16.");
    })
  }).catch(err => {
    console.log("Could not open I2C bus ");
  });

  // generic write
  i2c.open({ bus: 0x04, speed: "1000kbps"})
  .then(bus => {
    var timerId = setTimeout(() => { bus.abort(); }, 2000);
    bus.write(0x15, "To be sent as byte array.").then(() => {
      clearTimeout(timerId);
      console.log("Write finished on device 0x15.");
    }).catch(err => {
      console.log("Write failed: " + err.message);
    });
    // if doesn't finish in 2 seconds, abort the write
  });

  // write to a register
  i2c.open({ bus: 0x04, device: 0x15, speed: "1000kbps"})
  .then(bus => {
    bus.write(0x15, [0, 1, 2, 3, 4, 5], 0x05).then(() => {
      console.log("Written 6 bytes to register 0x05 on device 0x15.");
    });
  });

  // write a single bit
  i2c.open({ bus: 0x14, speed: "10kbps"})
  .then(bus => {
    bus.writeBit(0x18, false).then(() => {
      console.log("Bit reset on device 0x18.");
    });
  });

```
