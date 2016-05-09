# An OIC controller

## Interacting with user

Last step was really easy, but we didn't test it to see if it works.
Now, let's build a simple client controller to do so.
We'll make our controller as a simple three button application: one
button increases current power, another decreases it and last one toggle
state on and off. On our desktop, we can use normal keyboard buttons
to do so. Soletta provides us access to them via `keyboard` node types.
We can start by declaring them, like:

```sh
increase_button(keyboard/boolean:binary_code=106)
decrease_button(keyboard/boolean:binary_code=107)
state_button(keyboard/boolean:binary_code=97)
```

We use `keyboard/boolean` node type which simply outputs `true` when
key is pressed and `false` when released. Option `binary_code` identifies
which key we are interested in. In this code, increase is going to be
`j`, decrease `k` and state `a`. Actually, to help us and our users,
let's print this information when our controller starts:

```sh
_(constant/string:value="Press 'a' to change state, 'j' to increase power and 'k' to decrease it") OUT -> IN _(console:prefix="Hint: ")
```

Our server receives as input new 'power' our 'state' values, but our
client, limited to three buttons, works by increasing or decreasing
current 'power' value and toggling current 'state' value. In order to do
this, we'll use an `int/accumulator` node type. This node basically
provides all we need: it allows increase or decrease current values,
limited to a specific range. Let's use it:

```sh
power_value(int/accumulator:setup_value=min:0|max:100|step:1,send_initial_packet=false)
```

Option `setup_value` defines accumulator minimum and maximum values and
step: by how much it is increased or decreased. Option
`send_initial_packet` is very important: some nodes send to their output
ports an initial value when application starts. This is usually useful,
but for our application it would be annoying. We want to retrieve our
initial value from server, not send an initial value to server. Thus,
we disable this behaviour by setting this option to `false`.
Time to connect buttons to accumulators:

```sh
increase_button OUT -> IN _(boolean/filter) TRUE -> INC power_value
decrease_button OUT -> IN _(boolean/filter) TRUE -> DEC power_value
```

As buttons outputs `true` packets when pressed and `false` when released,
we need to filter its output: only when they are pressed matters.
We achieve this with node type `boolean/filter` - connect only its
TRUE output port to get only true packets.
Ports `INC` and `DEC` increases and decreases accumulator current value,
respectively.
Print some debug information when change value:

```sh
power_value OUT -> IN _(console:prefix="Power value changed: ")
```

## Receiving command line arguments and connecting to OIC server

It is time to connect to OIC server. Node type `oic/client-light` will
help is with such task. But it needs to know where connect. Different
from HTTP, it doesn't need a network address, but an ID, something
on the form '2f3089a1dbfb43d38cab64383bdf9380'. When running our server,
it informs us its ID:

```sh
$ sol-fbp-runner oic-and-http-light-server.fbp
OIC device-id: 0bacbaedb34b434785dec5519d60101c (string)
note: http-light-server running on port 8002, paths: /state and /power (string)
```

But how to inform it to our client controller? We could hardcode this
information for tests, but on real world, this is unpractical - even for
tests. Soletta inform us about command line arguments, so let's use them:

```sh
cmdline_arguments(app/argc-argv)
_(constant/int:value=1) OUT -> IN cmdline_arguments
```

Node type `app/argc-argv` is the one we use to get this information.
Second line states that we want first argument. Now, we can properly
use `oic/client-light` node type:

```sh
oic(oic/client-light)
cmdline_arguments OUT -> DEVICE_ID oic
```

First line declares our OIC client and second one sets the DEVICE_ID
it should connect from command line argument we received.
Now, cross-link accumulator and OIC client nodes:

```sh
oic POWER -> SET power_value
power_value OUT -> POWER oic
```

To complete our controller first iteration, let's inform user
when server value changes:

```sh
oic POWER -> IN _(console:prefix="Server power level is: ")
```

This way, if another controller changes server 'power' value, we
are informed. That's a nice OIC feature: we can observe the resource
and be notified about changes, instead of having to keep asking.

## Running our controller

It's time to run our controller, thus testing our OIC server and the
controller itself. On one terminal, run our server:

```sh
$ sol-fbp-runner oic-and-http-light-server.fbp
OIC device-id: 0bacbaedb34b434785dec5519d60101c (string)
note: http-light-server running on port 8002, paths: /state and /power (string)
```

