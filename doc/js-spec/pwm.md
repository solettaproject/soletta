PWM Web API for Soletta™ Framework
=======================

Introduction
------------
This document presents a JavaScript API based on the [Soletta PWM API](http://solettaproject.github.io/docs/c-api/group__PWM.html).

Web IDL
-------
```javascript
// require returns a PWM object
// var pwm = require('soletta/pwm');

[NoInterfaceObject]
interface PWM {
  Promise<PWMPin> open(PWMInit init);
};

enum PWMPolarity { "normal", "inversed" };
enum PWMAlignment { "left", "center", "right"};

dictionary PWMInit {
  DOMString name;
  unsigned long device;
  unsigned long channel;
  boolean raw = false;
  boolean enabled = true;
  unsigned long period;     // nanoseconds
  unsigned long dutyCycle;  // nanoseconds
  PWMPolarity polarity = "normal";
  PWMAlignment alignment = "left";
};

[NoInterfaceObject]
interface PWMPin {
  // has all the properties of PWMInit as read-only attributes
  Promise<void> setEnabled(boolean enable);
  Promise<void> setPeriod(unsigned long period);
  Promise<void> setDutyCycle(unsigned long dutyCycle);
  void close();
};

```

The ```PWMPin``` interface has all the properties of ```PWMInit``` as read-only attributes.
A PWM pin is identified by a device and a channel, or by a name.

In ```PWMInit```, either ```name``` MUST be specified and map to a valid PWM path, or otherwise ```device``` and ```channel``` MUST be specified.

When a PWM pin is opened with ```raw=true```, then ```device``` and ```channel``` MUST be specified, and the UA does not try to enable multiplexing if available.

When ```period``` is specified, ```dutyCycle``` SHOULD be also specified.

#### Example
```javascript
  var pwm = require('soletta/pwm');

  pwm.open({ device: 0, channel: 1, period: 1000, dutyCycle: 500 })
  .then((pin) => {
      console.log("PWM channel 1 opened on device 0.");
      pin.close();
  }).catch( error => {
      console.log("PWM error: " + error.name);
  });
```
