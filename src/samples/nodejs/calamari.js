/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* A simple sample of nodejs Soletta API. On a Calamari Lure - on top of a
 * MinnowBoard MAX - pressing buttons should toggle each colour of RGB led.
 * Moving Calamari lever shall also change intensit of lure PWM LED1.
 */

gp = require('soletta/gpio')
spi = require('soletta/spi')
pwm = require('soletta/pwm')

var pwmLed;
var gpios = [];
var lever;

gpio_map = [{pin: 338}, {pin: 339}, {pin: 464},
            {pin: 472, direction: "in", edge: "rising"},
            {pin: 483, direction: "in", edge: "rising"},
            {pin: 482, direction: "in", edge: "rising"}]
            .map(function(pin_config) {
                return new Promise(function(fullfill) {
                    gp.open(pin_config)
                        .then((gpio) => {
                            gpios.push(gpio);
                            fullfill(gpio)
                        })
                        .catch(function(fail) {
                            console.log("Could not open gpio: ", fail);
                            fullfill();
                        });
                });
            });

/* Hack in place: I really don't care about fullfilled values, only about
 * `gpios` array itself, as it is the one that contains valid gpios.
 * If we don't have six of them, then application failed. At least, we can
 * close all gpio that were opened at exit handler */
Promise.all(gpio_map).then(_ => {
    if (gpios.length == 6) {
        btnLedControllers = [
            {led: gpios[0], btn: gpios[3]},
            {led: gpios[1], btn: gpios[4]},
            {led: gpios[2], btn: gpios[5]},
        ];
        btnLedControllers.map(function(controller) {
            var status = {status: true};
            controller.btn.onchange = function(status, event) {
                status.status = !status.status;
                controller.led.write(status.status);
            }.bind(this, status);
        });
    } else {
        process.exit();
    }
});

spi.open({bus: 0, frequency: 100 * 1000})
    .then(function(_lever) {
        lever = _lever;
        setInterval(function() {
            lever.transfer([0x01, 0x80, 0x00])
                .then(function(read) {
                    var val = (read[1] << 8 | read[2]) & 0x3ff;
                    if (pwmLed) {
                        pwmLed.setDutyCycle(Math.floor(val * 10000 / 1023));
                    }
                })
        }, 100)
    })
    .catch(function(fail){
        console.log("Could not open SPI: ", fail);
        process.exit();
    });

pwm.open({device: 0, channel: 0, period: 10000, dutyCycle: 0, enabled: true})
    .then(function(led){
        pwmLed = led;
    })
    .catch(function(fail){
        console.log("Could not open PWM: ", fail);
        process.exit();
    });

process.on("exit", (code) => {
    for (var i = 0; i < gpios.length; i++) {
        gpios[i].close();
    }

    if (lever)
        lever.close();
    if (pwmLed)
        pwmLed.close();
});
