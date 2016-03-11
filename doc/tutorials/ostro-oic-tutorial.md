# Overview

In this tutorial, we'll show how to develop a simple application using
Soletta. Although simple, this application will show some important
features of Soletta, like OIC and HTTP communication, besides GPIO
interaction.
Initially, we are going to run the application on desktop [1], but
later we're going to show how to run it on devices, such as [MinnowBoard
MAX](http://wiki.minnowboard.org/MinnowBoard_Wiki_Home).

## Step 0

Soletta must be installed on developer's computer. One can use a package
provide for his or her distro, or [build Soletta](https://github.com/solettaproject/soletta/wiki/How-to-start).

## Step 1

### Flow Based Programming overview

Soletta provides two API [3] for developers. A C API and a flow based
API. In this tutorial, we're going to show the flow based API.
The flow based API uses [Flow Based Programming](https://en.wikipedia.org/wiki/Flow-based_programming)
concepts. It is a higher level API than its C counterpart, and it's
much easier to use when expressing relationships among application
components.
Basically, a flow based application contains 'nodes' which represents
entities - like sensors or actuators, as well as operations like
concatenating string. These nodes have input or output 'ports' that
send or receive 'packets' of information. These ports can be connected
to express the flow of information among components. For instance:

```
button(gpio/reader:pin=1) OUT -> IN light(gpio/writer:pin=2)
```

Here, `button` is a node of node type [4] `gpio/reader` and light is
another node, of node type `gpio/writer`. A `gpio/reader` node
has an output port `OUT` - which sends boolean packets - that
is connected to input port `IN` of the `gpio/writer` - which
in turn, receives boolean packets as well. So, in this sample,
we use `button` output to feed `light`. The `pin=N` on those
nodes is a 'node option'. Node options are used to setup nodes,
so each node, in this sample, is aware of which GPIO pin they
represent.
Note that subsequent uses of a node don't take its setup information
again, so, if we wanted to debug `button` output to console,
we would do:

```
button OUT -> IN _(console)
```

For more information about Soletta flow programming approach,
you can see [Soletta README](https://github.com/solettaproject/soletta#flow-based-programming).
For more information about Soletta available node types,
see Soletta node type [Cheat Sheet](http://solettaproject.github.io/docs/nodetypes/).

### Writing a **really** simple application

The first step to write an application is to define what it shall do.
Let's imagine that we want to control a device that has two features:
'state' and 'power'. One can think of a light, for instance. It's either
'ON' or 'OFF' - the state, and has different light intensities - the power.
We can use two accumulators for this task: one from 0 to 1, to store
'state', and another from 0 to 100 to store 'power'.

```
power_value(int/accumulator:setup_value=min:0|max:100|step:1,initial_value=50)
state_value_as_int(int/accumulator:setup_value=min:0|max:1)
```

As `int/accumulator` node type deals with integers, and 'state' is more
naturally represented as boolean, we will need some 'converters' to change
from/to integer/boolean:

```
state_value_from_boolean(converter/boolean-to-int) OUT -> SET state_value_as_int
state_value_as_int OUT -> IN state_value(converter/int-to-boolean)
```

The first converter, `state_value_from_boolean`, will receive boolean
packets and converter to integer and deliver the result to
`state_value_as_int`. The second one does the opposite: it receives the
output from `state_value_as_int` and converts to boolean.

### Running the application

If we write those to a file named 'controller.fbp', we can run it by:

```
$ sol-fbp-runner controller.fbp
```

The result is that nothing will appear. As Soletta applications are
intended to run on devices, they should run forever. And as our
application has no input or output, it will just block our terminal.
You can finish it with ^C (Ctrl+C).

## Step 2

### Seeing initial values

So far, our application was not very useful. It did nothing! Now, let's
add some interaction, so we can see if 'state' and 'power' behave as
expected.
To debug it, we can always send nodes output to `console` node type:

```
power_value OUT -> IN power_value_output(console)
state_value OUT -> IN state_value_output(console)
```

If we run this updated application, we shall see:

```
power_value_output 50 (integer range)
state_value_output false (boolean)
```

This is so because many nodes, when 'are born', send they initial value
through its ports, so we basically see they initial values.

### Changing some states

Now, let's start interacting with our app. We'll use three buttons,
one to toggle 'state' value, one to increase 'power' and another to
decrease it. At this first moment, we'll use desktop keyboard buttons.

```
increase_button(keyboard/boolean:binary_code=106)
decrease_button(keyboard/boolean:binary_code=107)
state_button(keyboard/boolean:binary_code=97)
```

Those binary codes correspond to 'j', 'k' and 'a', respectively.
It's time to connect them to their accumulators. As buttons outputs
a `true` packet when pressed and `false` when released, we need to
filter its output: only when they are pressed matters.
We achieve this with node type `boolean/filter` - connect only its
TRUE output port to get only true packets.

```
increase_button OUT -> IN _(boolean/filter) TRUE -> INC power_value
decrease_button OUT -> IN _(boolean/filter) TRUE -> DEC power_value
state_button OUT -> IN _(boolean/filter) TRUE -> INC state_value_as_int
```

Run application again, and use 'j', 'k' and 'a' buttons on keyboard to
interact with application. You shall see something like:

```
power_value_output 50 (integer range)
state_value_output false (boolean)
state_value_output true (boolean)
state_value_output false (boolean)
state_value_output true (boolean)
power_value_output 51 (integer range)
power_value_output 52 (integer range)
power_value_output 53 (integer range)
power_value_output 52 (integer range)
power_value_output 51 (integer range)
power_value_output 50 (integer range)
power_value_output 49 (integer range)
power_value_output 48 (integer range)
```

Naturally, the exact output depends on which buttons you pressed.
As a bonus, it's really easy to add 'q' key to quit application:

```
quit_button(keyboard/boolean:binary_code=113)
quit_button OUT -> QUIT _(app/quit)
```

Now, we don't need ^C to quit our app.

## Step 3

### Saving values and splitting .fbp files

Right now, after execution, node values are lost. But it doesn't have to
be like this: thanks to Soletta persistence nodes, we can persist their
values and restore on subsequent executions.
As our fbp files is growing, we'll do something different now: instead
of just adding more code to it, we'll deal with persistence on a
different .fbp file, thus demonstrating 'subflows'.
Subflows are flows wrapped on different files that define themselves a
set of options, input and output ports, just like a node. In fact,
a subflow is used as another node on its 'caller'.
First, lets define our subflow as follows:

```
INPORT=power.IN:IN_POWER
INPORT=state.IN:IN_STATE

OUTPORT=power.OUT:OUT_POWER
OUTPORT=state.OUT:OUT_STATE

OPTION=power.default_value:default_power
OPTION=state.default_value:default_state

power(persistence/int:name="power",storage=fs)
state(persistence/boolean:name="state",storage=fs)
```

`INPORT` and `OUTPORT` statements maps which ports of which nodes
will be exported from this subflow. In this case, we export ports
`IN` of `power` and `state` nodes as `IN_POWER` and `IN_STATE`,
respectively. We do the same for output ports: `OUT` of `power` and
`state` are exported as `OUT_POWER` and `OUT_STATE`, respectively.
Following `OPTION` statements on our sample export node `power` and
`state` option `default_value` as `default_power` and `default_state`.
Persistence nodes can store their values on different media. Option
`storage=fs` states that they will use `filesystem` as storage media.
Save this file as `persistence-light-server.fbp`. Why server? We'll see
soon =D
On our `controller.fbp` file, we declare the subflow as follows:

```
DECLARE=persistence-light-server:fbp:persistence-light-server.fbp
```

So, from now on, on `controller.fbp` file, we have access to
`persistence-light-server` subflow. We use it like a normal node, as
follows:

```
persistence(persistence-light-server:default_state=true,default_power=100)
```

We then connect this persistence node to our accumulators:

```
state_value OUT -> IN_STATE persistence
power_value OUT -> IN_POWER persistence

persistence OUT_STATE -> IN state_value_from_boolean
persistence OUT_POWER -> SET power_value
```

We 'cross linked' them, so any update to them is reflected on the other.
It is important to note that, as accumulators send a packet once they
'are born', just running the application would result on an infinite loop
between accumulator and persistence nodes. To avoid that, we define
that our accumulators do not send initial packet, by adding the option
`send_initial_packet=false`. So their declarations become:

```
power_value(int/accumulator:setup_value=min:0|max:100|step:1,initial_value=50,send_initial_packet=false)
state_value_as_int(int/accumulator:setup_value=min:0|max:1,send_initial_packet=false)
```

If you run the application now, you'll see that it now keeps 'state' and
'power' values between executions. As we are using 'filesystem'
persistence, you may notice two new files on the dir you are running
application: 'state' and 'power', exact the same name given to
persistence nodes on our subflow.

## Step 4

### Controlling a remote light using OIC

So far, we stayed on only one one device. But what if we want to, say,
control a remote light? Soletta also supports [OIC](http://openinterconnect.org/)
and has a 'light client/server' sample. We are going to use the
controller we built so far to control this remote light.
We are going to use our controller, but let's modify it to be a subflow
as well. So we can use it as a generic controller.
We start by exposing its relevant ports:

```
OUTPORT=power_value.OUT:POWER
OUTPORT=state_value.OUT:STATE

INPORT=power_value.SET:POWER
INPORT=state_value_from_boolean.IN:STATE
```

Now, our controller subflow will simply have `POWER` and `STATE`
ports in both directions (`IN` and `OUT`).
**Note:** As it doesn't make sense for a client controller store data, we
should also remove persistence related nodes. We will move persistence
stuff to our fbp servers later on.
**Note:** It's also a good idea to change controller lines that output
status to something more meaningful, like:
print content,

```
power_value OUT -> IN power_value_output(console:prefix="Power value changed: ")
state_value OUT -> IN state_value_output(console:prefix="State changed: ")
```

so they are easily identified on output.

On a new file, `oic-controller.fbp` for instance, declare our controller
subflow (and create it):

```
DECLARE=controller:fbp:controller.fbp

# create a controller
controller(controller)
```

OIC resources, like Soletta light-server sample, have an ID to uniquely
identify them. As our client is going to connect to a specific resource,
we need to define its ID somehow. We are going to get them from command
line argument used to run our application. This can be done with help
of node `app/argc-argv`:

```
cmdline_arguments(app/argc-argv)
_(constant/int:value=1) OUT -> IN cmdline_arguments
```

With this, when we get output value from `cmdline_arguments` node, it
will output command line argument number 1.
Then, we use Soletta `oic/client-light` [5] to communicate with a light
server. We also cross link it our controller, so changes on one are
properly reflected on another - note however, that at this point, changes
only happen in the direction 'controller -> oic/client', but we'll
improve this later.

```
oic(oic/client-light)

# Uses command line argument as device id
cmdline_arguments OUT -> DEVICE_ID oic

# Cross link OIC and controller
oic OUT_STATE -> STATE controller
oic OUT_POWER -> POWER controller

controller STATE -> IN_STATE oic
controller POWER -> IN_POWER oic
```

Let's not forget to show user about server status:

```
oic OUT_STATE -> IN _(console:prefix="Server state is: ")
oic OUT_POWER -> IN _(console:prefix="Server power level is: ")
```

### Showing a nice error message to user

If we run our `controller-oic.fbp` as is, it will end up showing an error
message like:

```
$ sol-fbp-runner controller-oic.fbp

WRN:sol-flow ./src/lib/flow/sol-flow-static.c:341 flow_send_do() Error packet '22 (Argument position (1) is greater than arguments length (1))' sent from 'controller-oic.fbp (0x560d1c548e90)' was not handled
```

This is so because we didn't pass the device id as argument. But how a
user would know? Every Soletta node type as an implicit `ERROR` port,
where error packets are sent. The idea will be capture an ERROR packet
sent by `cmdline_arguments` and then print a more meaningful message to
user:

```
cmdline_arguments ERROR -> IN cmdline_usage(converter/empty-to-string:output_value="Missing device ID in the command line. Example: 2f3089a1dbfb43d38cab64383bdf9380") OUT -> IN _(console:prefix="ERROR: ")
cmdline_usage OUT -> IN _(converter/empty-to-int:output_value=1) OUT -> CODE _(app/quit)
```

See that we used an `app/quit` node type to quit application with an
error code, in this case, 1.

Now, if we run without the expected argument, we get a better error
message:

```
$ sol-fbp-runner controller-oic.fbp

ERROR: Missing device ID in the command line. Example: 2f3089a1dbfb43d38cab64383bdf9380 (string)
```

### Running the server

To really test our OIC controller, we need to run Soletta
`light-server.fbp` sample, using the device id we are going to pass
to the application. To do so, you can run, on Soletta dir:

```
SOL_MACHINE_ID=2f3089a1dbfb43d38cab64383bdf9380 sol-fbp-runner src/samples/flow/oic/light-server.fbp
```

And them, run our app:

```
$ sol-fbp-runner controller-oic.fbp 2f3089a1dbfb43d38cab64383bdf9380
Server state is: false (boolean)
Server power level is: 76 (integer range)
Power value changed: 76 (integer range)
State changed: true (boolean)
Power value changed: 77 (integer range)
```

**Note:** You may see some warnings related to OIC as well, as it tries
to get information on all your network interfaces and fail in some
of them. They are harmless.

## Step 5

### Defining our own HTTP server

If instead of connect to an OIC server, we connect to our own HTTP light
server? Yeah, it can be done!
We will first write our server and them a controller for it.
Again, we'll use subflows for this, so we keep this code separated.
Soletta has node types to HTTP servers, we are going to use two of them:
`http-server/int` and `http-server/boolean`, to provide our 'power' and
'state' values. On a `http-light-server.fbp` write:

```
power(http-server/int:path="/power",port=8002)
state(http-server/boolean:path="/state",port=8002)

_(constant/string:value="http-light-server running on port 8002, paths: /state and /power") OUT -> IN _(console:prefix="note: ")
```

First two lines create two HTTP servers, one for 'power' and another for
'state'. You can see that they have a `path` and a network `port`. Do not
confuse this port with flow ports, like `IN` or `OUT`.
The third one is just to print an informative note.
As with any subflow, we need to export it's input, output and options.
For this subflow, it suffice to export 'power' and 'state' ports, like:

```
INPORT=power.IN:IN_POWER
INPORT=state.IN:IN_STATE

OUTPORT=power.OUT:OUT_POWER
OUTPORT=state.OUT:OUT_STATE
```

If you run this file, you can even access current state on browser, by
typing `http://localhost:8002/power`, for instance.

### An HTTP controller to communicate with our HTTP server

Now let's controll our HTTP server. Our OIC controller connected
specifically to an OIC server, so it can't be reused. But we were smart
and separated our generic controller, the one that takes user inputs.
Let's reuse it! First step, as always, is to declare and use it, on
a file named `http-controller.fbp`:

```
DECLARE=controller:fbp:controller.fbp

# create a controller
controller(controller)
```

We will also let our user inform, via command line arguments, the path
of our server. And also show them a nice message if they miss anything:

```
cmdline_arguments(app/argc-argv)
cmdline_arguments ERROR -> IN cmdline_usage(converter/empty-to-string:output_value="Missing URL in the command line. Example: http://localhost:8002") OUT -> IN _(console:prefix="ERROR: ")
cmdline_usage OUT -> IN _(converter/empty-to-int:output_value=1) OUT -> CODE _(app/quit)

# We are going to assume that first command line argument is our server path
_(constant/int:value=1) OUT -> IN cmdline_arguments
```

From the path we got, we need to build full URL for each resource,
basically, add '/state' and '/power' to path:

```
state_url(string/concatenate:separator="/")
cmdline_arguments OUT -> IN[0] state_url
_(constant/string:value="state") OUT -> IN[1] state_url

power_url(string/concatenate:separator="/")
cmdline_arguments OUT -> IN[0] power_url
_(constant/string:value="power") OUT -> IN[1] power_url
```

We used `string/concatenate` node types. Note how it has indexed input,
so its port `IN` can be indexed.
Different from OIC, an HTTP server will not inform us about change on its
state, so we have to keep polling to get its current status. We'll do so
with aid of a timer:

```
trigger_get(timer:interval=1000)
```

This timer node will send an empty pack each 1000 milliseconds, se we can
ask HTTP server repeatedly about its status.
An `http-client` node type will be used to ask HTTP server:

```
http_power(http-client/int)
power_url OUT -> URL http_power
http_power ERROR -> IN _(converter/empty-to-string:output_value="Error communicating with HTTP server at /power. Check if the server is running.") OUT -> IN note
```

This code declares the client node, sets its URL - using the output of
corresponding concatenate node. We also capture its error packet, so we
can inform user about errors. The `note` node is just a `console`
node with a defined prefix:

```
note(console:prefix="note: ")
```

Now we use our timer node, `trigger_get`, to perform a request for HTTP
server current power each second:

```
trigger_get OUT -> GET http_power
http_power OUT -> POWER controller
http_power OUT -> IN _(console:prefix="Server power level is: ")
```

'Power' request response is then set to our `controller` node, as well as
informed to user.

Besides getting information about server status, we will also update
our HTTP server:

```
# Notify server of power level change
controller POWER -> POST http_power
```

Then, we repeat the same to get and set 'state':

```
# define http client for 'state' property
http_state(http-client/boolean)
http_state ERROR -> IN _(converter/empty-to-string:output_value="Error communicating with HTTP server at /state. Check if the server is running.") OUT -> IN note
state_url OUT -> URL http_state

# load state from server
trigger_get OUT -> GET http_state
http_state OUT -> STATE controller
http_state OUT -> IN _(console:prefix="Server state is: ")

# notify server of state change
controller STATE -> POST http_state
```

Now we can run our server in one terminal:

```
$ sol-fbp-runner http-light-server.fbp
```

Our client controller on another:

```
$ sol-fbp-runner http-controller.fbp http://localhost:8002
```

And interact with it, getting something like:

```
Server power level is: 3 (integer range)
Server state is: true (boolean)
Server power level is: 3 (integer range)
Server state is: true (boolean)
Server power level is: 4 (integer range)
Server state is: true (boolean)
Server power level is: 5 (integer range)
Server state is: true (boolean)
Server power level is: 5 (integer range)
Server state is: true (boolean)
Server power level is: 5 (integer range)
Server state is: false (boolean)
Server power level is: 5 (integer range)
Server state is: true (boolean)
```

## Step 6

### What about joining our OIC and HTTP server together?

So far, we had two different servers and two controllers. Let's now group
our servers together, so we can have a more realist application: one that
exposes two interfaces, HTTP and OIC, and also persists current state.
We already have all pieces we need: OIC server (Soletta sample one), our
HTTP server, an HTTP controller, an OIC controller and our persistence
subflow. Following diagram shows our idea:

```
       +---------------------+                           +-----------------------+
       |                     |                           |                       |
   +---+ http-controller.fbp +----+                      | http-light-server.fbp |
   |   |                     |    |                      |                       |
   |   +---------------------+    |                      +-----------+-----------+
   |                              |                                  ^
   |[uses]                        |                                  |[uses]
   |                              |                                  |
   V                              |                 +-----------------+-------------+
+--+-------------+                |                 |                               |
|                |                | [communicates]  | oic-and-http-light-server.fbp |
| Controller.fbp |                +---------------->+                               |
|                |                |                 |        [oic/server-light]     |
+--+-------------+                |                 |                               |
   ^                              |                 +-----------------+-------------+
   |                              |                                   |
   |[uses]                        |                                   |[uses]
   |                              |                                   V
   |                              |                   +---------------+--------------+
   |    +--------------------+    |                   |                              |
   |    |                    |    |                   | persistence-light-server.fbp |
   +----+ oic-controller.fbp +----+                   |                              |
        |                    |                        +------------------------------+
        +--------------------+
```

Here, we show the relationships among our subflows. We have basically
everything but `oic-and-http-light-server.fbp` - which has a node
`oic/server-light` itself. So, this fbp will be our server - it will
expose both an HTTP and an OIC server. Let's write it!
First, we declare the subflows we want to use:

```
DECLARE=http-light-server:fbp:http-light-server.fbp
DECLARE=persistence-light-server:fbp:persistence-light-server.fbp
```

Then, declare our HTTP and OIC servers, as well as our persistence node:

```
oic_server(oic/server-light)
http_server(http-light-server)

persistence(persistence-light-server:default_state=true,default_power=100)
```

Note that declaring the servers doesn't block execution. We already saw
that with our HTTP server. Soletta runs them on its mainloop and will
serve requests appropriately.
Then we cross link our HTTP and OIC servers, so changes in one are
applied to the other:

```
oic_server OUT_POWER -> IN_POWER http_server
oic_server OUT_STATE -> IN_STATE http_server

http_server OUT_POWER -> IN_POWER oic_server
http_server OUT_STATE -> IN_STATE oic_server
```

And OIC to persistence, so it's updated as well:

```
oic_server OUT_POWER -> IN_POWER persistence
oic_server OUT_STATE -> IN_STATE persistence

persistence OUT_POWER -> IN_POWER oic_server
persistence OUT_STATE -> IN_STATE oic_server
```

When our server starts, lets show its device id:

```
_(platform/machine-id) OUT -> IN _(console:prefix="OIC device-id: ")
```

Yes, OIC device id is the platform machine ID. For tests, one can
force Soletta to use a given ID by setting `SOL_MACHINE_ID`
environment variable.
Finally, let's show user server state:

```
oic_server OUT_POWER -> IN _(console)
oic_server OUT_STATE -> IN _(console)
```

### Running our application

Time to run our application! On three terminals, open the server:

```
$ sol-fbp-runner oic-and-http-light-server.fbp
OIC device-id: 0bacbaedb34b434785dec5519d60101c (string)
note: http-light-server running on port 8002, paths: /state and /power (string)
```

Using the device-id shown here, start our OIC controller:

```
$ sol-fbp-runner oic-controller.fbp 0bacbaedb34b434785dec5519d60101c
```

And finally, our HTTP controller:

```
$ sol-fbp-runner http-controller.fbp http://localhost:8002
```

And see how issuing commands to any of the controller properly updates
our server and both controllers. You may also notice that when updating
via OIC controller, it will just show that it is changing, but not new
server value:

```
State changed: true (boolean)
State changed: false (boolean)
State changed: true (boolean)
```

As opposed to:

```
Server state is: true (boolean)
Server power level is: 79 (integer range)
State changed: true (boolean)
Server state is: false (boolean)
Server power level is: 79 (integer range)
State changed: false (boolean)
Server state is: true (boolean)
Server power level is: 79 (integer range)
State changed: true (boolean)
```

When updating from another controller. This happens because OIC client
will only send output packets when new values coming from OIC server
are different of client current ones. Or, to put it in another way, it
only reports changes that were made elsewhere, not the ones that the
client itself made.

## Step 7

### Running on a device - MinnowBoard MAX

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

### Soletta configuration files

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

```
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
}
```

Then, in `controller.fbp` we use these new node types:

```
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

```
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
}
```

Note that we use `gpio/reader` node type to read Calamari button state.
Now that we have different ways of interacting with our application, it
is nice to tell the user how, depending on platform they are using it.
Again, conf files are the solution! You can create another entry to
provide this information on our application configuration files. On
sol-flow.json (the fallback one, that is loaded on desktop) add:

```
{
    "name": "StartupMessage",
        "options": {
            "value": "Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit."
        },
        "type": "constant/string"
}
```

And on `sol-flow-intel-minnow-max-linux_gt_3_17.json`:

```
{
    "name": "StartupMessage",
        "options": {
            "value": "This configuration uses Minnowboard and Calamari Lure. Press Button-3 to change state, Button-1 to increase power and Button-2 to decrease it, 'q' (keyboard) to quit."
        },
        "type": "constant/string"
}
```

Finally, on `controller.fbp` add:

```
_(StartupMessage) OUT -> IN _(console:prefix="Hint: ")
```

Now, when running any controller, one will get a startup message:

```
$ sol-fbp-runner http-controller.fbphttp://localhost:8002
Hint: Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit. (string)
```

### Running on MinnowBoard MAX + Calamari Lure

Make sure that your MinnowBoard MAX has access to network. Running any
controller (OIC or HTTP) on it should be just a matter of sending
corresponding fbp file, `oic-controller.fbp` or `http-controller.fbp`,
`controller.fbp` as it's used by both and
`sol-flow-intel-minnow-max-linux_gt_3_17.json`, naturally.
If you want to run the servers, then you'll need:

```
http-light-server.fbp
persistence-light-server.fbp
oic-and-http-light-server.fbp
```

For instance, if running HTTP controller on MinnowBoard MAX and
server on desktop, you could get something similar to:

```
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

Remember to use correct address when running controller

### Step 8 - Defining real actuators

Our application is mostly done. However it doesn't change anything -
our server can keep state and power values, but how to apply them
to the 'real world'? It's just a matter of connecting this values
to nodes that represent real actuators.
To demonstrate it, let's connect `OUT_POWER` and `OUT_STATE` ports
of  `oic_server` to actuators instead of `console`:

```
oic_server OUT_POWER -> IN power_actuator(PowerActuator)
oic_server OUT_STATE -> IN state_actuator(StateActuator)
```

And what are `PowerActuator` and `StateActuator`? Node types defined
on configuration files. For desktop, `sol-flow.json` have:

```
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

```
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
controls Calamari intensity variable LED 1[6].
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

[1] On this tutorial, 'desktop' refers to the machine a developer
is using to develop application. In opposition, 'device' is used
to refer to a small low-end device, like a MinnowBoard MAX [2].

[2] We're aware that MinnowBoard MAX is not so 'small low-end' =D

[3] Application programming interface

[4] 'Node type' refers to a description of a node, its 'type'.
Only 'node' refers to an instance of a node type, akin to 'class'
and 'object' on Object-Oriented Programming.

[5] This node type is also a Soletta sample, to connect to
`oic/server-light` node type.

[6] This mapping isn't perfect, as `PowerActuator` uses a port
`INTENSITY` instead of `IN`. See `TODO` on
"src/samples/flow/oic-and-http-light/oic-and-http-light-server.fbp".
