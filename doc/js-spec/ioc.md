High level JavaScript IO API
============================

Introduction
------------

This document presents high level JavaScript convenience API for accessing IO, in particular GPIO, AIO, I2C, PWM, SPI and UART.

This API is using the lower level HW IO APIs to implement higher level convenience functions that are close to, and can be used for implementing the [W3C Generic Sensor API](https://www.w3.org/TR/generic-sensor/) or the [OCF API](./oic.md).

#### IO pin multiplexing
On most boards, GPIO pins can be configured as GPIO, PWM or AIO pins.

*GPIO* can have 3 states, input (high impedance), output low and output high. A GPIO pin can be read-only (```"out"```), or read-write (```in```). They have a pullup-resistor that defines default state, and are configurable with various options, such as direction, active level, interrupt generation mode, etc.

Boards may use *PWM* (pulse width modulation) controllers on GPIO pins, making it possible to rapidly alternate the *output-low* and *output-high* states following a given temporal pattern, thereby creating a digital signal characterized by a *duty cycle* that can be filtered to represent an analog value. As such, it can be used for dimming lights or controlling motor speeds.
PWM pins are identified with a *device* and a *pin*.

GPIO pins can also be multiplexed with an ADC (Analog to Digital Converter) controller, exposed as *AIO* (Asynchronous Input-Output), identified by a *device* and a *pin*. AIO pins don't have a pullup-resistor or configuration other than a conversion precision that may or may not be supported by a given board. The converted bits are latched to provide a multi-bit output.

A group of GPIO pins can be configured to a *port*, for instance a GPIO port can comprise 8, 16 or 32 pins. This API assumes that ports are exposed with labels by the underlying board implementations.

*UART* (Universal Asynchronous Receiver Transmitter) is a serial (time-multiplexed) communication protocol over two wires/pins (transmit and receive). Addressing is by ports, specified as strings, and data is transferred in octets.

*SPI* (serial peripheral interface) is a serial protocol between a master device and one or more slave devices (by using slave select signal, or daisy chaining). The master provided clock signal writes one bit to the slave and reads one bit from the slave. In order to read SPI, the client must write first, and data transfer is done by a single transaction from the API point of view. The data transfer can be bit-based, byte-based or packet based. This API models the more generic packet based communication, and simulates the transfer by a write operation used together with a read listener. Usually 3 or 4 pins are used (in, out, clock and GPIO), and they are addressed as an SPI bus number. The chip select option selects the slave.

*I2C* (Inter-Integrated Circuit) provides a bus functionality over two wires/pins (clock and data) between a master and many slave devices, but instead of slave select, it supports 7 bit (basic) or 10 bit (extended) addresses (registers), and octet based data transfer.


#### IO pin mapping
A client that wants to use GPIO needs to know the hardware configuration of GPIO on the specific HW board. For instance a given pin name in the board documentation is mapped as a "label" to the internal representation in the SW stack (which are called the "raw" pin values).

Some pins can be configured to map to GPIO, AIO or PWM, for instance on the Intel Edison board the label ```"A0"``` can map to GPIO raw value ```44``` or AIO pin ```0``` on AIO device ```1```. The label ```"6"``` can be mapped to GPIO raw pin ```182```, and PWM pin ```1``` on PWM device ```0```. The mapping between labels and raw pin value is maintained by the implementation and it is not exposed by this API.



#### API design considerations
This API attempts generalizing how to open, write and read HW. In principle the API provides an object constructed for a given HW pin or port, and high level read and write functions. After bein constructed, the object can be configured (opened) for read and write, using IO type (e.g. GPIO, I2C, etc) specific settings. The object can be reconfigured during its lifetime. Also, the object can be closed, after which read and write operations fail until the next configuration is successful.

Each operation may take an ```options``` parameter that differentiates between the various HW IO types and their specific options.

There can be only one pending asynchronous operation active at a time for a given IO type. Pending operations can be aborted by the client.

To ease transition, the ```configure()``` method returns a low level IO type specific object, such as [```GPIOConnection```](./gpio.md), and from there one could use that given functionality. However, this API attempts to unify the IO API read and write operations exposed in the ```IOC``` objects.

For instance, the following example will look similar for GPIO, or I2C, or SPI, only differing in the configuration parameters.

```javascript
var async = require('asyncawait/async');
var await = require('asyncawait/await');
var io = require("ioc");

async function myTask() {
  try {
    let mydevice = new IOC({
      name: "A0",  // board name for pin
      mode: "pin",
      type: "gpio",  // could also be "pwm", "i2c", "uart", "spi", "aio"
    });

    let lowlevelGPIO = await mydevice.configure({
        direction: "in",
        activeLow: true,
        edge: "any",
        pull: "up"});

    // configure() returns a reference to the low level API object
    console.log("lowlevelGPIO instanceof GPIOConnection":
        lowlevelGPIO instanceof GPIOConnection);

    // It is usable as described in the low-level APIs.
    lowlevelGPIO.write(true);

    // Now let's use the high level API defined in IOC.
    mydevice.on("data", function(data, isLastChunk) {
      console.log("Received 4 bytes: " + data + isLastChunk ? "." : "...");
    };

    // will generate read events
    await mydevice.startRead({
      leastPerSecond: 0,  // only when the value is changed
      mostPerSecond: 10,  // at most 10 times per second
    });

    await mydevice.write(true);  // write bit (boolean)

    await mydevice.write(0x1e);  // write byte (number)

    await mydevice.write("Hello world");  // write DOMString

    await mydevice.write( [ 0, 12, 0x45, 0xbf, 23 ] );  // write byte array

    await mydevice.close();  // also remove listeners

  } catch (err) {
    console.log("Error: " + err);
  }
};
```

Web IDL
-------

```javascript
// var ioc = require("ioc");

[Constructor(IOCInit init)]
interface IOC: EventTarget {
  readonly attribute USVString? name;
  readonly attribute IOCMode mode;
  readonly attribute IOCType type;

  // configure() accepts low level type specific dictionaries like GPIOInit
  Promise<LowLevelIO> configure(optional OpenOptions options);
  Promise<void> write(IOCData, optional WriteOptions options);
  Promise<void> startRead(optional ReadOptions options);
  Promise<void> abort();  // abort all pending write and read operations
  Promise<void> close();  // abort operations and remove event listeners

  attribute EventHandler<ReadEvent> ondata;
  attribute EventHandler<ErrorEvent> onerror;
}

dictionary IOCInit {
  USVString? name;  // if null, raw pin/device must be provided to open()
  IOCMode mode;  // "pin" or "port"
  IOCType type;
}

enum IOCMode { "pin", "port" };

enum IOCType { "gpio", "pwm", "aio", "uart", "spi", "i2c" };

typedef (boolean or Number or USVString or ArrayBuffer) IOCData;

typedef (GPIOInit or PWMInit or AIOInit or SPIInit or I2CInit or UARTInit ) OpenOptions;

typedef (GPIOConnection or PWMConnection or AIOConnection or SPIConnection or
         I2CConnection or UARTConnection) LowLevelIO;

dictionary ReadOptions {
  unsigned long maxChunkSize;
  unsigned long leastPerSecond = 0,  // notify only when there is a change
  unsigned long mostPerSecond
}

dictionary WriteOptions {
  // no generic options, all options are HW-specific
};

interface ReadEvent: Event {
  readonly attribute IOCData data;
  readonly attribute boolean isLastChunk;
};

interface ErrorEvent: Event {
  readonly attribute Error error;
};

```

Constructing an IOC object
--------------------------
Creating an IOC object requires a dictionary as argument with high level preferences (```name```, ```type```, ```mode```).
- ```mode``` can be either ```"pin"``` or ```"port"```.
- ```type``` can be ```"gpio"```, ```"i2c"```, ```"aio"```, ```"spi"```, ```"pwm"```, ```"uart"```.
- ```name``` identifies the pin or the port to be open. It is considered as a *board name*, mapped to internal *raw pin number* (e.g. Linux GPIO pin), or to a device number and pin number (in the case of the IO types that require both a raw pin number and a device number).


Opening and configuring HW IO
-----------------------------
In all examples the construction of the ```IOC``` object is followed by a call to ```configure()```. Configuration takes a dictionary parameter that contains the rest of the required initialization properties of the given IO type. When the ```name``` property is not provided to the constructor, the IO type is opened in "raw" mode, i.e. the low level addressing must be provided to the ```configure()``` method.

#### Opening GPIO with a label
```javascript
var ioc = require("ioc");

let device = new IOC( { name: "A0", mode: "pin", type: "gpio"});

let ll = await device.configure();  // use the default GPIOInit low level options
```

#### Opening GPIO with raw pin number
```javascript
var ioc = require("ioc");

let device = new IOC( { type: "gpio" });  // name defaults to null and mode defaults to "pin"

// Provide the rest of the GPIOInit parameters to configure.
let ll = await device.configure({ pin: 22 });
```

#### Opening PWM with a label
```javascript
var ioc = require("ioc");

let device = new IOC( { type: "pwm", name: "3" });

// Provide the rest of the PWMInit parameters to configure.
let ll = await device.configure({ period: 1000, dutyCycle: 500 });
```

#### Opening PWM with device and channel number
```javascript
var ioc = require("ioc");

let device = new IOC( { type: "pwm" });  // name defaults to null

// Provide the rest of the PWMInit parameters to configure.
let ll = await device.configure({
  device: 0,
  channel: 1,
  period: 1000,
  dutyCycle: 500 });
```

#### Opening AIO with a label
```javascript
var ioc = require("ioc");

let device = new IOC( { type: "aio", name: "A1" });

// Provide the rest of the AIOInit parameters to configure.
let ll = await device.configure({ precision: 10 });
```

#### Opening AIO with a device and pin number
```javascript
var ioc = require("ioc");

let device = new IOC( { type: "aio" });

// Provide the rest of the AIOInit parameters to configure.
let ll = await device.configure({ device: 0, pin: 1, precision: 10 });
```

#### Opening I2C with a label
```javascript
var ioc = require("ioc");

let device = new IOC( { type: "i2c", name: 8 });

// Provide the rest of the I2CInit parameters to configure.
let ll = await device.configure({ speed = "400kbps" });
```

#### Opening I2C with a bus number
```javascript
var ioc = require("ioc");

let device = new IOC( { type: "i2c" });  // name defaults to null

// Provide the rest of the I2CInit parameters to configure.
let ll = await device.configure({ bus: 8, speed = "400kbps" });
```
Note that this example opens a different physical pin than when opening I2C with a label.

#### Configuring I2C with a bus and a slave device number
When also a device number is provided, that will be taken as the default value for ```write()``` and ```startRead()```.
```javascript
var ioc = require("ioc");

let device = new IOC( { type: "i2c" });  // name defaults to null

// Provide the rest of the I2CInit parameters to configure.
let ll = await device.configure({ bus: 8, device: 2, speed = "400kbps" });
```

#### Opening SPI
SPI can be opened only by providing the low level bus number. As an exception, this can also be provided in the ```name``` parameter at construction. If the ```name``` property is provided, and the ```bus``` property is not, then ```bus``` takes the value of ```name``` that has to be a ```Number```. Otherwise the ```bus``` property overrides the ```name``` property. The ```name``` property acts as the default value for later invocations of the ```configure()``` method (when the ```bus``` property is not provided to ```configure()```.

To be consistent with the other IO APIs, the recommended usage is not to use the ```name``` property, and use the ```bus``` property, like the following example does.

```javascript
var ioc = require("ioc");

let device = new IOC( { type: "spi" });  // SPI requires a bus number

// Provide the rest of the SPIInit parameters to configure.
let ll = await device.configure({
    bus: 0,
    chipSelect:1,
    mode: "mode0",
    frequency: 1000000 });
```

#### Opening UART
The UART port can be specified either as ```name``` property during construction, or as the low level ```port``` property. Similarly to the SPI API, ```port``` overrides ```name```. For consistency, the recommended usage is to use ```port``` in ```configure()```. The example shows the usage with ```name```.
```javascript
var ioc = require("ioc");

let device = new IOC( { type: "uart", name: "ttyS0" });

// Provide the rest of the UARTInit parameters to configure.
let ll = await device.configure({ baud: "57600" });

// ...

// Reconfigure with a different port and baud rate.
ll = await device.configure({ port:"ttyS1" });  // baud defaults to 115200
```


Reading and writing data
------------------------
#### GPIO
```javascript
var ioc = require("ioc");

async function myTask() {
  try {
    let device = new IOC( { name: "A0", mode: "pin", type: "gpio"});
    let ll = await device.configure({
      direction: "in",
      activeLow: false,
      pull:"down" });

    // Configure reads.
    device.on("error") = function(event) {
      console.log("GPIO error: " + event.error);
    };

    device.on("data") = function(event) {
      // For GPIO, data is Number: 0 or 1 in pin mode and higher in port mode.
      console.log("Data: " + event.data);
    };

    await device.startRead({
      leastPerSecond: 0,  // only when the value is changed
      mostPerSecond: 10,  // at most 10 times per second
    });

    // Write various data types. Implementations manage conversions and buffering.

    await device.write(true);  // write bit (boolean)

    await device.write(0x1e);  // write byte (number)

    await device.write("Hello world");  // write DOMString

    await device.write( [ 0, 12, 0x45, 0xbf, 23 ] );  // write byte array

    // Close the device. Reading and writing fails until next configure().
    await device.close();  // also remove listeners

  } catch (err) {
    console.log("Error: " + err);
  }
};
```

#### PWM
PWM can be only configured, therefore invoking ```read() or ```write()``` on a PWM device must always fail with ```NotSupportedError```. Also, it is possible to attach event listeners to the ```data``` event, but it will never fire. The ```error``` event will fire when an error happens.
```javascript
var ioc = require("ioc");

async function myTask() {
  try {
    let device = new IOC( { type: "pwm", name: "3" });

    // Provide an error handler.
    device.on("error") = function(event) {
      console.log("PWM error: " + event.error);
    };

    // Provide the rest of the PWMInit parameters to configure.
    let ll = await device.configure({ period: 1000, dutyCycle: 500 });
  } catch (err) {
    console.log("Error: " + err);
  }
};
```

#### AIO
AIO can be only configured and read, therefore invoking ```write()``` on an AIO device must always fail with ```NotSupportedError```.
```javascript
var ioc = require("ioc");

async function myTask() {
  try {
    let device = new IOC( { type: "aio", name: "3" });

    // Provide an error handler.
    device.on("error") = function(event) {
      console.log("AIO error: " + event.error);
    };

    // Provide the rest of the AIOInit parameters to configure.
    let ll = await device.configure({ precision: 10 });

    // Configure reads.
    device.on("data") = function(event) {
      // For AIO, data is a Number.
      console.log("Data: " + event.data);
    };

    await device.startRead();

  } catch (err) {
    console.log("Error: " + err);
  }
};
```

#### I2C
I2C ```startRead()``` and ```write``` methods take a dictionary parameter for options as described by the following IDL:
```javascript
dictionary I2CReadOptions: ReadOptions {
  unsigned long size;  // the number of octets to be read
  octet register;  // optional register to read
  unsigned long repetitions = 1  // optional number of reads
};

dictionary I2CWriteOptions: WriteOptions {
  octet register;  // optional register address
  boolean writeBit = false;  // if true, write one bit to 'device' and ignore 'register'
};
```
The data types accepted for ```write()``` are Number, or ArrayBuffer, or array of Numbers.
The data type for reads is sequence of octets, i.e. ArrayBuffer.
```javascript
var ioc = require("ioc");

async function myTask() {
  try {
    let device = new IOC( { type: "i2c"});
    let ll = await device.configure({ bus: 8, device: 1, speed: "400kbps" });

    // Configure error handling.
    device.on("error") = function(event) {
      console.log("I2C error: " + event.error);
    };

    // Configure reads.
    device.on("data") = function(event) {
      // For I2C, data is ArrayBuffer.
      console.log("Data: " + new Buffer(event.data));
    };

    await device.startRead({
      // device already provided by configure()
      register: 0x03,
      size: 4,
      repetitions: 4  // data still delivered in one read event
    });

    // Write various data types. Implementations manage conversions and buffering.
    let writeOptions = {
      register: 0x02
    };

    await device.write(0x1e, writeOptions);  // write byte (number)

    await device.write("Hello world", writeOptions);

    await device.write( [ 0, 12, 0x45, 0xbf, 23 ], writeOptions);

    let buf = new Buffer([0, 1, 3, 6 ]);
    await device.write(buf, writeOptions);

    // For telling whether the I2C device is busy, we need to use the low level API.
    if (ll.busy) {
      console.log("The I2C device is busy.");
    }

    // Close the device. Reading and writing fails until next configure().
    await device.close();  // also remove listeners

  } catch (err) {
    console.log("Error: " + err);
  }
};
```

#### SPI
With SPI, read and write happens in one transaction. Clients need to register a read listener, then start a write. The data type accepted by ```write()``` is ```ArrayBuffer``` or array of Numbers or ```string```.

When invoking ```startRead()```, implementations should reject it with ```NotSupportedError```.
```javascript
var ioc = require("ioc");

async function myTask() {
  try {
    let device = new IOC( { type: "spi"});
    let ll = await device.configure({ bus: 0, chipSelect: 1, frequency: 10000000 });

    // Configure error handling.
    device.on("error") = function(event) {
      console.log("SPI error: " + event.error);
    };

    // Configure reads.
    device.on("data") = function(event) {
      // For SPI, data is ArrayBuffer.
      console.log("Data: " + new Buffer(event.data));
    };

    // Write various data types. Implementations manage conversions and buffering.
    // Each write will trigger a read event.

    await device.write("Hello world");

    await device.write( [ 0, 12, 0x45, 0xbf, 23 ]);

    await device.write(new Buffer([0, 1, 3, 6 ]));

    // Close the device. Reading and writing fails until next configure().
    await device.close();  // also remove listeners

  } catch (err) {
    console.log("Error: " + err);
  }
};
```

#### UART
On UART, ```startRead()``` may poll for available data, but may also be without effect.
The ```write()``` method accepts data as string, or array or numbers, or as ```ArrayBuffer```.

```javascript
var ioc = require("ioc");

async function myTask() {
  try {
    let device = new IOC( { type: "uart", name: "ttyS0" });
    let ll = await device.configure({ baud: "57600" });

    // Configure error handling.
    device.on("error") = function(event) {
      console.log("UART error: " + event.error);
    };

    // Configure reads.
    device.on("data") = function(event) {
      // For UART, data is ArrayBuffer.
      console.log("Data: " + new Buffer(event.data));
    };

    await device.startRead();  // no effect

    // Write various data types. Implementations manage conversions and buffering.

    await device.write("Hello world");

    await device.write( [ 0, 12, 0x45, 0xbf, 23 ]);

    await device.write(new Buffer([0, 1, 3, 6 ]));

    // Close the device. Reading and writing fails until next configure().
    await device.close();  // also remove listeners

  } catch (err) {
    console.log("Error: " + err);
  }
};
```
