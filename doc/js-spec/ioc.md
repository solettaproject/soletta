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
This API attempts generalizing how to open, write and read HW. In principle the API provides an object constructed for a given HW pin or port, and high level read and write functions. After being constructed, the object can be configured (opened), using IO type specific settings. The object can be reconfigured during its lifetime. Also, the object can be closed, after which read and write operations fail until the next configuration is successful.

Each operation may take an ```options``` parameter that differentiates between the various HW IO types and their specific options.

There can be only one pending asynchronous operation active at a time for a given IO type. Pending operations can be aborted by the client.


Web IDL
=======
The API provides entry points for GPIO, AIO, PWM, SPI, I2C and UART. They are subclassed from a common ```AbstractIO``` class (except PWM) that defines the basic methods and events. All constructors take a HW type specific init dictionary, and may take HW type specific read and write options as dictionary parameters. The behavior of methods may be different depending on IO type.

This section presents only the Web IDL of the IO objects. Examples for configuring and using these objects are presented later.

```javascript
// var ioc = require("ioc");

[NoInterfaceObject]
interface AbstractIO: EventTarget {
  Promise<Dictionary> configure(optional Dictionary configuration);  // to be overridden
  Promise<void> write(IOCData, optional WriteOptions options);
  Promise<void> startRead(optional ReadOptions options);
  Promise<void> abort();  // abort all pending write and read operations
  Promise<void> close();  // abort operations and remove event listeners

  attribute EventHandler<ReadEvent> ondata;
  attribute EventHandler<ErrorEvent> onerror;
}

typedef (Number or USVString or sequence<octet> or ArrayBuffer) IOCData;

dictionary ReadOptions {
  unsigned long minBitLength = 1;  // Minimum bit length to trigger a read event.
  unsigned long maxBitLength;  // Maximum bit length to trigger a read event.
  unsigned long leastPerSecond = 0;  // By default notify only when there is a change.
  unsigned long mostPerSecond;
}

dictionary WriteOptions {
  // no generic options, all are HW-specific
};

interface ReadEvent: Event {
  readonly attribute IOCData data;
  readonly attribute unsigned long bitLength;
};

interface ErrorEvent: Event {
  readonly attribute Error error;
};
```
In continuation each IO type define behavior for configuration, read, write, abort and close.
If any given method does not work with a specific IO type, it should reject with ```NotSupportedError``` or other, more specific Error type.

GPIO
----
```javascript
interface GPIO: AbstractIO {
  Promise<GPIOInit> configure(optional GPIOInit options);
  // startRead(), write(), abort(), close(), ondata, onerror are inherited
};

dictionary GPIOInit {
  USVString? name = null;  // Board GPIO name.
  unsigned long pin;  // If name is null, raw pin number must be specified.
  GPIOPinDirection direction = "in";  // By default writeable.
  boolean activeLow = false;
  GPIOActiveEdge edge = "any";  // on which edge interrupts are generated
  // the polling interval in milliseconds, in case interrupts are not used
  unsigned long long poll = 0;  // by default no polling
  GPIOPull pull;
};

enum GPIOPinDirection { "in", "out" };
enum GPIOActiveEdge { "none", "rising", "falling", "any"};
enum GPIOPull { "up", "down"};

interface GPIOReadEvent: ReadEvent {
  readonly attribute long data;  // Number
  // bitLength inherited
};
```
GPIO ```configure()``` resolves with the actual values of the configuration properties.

GPIO writes by default accept all data types: ```0``` and ```1``` will write one bit, higher value numbers, ```USVString``` and ```ArrayBuffer``` arguments will map to multiple consecutive write operations.

GPIO reads are by default one bit (by default ```bitLength``` is ```1```), but with ```startRead()``` it can be configured to wait for multiple bits before generating a read event.

AIO
---
```javascript
interface AIO : AbstractIO {
  Promise<AIOInit> configure(optional AIOInit options);
  // write() should reject with NotSupportedError
};

dictionary AIOInit {
  USVString? name = null;  // Board AIO name.
  unsigned long device;
  unsigned long pin;
  unsigned long precision = 12;  // Number of bits after A/D conversion.
};

interface AIOReadEvent: ReadEvent {
  readonly attribute long data;  // Number
  // bitLength inherited
};
```
AIO ```configure()``` resolves with the actual values of the configuration properties.

