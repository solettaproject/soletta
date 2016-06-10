UART Web API for Soletta™ Framework
========================

Introduction
------------
This document presents a JavaScript API based on the [Soletta UART API](http://solettaproject.github.io/docs/c-api/group__UART.html).

Web IDL
-------
```javascript
// require returns an UART object
// var uart = require('soletta/uart');

[NoInterfaceObject]
interface UART {
  Promise<UARTConnection> open(UARTInit init);
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

typedef (USVString or sequence<octet> or ArrayBuffer) UARTData;

[NoInterfaceObject]
interface UARTConnection: EventTarget {
  // has all the properties of UARTInit as read-only attributes
  void close();
  Promise<void> write(UARTData data);
  attribute EventHandler<UARTEvent> onread;
};

interface UARTEvent: Event {
  readonly attribute octet data;
};

```
The ```UARTData``` type refers to either a string or an array of octets (unsigned bytes) that MAY be represented as an array of numbers, or as ```ArrayBuffer``` or as ```Buffer```.

The callback provided to the Soletta C API for [write()](http://solettaproject.github.io/docs/c-api/group__UART.html) will receive the number of characters sent, but this API does not use it: in the case of success it's already known, in case of error the operation needs to be repeated anyway, with different settings or data length. Additional error information may be conveyed by ```Error.message``` when ```write()``` is rejected.

#### Example
```javascript
  var uart = require('soletta/uart');
  uart.open({ port: "ttyS0" }).then( conn => {
    conn.onread = function(event) {
      console.log("UART data: " + event.data);
    };
    conn.write("UART test").then(() => {
      console.log("Sent on UART.");
      conn.write([ 0, 1, 2, 3, 4 ]).then(() => {
        conn.close();
      });
    });
  }).catch( error => {
    console.log("UART error: " + error.name);
  });
