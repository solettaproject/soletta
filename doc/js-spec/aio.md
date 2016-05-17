AIO Web API for Soletta™ Framework
=======================

Introduction
------------
This document presents a JavaScript API based on the [Soletta AIO API](http://solettaproject.github.io/docs/c-api/group__AIO.html).

Web IDL
-------
```javascript
// require returns an AIO object
// var aio = require('soletta/aio');

[NoInterfaceObject]
interface AIO {
  Promise<AIOPin> open(AIOInit init);
};

dictionary AIOInit {
  DOMString name;
  unsigned long device;
  unsigned long pin;
  boolean raw = false;
  unsigned long precision = 12;
};

[NoInterfaceObject]
interface AIOPin: {
  // has all the properties of AIOInit as read-only attributes
  Promise<unsigned long> read();
  void abort();
  void close();
};

```

In ```AIOInit```, either ```name``` (label) MUST be specified and map to a valid AIO path, or otherwise, ```device``` and ```pin``` MUST be specified.

When ```raw=true```, then ```device``` and ```pin``` MUST be specified, and the UA does not try to enable multiplexing if that is available.

Precision is the number of valid bits on the data coming from the AD converter. A corresponding mask will be applied to the least significant bits of the data. By default it is 12 bits (data range is between 0 and 4096).

The ```abort()``` method cancels the current pending read operation on a given pin.

#### Example
```javascript
  var aio = require('soletta/aio');

  aio.open({ device: 0, pin: 1})
  .then((pin) => {
      console.log("AIO channel 1 opened on device 0.");
      pin.read().then((data) => {
          console.log("AIO data: " + data);
          pin.close();
      });
  }).catch( error => {
      console.log("AIO error: " + error.name);
  });
```