Invoking the ```write()``` method on an AIO object should reject with ```NotSupportedError```.


PWM
---
PWM does not extend ```AbstractIO```, it works by configuration with a dictionary comprising at least the properties that are changed from last configuration call.
```javascript
interface PWM {
  Promise<PWMInit> configure(optional PWMInit options);
  Promise<void> close();
};

enum PWMPolarity { "normal", "inversed" };
enum PWMAlignment { "left", "center", "right"};

dictionary PWMInit {
  USVString? name = null;  // Board PWM name.
  unsigned long device;
  unsigned long channel;
  boolean enabled = true;
  unsigned long period;     // nanoseconds
  unsigned long dutyCycle;  // nanoseconds
  PWMPolarity polarity = "normal";
  PWMAlignment alignment = "left";
};
```
PWM ```configure()``` resolves with the actual values of the configuration properties.


SPI
---
```javascript
interface SPI : AbstractIO {
  Promise<SPIInit> configure(optional SPIInit options);
  // transceive() could be added here, but it can be done with read() and write().
  // Read can only be triggered by write().
  // startRead() should reject with NotSupportedError.
};

enum SPIMode {
  "mode0",  // polarity normal, phase 0, i.e. sampled on leading clock
  "mode1",  // polarity normal, phase 1, i.e. sampled on trailing clock
  "mode2",  // polarity inverse, phase 0, i.e. sampled on leading clock
  "mode3"   // polarity inverse, phase 1, i.e. sampled on trailing clock
};

dictionary SPIInit {
  unsigned long bus;
  unsigned long chipSelect = 0;
  SPIMode mode = "mode0";
  octet bitsPerWord = 8;
  unsigned long frequency;  // in Hz
};

interface SPIReadEvent: ReadEvent {
  readonly attribute ArrayBuffer data;
  // bitLength inherited
};
```
SPI ```configure()``` resolves with the actual values of the configuration properties.

Invoking the ```startRead()``` method on an SPI object should reject with ```NotSupportedError```.

GPIO writes by default accept all ```IOCData``` data types, and will trigger a read operation.


I2C
---
```javascript
interface I2C : AbstractIO {
  Promise<I2CInit> configure(optional I2CInit options);
  readonly attribute boolean busy;  // implement a getter
};

enum I2CBusSpeed { "10kbps", "100kbps", "400kbps", "1000kbps", "3400kbps" };

dictionary I2CInit {
  octet bus;
  octet device;
  I2CBusSpeed speed;
  boolean raw = false;
};

interface I2CReadEvent: ReadEvent {
  readonly attribute ArrayBuffer data;
  // bitLength inherited
};

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
I2C ```configure()``` resolves with the actual values of the configuration properties.


UART
----
```javascript
interface UART : AbstractIO {
  Promise<UARTInit> configure(optional UARTInit options);
};


enum UARTBaud { "baud-9600", "baud-19200", "baud-38400", "baud-57600", "baud-115200" };
enum UARTDataBits { "databits-5", "databits-6", "databits-7", "databits-8" };
enum UARTStopBits { "stopbits-1", "stopbits-2" };
enum UARTParity { "none", "even", "odd" };

dictionary UARTInit {
  DOMString port;
  UARTBaud baud = "115200";
  UARTDataBits dataBits = "8";
  UARTStopBits stopBits = "1";
  UARTParity parity = "none";
  boolean flowControl = false;
};

interface UARTReadEvent: ReadEvent {
  readonly attribute (USVString or sequence<octet> or ArrayBuffer) data;
  // bitLength inherited
};
```

Constructing an IOC object
==========================
Creating an IOC object requires a dictionary as argument with high level preferences (```name```, ```mode```).
- ```mode``` can be either ```"pin"``` or ```"port"```.
- ```name``` identifies the pin or the port to be open. It is considered as a *board name*, mapped to internal *raw pin number* (e.g. Linux GPIO pin), or to a device number and pin number (in the case of the IO types that require both a raw pin number and a device number).


Configuring HW IO
=================
In all examples *opening an object* means construction followed by a call to ```configure()```. Configuration takes a dictionary parameter that contains the rest of the required initialization properties of the given IO type. When the ```name``` property is not provided to the constructor, the IO type is opened in "raw" mode, i.e. the low level addressing must be provided to the ```configure()``` method.

#### Opening GPIO with a label
```javascript
var ioc = require("ioc");

