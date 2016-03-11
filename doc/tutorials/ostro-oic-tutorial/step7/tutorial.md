# Step 7 - Using real actuators

## Using Calamari LED

Our application is mostly done. However it doesn't change anything -
our server can keep state and power values, but how to apply them
to the 'real world'? It's just a matter of connecting this values
to nodes that represent real actuators.
To demonstrate it, let's connect `POWER` and `STATE` ports
of `http_server` to actuators instead of `console`:

```sh
http_server POWER -> IN power_actuator(PowerActuator)
http_server STATE -> IN state_actuator(StateActuator)
```

And what are `PowerActuator` and `StateActuator`? Node types defined
on configuration files. For desktop, `sol-flow.json` have:

```json
{
    "name": "StateActuator",
        "options": {
            "prefix": "state: "
        },
        "type": "console"
},
{
    "name": "PowerActuator",
    "options": {
        "prefix": "power: "
    },
    "type": "console"
}
```

It's just a `console` node type. But on MinnowBoard MAX with Calamari
we may have something cooler:

```json
{
    "name": "StateActuator",
    "options": {
        "pin": "339",
        "raw": true
    },
    "type": "gpio/writer"
},
{
    "name": "PowerActuator",
    "options": {
        "address": 1,
        "period": 10000,
        "range": "min:0|max:10000|step:1"
    },
    "type": "calamari/led"
}
```

`StateActuator` turns ON/OFF Calamari Red LED, and `PowerActuator`
controls Calamari intensity variable LED 1[[5](#footnote_05)].

New `oic-and-http-light-server.fbp` after changes:

```sh
DECLARE=http-light-server:fbp:http-server.fbp
DECLARE=persistence-light-server:fbp:persistence.fbp

oic_server(oic/server-light)
http_server(http-light-server)

persistence(persistence-light-server:default_state=true,default_power=100)

http_server POWER -> POWER persistence
http_server STATE -> STATE persistence

persistence POWER -> POWER http_server
persistence STATE -> STATE http_server

oic_server POWER -> POWER http_server
oic_server STATE -> STATE http_server

http_server POWER -> POWER oic_server
http_server STATE -> STATE oic_server

# show machine-id as device-id
_(platform/machine-id) OUT -> IN _(console:prefix="OIC device-id: ")

http_server POWER -> IN power_actuator(PowerActuator)
http_server STATE -> IN state_actuator(StateActuator)
```

Default `sol-flow.json`:

```json
{
    "$schema": "http://solettaproject.github.io/soletta/schemas/config.schema",
        "nodetypes": [
        {
            "name": "StateActuator",
            "options": {
                "prefix": "state: "
            },
            "type": "console"
        },
        {
            "name": "PowerActuator",
            "options": {
                "prefix": "power: "
            },
            "type": "console"
        },
        {
            "name": "QuitButton",
            "options": {
                "binary_code": 113
            },
            "type": "keyboard/boolean"
        },
        {
            "name": "IncreaseButton",
            "options": {
                "binary_code": 106
            },
            "type": "keyboard/boolean"
        },
        {
            "name": "DecreaseButton",
            "options": {
                "binary_code": 107
            },
            "type": "keyboard/boolean"
        },
        {
            "name": "StateButton",
            "options": {
                "binary_code": 97
            },
            "type": "keyboard/boolean"
        },
        {
            "name": "StartupMessage",
            "options": {
                "value": "Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit."
            },
            "type": "constant/string"
        }
    ]
}
```

And `sol-flow-intel-minnow-max-linux_gt_3_17.json`:

```json
{
    "$schema": "http://solettaproject.github.io/soletta/schemas/config.schema",
    "nodetypes": [
        {
            "name": "StateActuator",
            "options": {
                "pin": "339",
                "raw": true
            },
            "type": "gpio/writer"
        },
        {
            "name": "PowerActuator",
            "options": {
                "address": 1,
                "period": 10000,
                "range": "min:0|max:10000|step:1"
            },
            "type": "calamari/led"
        },
        {
            "name": "QuitButton",
            "options": {
                "binary_code": 113
            },
            "type": "keyboard/boolean"
        },
        {
            "name": "IncreaseButton",
            "options": {
                "active_low": true,
                "edge_falling": true,
                "edge_rising": true,
                "pin": 472,
                "pull": "up",
                "raw": true
            },
            "type": "gpio/reader"
        },
        {
            "name": "DecreaseButton",
            "options": {
                "active_low": true,
                "edge_falling": true,
                "edge_rising": true,
                "pin": 482,
                "pull": "up",
                "raw": true
            },
            "type": "gpio/reader"
        },
        {
            "name": "StateButton",
            "options": {
                "active_low": true,
                "edge_falling": true,
                "edge_rising": true,
                "pin": 483,
                "pull": "up",
                "raw": true
            },
            "type": "gpio/reader"
        },
        {
            "name": "StartupMessage",
            "options": {
                "value": "This configuration uses Minnowboard and Calamari Lure. Press Button-3 to change state, Button-1 to increase power and Button-2 to decrease it, 'q' (keyboard) to quit."
            },
            "type": "constant/string"
        }
    ]
}
```

## Conclusion

This finishes this tutorial. Now you should be familiar with Soletta:

  * Configuration files
  * Flow Based Programming
    * Some node types
    * Converting packet types
    * Handling error packets
  * HTTP client and server
  * OIC client and server
  * Persistence to file storage
  * Using command line arguments
  * Quitting application

You can get more information about Soletta on its
[wiki](https://github.com/solettaproject/soletta/wiki),
[Readme](https://github.com/solettaproject/soletta#soletta-project)
and [Node types cheat sheet](https://github.com/solettaproject/soletta#soletta-project).

<a name="footnote_05"> [5] This mapping isn't perfect, as `PowerActuator` uses a port
`INTENSITY` instead of `IN`. See `TODO` on
"src/samples/flow/oic-and-http-light/oic-and-http-light-server.fbp".