Now, run our client using device-id as parameter:

```sh
$ sol-fbp-runner oic-controller.fbp 0bacbaedb34b434785dec5519d60101c
Hint: Press 'a' to change state, 'j' to increase power and 'k' to decrease it (string)
Server power level is: 42 (integer range)
Power value changed: 42 (integer range)
```

Note the two messages: first comes from OIC node, second one from
accumulator.
We didn't implemented 'a' button yet, but you can use 'j' and 'k' to
change current values and get something like:

```sh
Power value changed: 43 (integer range)
Server power level is: 43 (integer range)
Power value changed: 44 (integer range)
Server power level is: 44 (integer range)
Power value changed: 45 (integer range)
Server power level is: 45 (integer range)
Power value changed: 44 (integer range)
Server power level is: 44 (integer range)
```

See that as both accumulator packets and OIC node packets are sent to
console, you see the change you made and new server value.
You may have noticed that our controller application stays running, it
doesn't close. That's how Soletta application are supposed to live:
forever. As they are intended to run on devices, they need to run as long
as device is being used. To quit it, you may use `^C`(`Ctrl+C`).

## Converting packets

We didn't implement 'state' value changes on our controller. The idea is
basically the same: use an accumulator from 0 to 1. As accumulators
normally wrap over on overflow, it should be OK. We then proceed and
write something like:

```sh
state_value(int/accumulator:setup_value=min:0|max:1|step:1,send_initial_packet=false)

# Button control
state_button OUT -> IN _(boolean/filter) TRUE -> INC state_value
state_value OUT -> IN _(console:prefix="State changed: ")

# Cross-link oic and state accumulator
oic STATE -> SET state_value
state_value OUT -> STATE oic
```

And when running, we got the following error:

```sh
$ sol-fbp-runner oic-controller.fbp 0bacbaedb34b434785dec5519d60101c
WRN:sol-flow ./src/lib/flow/sol-flow-static.c:225 connect_nodes() Invalid connection specification: { .src_id=state_value, .src=8, .src_port=0, .dst_id=oic .dst=12, .dst_port=2 }: "Error matching source and destination packet types: int != boolean: Invalid argument"
WRN:sol-flow ./src/lib/flow/sol-flow.c:142 sol_flow_node_init() failed to create node of type=0x56098a936750: Invalid argument
Failed to run
}
```

Note message `"Error matching source and destination packet types: int != boolean:
Invalid argument"`.
This happened because we try to connect a port that outputs `int` to a
port that accepts `boolean`. Soletta flow based programming is strongly
typed, so you can't mix outputs of one type with inputs of another [[4](#footnote_04)].
Then it's necessary to change packet type. We can do so with aid of
`convert` node types.
We start with accumulator:

```sh
state_value_as_int(int/accumulator:setup_value=min:0|max:1|step:1,send_initial_packet=false)
```

Connect button to it:

```sh
state_button OUT -> IN _(boolean/filter) TRUE -> INC state_value_as_int
```

And to cross-link, we'll use two converters, from `boolean` to `int` and
from `int` to `boolean`.

```sh
state_value_from_boolean(converter/boolean-to-int)
state_value(converter/int-to-boolean)

oic STATE -> IN state_value_from_boolean
state_value OUT -> STATE oic

state_value_from_boolean OUT -> SET state_value_as_int
state_value_as_int OUT -> IN state_value
```

We started by declaring our converters, then linked them to OIC
node. Last line connects boolean value from OIC to accumulator.
A nice debug message:

```sh
state_value OUT -> IN _(console:prefix="State changed: ")
oic STATE -> IN _(console:prefix="Server state is: ")
```

Again, first line shows when accumulator value changes, second one shows
server current state.
So, when interacting with this controller, you may expect only to see
first message. But if a change is made to server via another client,
you'll see both messages.

We shall be good to go:

```sh
$ sol-fbp-runner oic-controller.fbp 0bacbaedb34b434785dec5519d60101c
Hint: Press 'a' to change state, 'j' to increase power and 'k' to decrease it (string)
Server state is: true (boolean)
Server power level is: 42 (integer range)
Power value changed: 42 (integer range)
State changed: true (boolean)
```