let gpio = new ioc.GPIO( { name: "A0" } );  // mode defaults to "pin"

await gpio.configure();  // use the default GPIOInit low level options
```

#### Opening GPIO with raw pin number
```javascript
var ioc = require("ioc");

let gpio = new ioc.GPIO();  // name defaults to null and mode defaults to "pin"

// Provide the rest of the GPIOInit parameters to configure.
await gpio.configure({ pin: 22 });
```

#### Opening PWM with a label
```javascript
var ioc = require("ioc");

let pwm = new ioc.PWM( { name: "3" } );

// Provide the rest of the PWMInit parameters to configure.
await pwm.configure( { period: 1000, dutyCycle: 500 } );
```

#### Opening PWM with device and channel number
```javascript
var ioc = require("ioc");

let pwm = new ioc.PWM();  // name defaults to null

// Provide the rest of the PWMInit parameters to configure.
await pwm.configure( {
  device: 0,
  channel: 1,
  period: 1000,
  dutyCycle: 500
} );
```

#### Opening AIO with a label
```javascript
var ioc = require("ioc");

let aio = new ioc.AIO( { name: "A1" } );

// Provide the rest of the AIOInit parameters to configure.
await aio.configure( { precision: 10 } );
```

#### Opening AIO with a device and pin number
```javascript
var ioc = require("ioc");

let aio = new ioc.AIO();

// Provide the rest of the AIOInit parameters to configure.
await aio.configure( { device: 0, pin: 1, precision: 10 } );
```

#### Opening I2C with a label
```javascript
var ioc = require("ioc");

let i2c = new ioc.I2C( { name: 8 });

// Provide the rest of the I2CInit parameters to configure.
await i2c.configure( { speed: "400kbps" } );
```

#### Opening I2C with a bus number
```javascript
var ioc = require("ioc");

let i2c = new ioc.I2C();  // name defaults to null

// Provide the rest of the I2CInit parameters to configure.
await i2c.configure( { bus: 8, speed = "400kbps" } );
```
Note that this example opens a different physical pin than when opening I2C with a label.

#### Configuring I2C with a bus and a slave device number
```javascript
var ioc = require("ioc");

let i2c = new ioc.I2C();  // name defaults to null

// Provide the rest of the I2CInit parameters to configure.
await i2c.configure( { bus: 8, device: 2, speed = "400kbps" } );
```

#### Opening SPI
SPI can be opened only by providing the low level bus number. As an exception, this can also be provided in the ```name``` parameter at construction. If the ```name``` property is provided, and the ```bus``` property is not, then ```bus``` takes the value of ```name``` that has to be a ```Number```. Otherwise the ```bus``` property overrides the ```name``` property. The ```name``` property acts as the default value for later invocations of the ```configure()``` method (when the ```bus``` property is not provided to ```configure()```.

To be consistent with the other IO APIs, the recommended usage is not to use the ```name``` property, and use the ```bus``` property, like the following example does.

```javascript
var ioc = require("ioc");

let spi = new ioc.SPI();

// Provide the rest of the SPIInit parameters to configure.
await spi.configure( {
    bus: 0,
    chipSelect:1,
    mode: "mode0",
    frequency: 1000000
  } );
```

#### Opening UART
The UART port can be specified either as ```name``` property during construction, or as the low level ```port``` property. Similarly to the SPI API, ```port``` overrides ```name```. For consistency, the recommended usage is to use ```port``` in ```configure()```. The example shows the usage with ```name```.
```javascript
var ioc = require("ioc");

let uart = new ioc.UART( { name: "ttyS0" } );

// Provide the rest of the UARTInit parameters to configure.
await uart.configure( { baud: "57600" } );

// ...

