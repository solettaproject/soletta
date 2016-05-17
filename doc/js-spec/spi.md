SPI Web API for Soletta™ Framework
=======================

Introduction
------------
This document presents a JavaScript API based on the [Soletta SPI API](http://solettaproject.github.io/docs/c-api/group__SPI.html).

Web IDL
-------
```javascript
// require returns an SPI object
// var spi = require('soletta/spi');

[NoInterfaceObject]
interface SPI {
  Promise<SPIBus> open(SPIInit init);
};

typedef (sequence<octet> or ArrayBuffer) SPIData;

enum SPIMode {
  "mode0",  // polarity normal, phase 0, i.e. sampled on leading clock
  "mode1",  // polarity normal, phase 1, i.e. sampled on trailing clock
  "mode2",  // polarity inverse, phase 0, i.e. sampled on leading clock
  "mode3"   // polarity inverse, phase 1, i.e. sampled on trailing clock
};

dictionary SPIInit {
  unsigned long bus;
  SPIMode mode = "mode0";
  unsigned long chipSelect = 0;
  octet bitsPerWord = 8;
  unsigned long frequency;  // in Hz
};

[NoInterfaceObject]
interface SPIBus {
  // has all the properties of SPIInit as read-only attributes
  Promise<SPIData> transfer(SPIData txData);
  void close();
};

```

The ```SPIData``` type refers to an array of octets (unsigned bytes) and MAY be represented as an array of numbers, or as ```ArrayBuffer``` or as ```Buffer```.

The ```SPIBus``` interface has all the properties of ```SPIInit``` as read-only attributes.

_Note_: while SPI modes could have been broken up to polarity and phase, the native API uses symbolic modes, so there would be no gain for using polarity and phase instead of modes.

The ```transfer()``` method concurrently sends and receives a number of octets. The received data size is always smaller or equal to the transmitted data size.
The promise the ```transfer()``` method returns resolves with the received data as a byte array.

#### Example
```javascript
  var spi = require('soletta/spi');

  spi.open({ bus: 0, frequency: 10000000 })
  .then((bus) => {
      console.log("SPI bus 0 opened.");
      bus.transfer([ 0, 1, 2, 3, 4 ])
      .then(function(rx){
          console.log("SPI bus 0: data transferred.");
          console.log("SPI bus 0 received " + rx.length + " octets.");
      });
  }).catch( error => {
      console.log("SPI error: " + error.name);
  });
```