If you press 'a', you'll see value changing:

```sh
State changed: false (boolean)
Server state is: false (boolean)
Server power level is: 44 (integer range)
State changed: true (boolean)
Server state is: true (boolean)
Server power level is: 44 (integer range)
State changed: false (boolean)
Server state is: false (boolean)
Server power level is: 44 (integer range)
```

See that besides your change, lines starting with `State changed`, you'll
be updated about server current status.
A cool thing you can do is, on another terminal, open another controller
and change values from there: first controller will show the changes.
Test it =D
It also works if you change value via HTTP interface with `curl`:

```sh
curl --data "value=42" http://localhost:8002/power
```

Cool, isn't it?

Before moving on, let's add a nice feature: a button to quit
application, so we don't need to kill it with `^C`. This should be
as easy as:

```sh
quit_button(keyboard/boolean:binary_code=113)
quit_button OUT -> QUIT _(app/quit)
```

Now, press 'q' and quit our controller. Node type `app/quit` quits
application when receives input on its port `QUIT`. Don't forget to
update 'Hint message' as well.

### Showing a nice error message to user

If we run our `oic-controller.fbp` as is, it will end up showing an error
message like:

```
$ sol-fbp-runner oic-controller.fbp

WRN:sol-flow ./src/lib/flow/sol-flow-static.c:341 flow_send_do() Error packet '22 (Argument position (1) is greater than arguments length (1))' sent from 'oic-controller.fbp (0x560d1c548e90)' was not handled
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
$ sol-fbp-runner oic-controller.fbp

ERROR: Missing device ID in the command line. Example: 2f3089a1dbfb43d38cab64383bdf9380 (string)
```

Our controller final code should look like this:

```sh
increase_button(keyboard/boolean:binary_code=106)
decrease_button(keyboard/boolean:binary_code=107)
state_button(keyboard/boolean:binary_code=97)
quit_button(keyboard/boolean:binary_code=113)

_(constant/string:value="Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit.") OUT -> IN _(console:prefix="Hint: ")

quit_button OUT -> QUIT _(app/quit)

cmdline_arguments(app/argc-argv)
cmdline_arguments ERROR -> IN cmdline_usage(converter/empty-to-string:output_value="Missing device ID in the command line. Example: 2f3089a1dbfb43d38cab64383bdf9380") OUT -> IN _(console:prefix="ERROR: ")
cmdline_usage OUT -> IN _(converter/empty-to-int:output_value=1) OUT -> CODE _(app/quit)
_(constant/int:value=1) OUT -> IN cmdline_arguments

power_value(int/accumulator:setup_value=min:0|max:100|step:1,initial_value=50,send_initial_packet=false)
state_value_as_int(int/accumulator:setup_value=min:0|max:1|step:1,send_initial_packet=false)

increase_button OUT -> IN _(boolean/filter) TRUE -> INC power_value
decrease_button OUT -> IN _(boolean/filter) TRUE -> DEC power_value

power_value OUT -> IN _(console:prefix="Power value changed: ")

oic(oic/client-light)
cmdline_arguments OUT -> DEVICE_ID oic

oic POWER -> SET power_value
power_value OUT -> POWER oic

state_button OUT -> IN _(boolean/filter) TRUE -> INC state_value_as_int

state_value_from_boolean(converter/boolean-to-int)
state_value(converter/int-to-boolean)

oic STATE -> IN state_value_from_boolean
state_value OUT -> STATE oic

state_value_from_boolean OUT -> SET state_value_as_int
state_value_as_int OUT -> IN state_value

state_value OUT -> IN _(console:prefix="State changed: ")

# This prints server current state. It is not updated
# when this client updates server though: only if server is changed
# elsewhere `oic` node will send output packets.
oic POWER -> IN _(console:prefix="Server power level is: ")
oic STATE -> IN _(console:prefix="Server state is: ")
```

Now it looks bigger than previous codes, but still small relative to
what it does, I bet!

[Next](../step5/tutorial.md), we will write our HTTP controller.

<a name="footnote_04"/>[4] There are two special cases: some input
ports accept `any` type of packet, like `INC` of `int/accumulator` node
type; these ports can be connected to any output port. And some output
ports outputs `empty` packets; this special type of packet can only be
sent to ports that accept `any` packet as input.
