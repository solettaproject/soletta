SPI Web API for Soletta
=======================

Introduction
------------
This document presents a JavaScript API based on the [Soletta SPI API](http://solettaproject.github.io/docs/c-api/group__SPI.html).

Web IDL
-------
```javascript
// require returns an SPI object
// var spi = require('spi');

[NoInterfaceObject]
interface SPI {
  Promise<SPIBus> open(SPIBusInit init);
};

typedef (DOMString or sequence<octet>) SPIData;

enum SPIMode {
  "mode0",  // polarity normal, phase 0, i.e. sampled on leading clock
  "mode1",  // polarity normal, phase 1, i.e. sampled on trailing clock
  "mode2",  // polarity inverse, phase 0, i.e. sampled on leading clock
  "mode3"   // polarity inverse, phase 1, i.e. sampled on trailing clock
};

dictionary SPIBusInit {
  unsigned long bus;
  SPIMode mode = "mode0";
  unsigned long chipSelect = 0;
  octet bitsPerWord = 8;
  unsigned long frequency;  // in Hz
};

[NoInterfaceObject]
interface SPIBus {
  Promise<void> transfer(SPIData data);
  void close();
};

```
The ```SPIBus``` interface has all the properties of ```SPIBusInit``` as read-only attributes.

_Note_: while SPI modes could have been broken up to polarity and phase, the native API uses symbolic modes, so there would be no gain for using polarity and phase instead of modes.

#### Example
```javascript
  var spi = require('spi');

  spi.open({ bus: 0, frequency: 10000000 })
  .then((bus) => {
      console.log("SPI bus 0 opened.");
      bus.transfer([ 0, 1, 2, 3, 4 ])
      .then(function(){
          console.log("SPI bus 0: data transferred.");
      });
  }).catch( error => {
      console.log("SPI error: " + error.name);
  });
```
