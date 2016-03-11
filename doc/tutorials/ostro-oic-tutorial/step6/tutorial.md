# Step 6 - Running on a real device

## Running on MinnowBoard MAX

Until now, we've run our application on desktop. It's time to run it
on real devices. We're gonna use MinnowBoard MAX.
We could again build Soletta for it, but we're gonna use an [Ostro](https://ostroproject.org/)
image that can be obtained from https://download.ostroproject.org/releases/.
We will also use a [Calamari Lure](http://wiki.minnowboard.org/Calamari_Lure).
Before really using our board, we have to do some changes on our
application. When developing, instead of using, for instance, GPIO or
PWM, it makes sense mock these details and use keyboard or Soletta GTK
nodes. To abstract away these details, Soletta provides configuration
files, where one can define node types. A developer may provide
various configuration files, one for each type of device his or her
application will run on. Flow is the same, and these node details live
on these configuration files.

## Soletta configuration files

Soletta default configuration file is named `sol-flow.json`. When
`sol-fbp-runner` runs, it looks for a `sol-flow.json` file to load
flow configuration. If running on a particular platform, it will look
first on expected configuration file for that platform, thus it looks
for a `sol-flow-intel-minnow-max-linux_gt_3_17.json` on a MinnowBoard MAX
**before** looking for a `sol-flow.json` file. This behaviour eases the
task of maintaining different configuration files for different
platforms: just have them all with their respective names and Soletta
shall pick the right one on a given platform.
In our example, we could define the keys we use to interaction on a
configuration file:

```json
{
    "$schema": "http://solettaproject.github.io/soletta/schemas/config.schema",
        "nodetypes": [
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
        }
    ]
}
```

Then, in `controller.fbp` we use these new node types:

```sh
increase_button(IncreaseButton)
decrease_button(DecreaseButton)
state_button(StateButton)
quit_button(QuitButton)
```

Suppose that on MinnowBoard MAX we want to use the three Calamari Lure
buttons as 'increase', 'decrease' and 'state' buttons - and keep 'quit'
as 'q' key on keyboard, since we don't have four buttons on Calamari.
It would be just a matter of creating a
`sol-flow-intel-minnow-max-linux_gt_3_17.json` conf file and adding:

```json
{
    "$schema": "http://solettaproject.github.io/soletta/schemas/config.schema",
        "nodetypes": [
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
        }
    ]
}
```

Note that we use `gpio/reader` node type to read Calamari button state.
Now that we have different ways of interacting with our application, it
is nice to tell the user how, depending on platform they are using it.
We already provide a hint, but how to provide different hints for
different platforms?
Again, conf files are the solution! You can create another entry to
provide this information on our application configuration files. On
sol-flow.json (the fallback one, that is loaded on desktop) add:
**Note:** As Calamari has only three buttons, we left quit button still
on keyboard 'q'.

```json
{
    "name": "StartupMessage",
        "options": {
            "value": "Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit."
        },
        "type": "constant/string"
}
```

And on `sol-flow-intel-minnow-max-linux_gt_3_17.json`:

```json
{
    "name": "StartupMessage",
        "options": {
            "value": "This configuration uses Minnowboard and Calamari Lure. Press Button-3 to change state, Button-1 to increase power and Button-2 to decrease it, 'q' (keyboard) to quit."
        },
        "type": "constant/string"
}
```

Finally, on `controller.fbp` change our hint to:

```sh
_(StartupMessage) OUT -> IN _(console:prefix="Hint: ")
```

### Running on MinnowBoard MAX + Calamari Lure

Make sure that your MinnowBoard MAX has access to network. Running any
controller (OIC or HTTP) on it should be just a matter of sending
corresponding fbp file, `oic-controller.fbp` or `http-controller.fbp`,
`controller.fbp` as it's used by both and
`sol-flow-intel-minnow-max-linux_gt_3_17.json`, naturally.
If you want to run the servers, then you'll need:

```sh
http-light-server.fbp
persistence-light-server.fbp
oic-and-http-light-server.fbp
```

For instance, if running HTTP controller on MinnowBoard MAX and
server on desktop, you could get something similar to:

```sh
$ sol-fbp-runner http-controller.fbp http://192.168.0.101:8002
Hint: This configuration uses Minnowboard and Calamari Lure. Press Button-3 to change state, Button-1 to increase power and Button-2 to decrease it, 'q' (keyboard) to quit. (string)
Server state is: false (boolean)
Server power level is: 71 (integer range)
Power value changed: 71 (integer range)
Server power level is: 71 (integer range)
Server state is: false (boolean)
Power value changed: 72 (integer range)
Server power level is: 72 (integer range)
Server state is: false (boolean)
```

Remember to use correct address when running controller.

Our final `controller.fbp` should look like:

```sh
OUTPORT=power_value.OUT:POWER
OUTPORT=state_value.OUT:STATE

INPORT=power_value.SET:POWER
INPORT=state_value_from_boolean.IN:STATE

increase_button(IncreaseButton)
decrease_button(DecreaseButton)
state_button(StateButton)
quit_button(QuitButton)

_(constant/string:value="Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit.") OUT -> IN _(console:prefix="Hint: ")

quit_button OUT -> QUIT _(app/quit)

power_value(int/accumulator:setup_value=min:0|max:100|step:1,initial_value=50,send_initial_packet=false)
state_value_as_int(int/accumulator:setup_value=min:0|max:1|step:1,send_initial_packet=false)

increase_button OUT -> IN _(boolean/filter) TRUE -> INC power_value
decrease_button OUT -> IN _(boolean/filter) TRUE -> DEC power_value

power_value OUT -> IN _(console:prefix="Power value changed: ")

state_button OUT -> IN _(boolean/filter) TRUE -> INC state_value_as_int

state_value_from_boolean(converter/boolean-to-int)
state_value(converter/int-to-boolean)

# offer an entry point in the correct type (INPORT=...)
state_value_from_boolean OUT -> SET state_value_as_int

# provide final value with the correct type (OUTPORT=...)
state_value_as_int OUT -> IN state_value

state_value OUT -> IN _(console:prefix="State changed: ")
```

Default `sol-flow.json`:

```json
{
    "$schema": "http://solettaproject.github.io/soletta/schemas/config.schema",
        "nodetypes": [
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

And MinnowBoard MAX `sol-flow-intel-minnow-max-linux_gt_3_17.json`:

```json
{
    "$schema": "http://solettaproject.github.io/soletta/schemas/config.schema",
        "nodetypes": [
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

So far, so good. However our application didn't really change anything
on real world.

[Next](../step7/tutorial.md) step, we'll deal with real actuators.