// Reconfigure with a different port and baud rate.
await uart.configure( { port:"ttyS1" } );  // baud defaults to 115200
```


Reading and writing data
========================
#### GPIO
```javascript
var ioc = require("ioc");

async function myTask() {
  try {
    let gpio = new ioc.GPIO( { name: "A0" });
    await gpio.configure( {
      direction: "in",
      activeLow: false,
      pull:"down"
    } );

    // Configure reads.
    gpio.on("error") = function(event) {
      console.log("GPIO error: " + event.error);
    };

    gpio.on("data") = function(event) {
      // For GPIO, data is Number: 0 or 1 in pin mode and higher in port mode.
      console.log("Data: " + event.data);
    };

    await gpio.startRead( {
      leastPerSecond: 0,  // only when the value is changed
      mostPerSecond: 10,  // at most 10 times per second
    } );

    // Write various data types. Implementations manage conversions and buffering.

    await gpio.write(true);  // write bit (boolean)

    await gpio.write(0x1e);  // write byte (number)

    await gpio.write("Hello world");  // write DOMString

    await gpio.write( [ 0, 12, 0x45, 0xbf, 23 ] );  // write byte array

    // Close the device. Reading and writing fails until next configure().
    await gpio.close();  // also remove listeners

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
    let pwm = new ioc.PWM( { name: "3" } );

    // Provide an error handler.
    pwm.on("error") = function(event) {
      console.log("PWM error: " + event.error);
    };

    // Provide the rest of the PWMInit parameters to configure.
    await pwm.configure( { period: 1000, dutyCycle: 500 } );
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
    let aio = new ioc.AIO( { name: "3" } );

    // Provide an error handler.
    aio.on("error") = function(event) {
      console.log("AIO error: " + event.error);
    };

    // Provide the rest of the AIOInit parameters to configure.
    await aio.configure( { precision: 10 } );

    // Configure reads.
    aio.on("data") = function(event) {
      // For AIO, data is a Number.
      console.log("Data: " + event.data);
    };

    await aio.startRead();

  } catch (err) {
    console.log("Error: " + err);
  }
};
```

#### I2C
```javascript
var ioc = require("ioc");

async function myTask() {
  try {
    let device = new ioc.I2C();
    await device.configure( { bus: 8, device: 1, speed: "400kbps" } );

    // Configure error handling.
    device.on("error") = function(event) {
      console.log("I2C error: " + event.error);
    };

    // Configure reads.
    device.on("data") = function(event) {
      // For I2C, data is ArrayBuffer.
      console.log("Data: " + new Buffer(event.data));
    };

    await device.startRead( {
      // device already provided by configure()
      register: 0x03,
      size: 4,
      repetitions: 4  // data still delivered in one read event
    } );

    // Write various data types. Implementations manage conversions and buffering.
    let writeOptions = {
      register: 0x02
    };

    await device.write(0x1e, writeOptions);  // write byte (number)

    await device.write("Hello world", writeOptions);

    await device.write( [ 0, 12, 0x45, 0xbf, 23 ], writeOptions);

    await device.write(new Buffer([0, 1, 3, 6 ]), writeOptions);

    if (device.busy) {
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
With SPI, read and write happens in one transaction. Clients need to register a read listener, then start a write.

When invoking ```startRead()```, implementations should reject it with ```NotSupportedError```.
```javascript
var ioc = require("ioc");

async function myTask() {
  try {
    let device = new ioc.SPI();
    await device.configure( {
      bus: 0,
      chipSelect: 1,
      frequency: 10000000
    } );

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

    await device.write( [ 0, 12, 0x45, 0xbf, 23 ] );

    await device.write(new Buffer([0, 1, 3, 6 ]) );

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
    let device = new ioc.UART( { name: "ttyS0" } );
    await device.configure({ baud: "57600" } );

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

    await device.write( [ 0, 12, 0x45, 0xbf, 23 ] );

    await device.write(new Buffer([0, 1, 3, 6 ]) );

    // Close the device. Reading and writing fails until next configure().
    await device.close();  // also remove listeners

  } catch (err) {
    console.log("Error: " + err);
  }
};
```
