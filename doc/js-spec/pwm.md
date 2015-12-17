PWM Web API for Soletta
=======================

Introduction
------------
This document presents a JavaScript API based on the [Soletta PWM API](http://solettaproject.github.io/docs/c-api/group__PWM.html).

Web IDL
-------
```javascript
// require returns a PWM object
// var pwm = require('pwm');

[NoInterfaceObject]
interface PWM {
  Promise<PWMPin> open(PWMPinInit init);
};

enum PWMPolarity { "normal", "inversed" };

dictionary PWMPinInit {
  DOMString name;
  unsigned long device;
  unsigned long pin;
  boolean raw = false;
  boolean enabled = true;
  long period;     // nanoseconds
  long dutyCycle;  // nanoseconds
  PWMPolarity polarity = "normal";
};

[NoInterfaceObject]
interface PWMPin {
  Promise<void> setEnabled(boolean enable);
  Promise<void> setDutyCycle(boolean enable);
  Promise<void> setPeriod(boolean enable);
  void close();
};

```

The ```PWMPin``` interface has all the properties of ```PWMPinInit``` as read-only attributes.
In ```PWMPinInit```, either ```name``` MUST be specified and map to a valid PWM path, or otherwise ```device``` and ```pin``` MUST be specified.

When a PWM pin is opened with ```raw=true```, then ```device``` and ```pin``` MUST be specified, and the UA does not try to enable multiplexing if available.

When ```period``` is specified, ```dutyCycle``` SHOULD be also specified.

#### Example
```javascript
  var pwm = require('pwm');

  pwm.open({ device: 0, channel: 1, period: 1000, dutyCycle: 500 })
  .then((pin) => {
      console.log("PWM channel 1 opened on device 0.");
      pin.close();
  }).catch( error => {
      console.log("PWM error: " + error.name);
  });
```
