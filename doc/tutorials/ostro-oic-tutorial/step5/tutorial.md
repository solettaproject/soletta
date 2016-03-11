#Step 5 - HTTP Controller

## Isolating controller code

We have an OIC controller, why not an HTTP one? It could have some
buttons to increase/decrease current value and one to toggle state.
Wait! This is the same interface of our OIC controller... It'd be
nice if we could reuse some of that code. And we can, of course!
Looking at our `oic-controller.fbp` one more time:

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
```

We can see that this code can be divided into three parts: 1) code that
takes care of command line args and send to 2) code that deals with OIC
client and 3) code that receives user input and store it accumulator,
that will send to 2) as well. It's clear that 3) is the one to separate
on a subflow. We can start by moving button related code to a
`controller.fbp`:

```sh
increase_button(keyboard/boolean:binary_code=106)
decrease_button(keyboard/boolean:binary_code=107)
state_button(keyboard/boolean:binary_code=97)
quit_button(keyboard/boolean:binary_code=113)

_(constant/string:value="Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit.") OUT -> IN _(console:prefix="Hint: ")

quit_button OUT -> QUIT _(app/quit)
```

We moved `quit_button` action and our hints, as they are related to
buttons. We can actually do the same to our accumulators and converters:

```sh
power_value(int/accumulator:setup_value=min:0|max:100|step:1,initial_value=50,send_initial_packet=false)
state_value_as_int(int/accumulator:setup_value=min:0|max:1|step:1,send_initial_packet=false)

increase_button OUT -> IN _(boolean/filter) TRUE -> INC power_value
decrease_button OUT -> IN _(boolean/filter) TRUE -> DEC power_value

power_value OUT -> IN _(console:prefix="Power value changed: ")

state_button OUT -> IN _(boolean/filter) TRUE -> INC state_value_as_int

state_value_from_boolean(converter/boolean-to-int)
state_value(converter/int-to-boolean)

state_value OUT -> IN _(console:prefix="State changed: ")
```

This only leaves out connections with OIC nodes, like `SET` ports of
`power_value` or `IN` port of `state_value_from_boolean`. Those ports
are actually our controller interface to external world, let's declare
them as such:

```sh
OUTPORT=power_value.OUT:POWER
OUTPORT=state_value.OUT:STATE

INPORT=power_value.SET:POWER
INPORT=state_value_from_boolean.IN:STATE
```

This should finalise our controller. Let's now adjust our
`oic-controller.fbp`. We removed some code from it, now let's use our
new subflow:

```sh
DECLARE=controller:fbp:controller.fbp

# create a controller
controller(controller)
```

And finally, OIC cross-link is refactored to use new controller subflow:

```sh
oic POWER -> POWER controller
oic STATE -> STATE controller

controller POWER -> POWER oic
controller STATE -> STATE oic
```

Do not forget to test to see if our refactored code works:

```sh
$ sol-fbp-runner oic-controller.fbp 0bacbaedb34b434785dec5519d60101c
Hint: Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit. (string)
Server state is: true (boolean)
Server power level is: 42 (integer range)
Power value changed: 42 (integer range)
State changed: true (boolean)
```

It works! Now, to the HTTP controller.

## HTTP Controller

Since we have a handy controller subflow available to use, HTTP
controller should be quite straightforward. We start our
`http-controller.fbp` by declaring and creating out controller:

```sh
DECLARE=controller:fbp:controller.fbp

# create a controller
controller(controller)
```

To really connect to HTTP server, we are going to use Soletta node types
`http-client/int` and `http-client/boolean`. Those nodes need an URL to
connect to. Again, we'll get base URL via command line:

```sh
cmdline_arguments(app/argc-argv)
cmdline_arguments ERROR -> IN cmdline_usage(converter/empty-to-string:output_value="Missing URL in the command line. Example: http://localhost:8002") OUT -> IN _(console:prefix="ERROR: ")
cmdline_usage OUT -> IN _(converter/empty-to-int:output_value=1) OUT -> CODE _(app/quit)
_(constant/int:value=1) OUT -> IN cmdline_arguments
```

Not much different from OIC counterpart. We just changed error message.
But we'll only ask base URL. We must complete it by adding `/power` or
`/state`, to get each value. Soletta provides nodes for concatenating
strings, they are the way to go:

```sh
# Build URL for each resource
_(constant/int:value=1) OUT -> IN cmdline_arguments

state_url(string/concatenate:separator="/")
cmdline_arguments OUT -> IN[0] state_url
_(constant/string:value="state") OUT -> IN[1] state_url

power_url(string/concatenate:separator="/")
cmdline_arguments OUT -> IN[0] power_url
_(constant/string:value="power") OUT -> IN[1] power_url
```

We used `string/concatenate` node types. Note how it has indexed input,
so its port `IN` can be indexed.
Different from OIC, an HTTP server will not inform us about changes on its
state, so we have to keep polling to get its current status. We'll do so
with aid of a timer:

```sh
trigger_get(timer:interval=1000)
```

This timer node will send an empty pack each 1000 milliseconds, so we can
ask HTTP server repeatedly about its status.
An `http-client` node type will be used to ask HTTP server:

```sh
http_power(http-client/int)
power_url OUT -> URL http_power
http_power ERROR -> IN _(converter/empty-to-string:output_value="Error communicating with HTTP server at /power. Check if the server is running.") OUT -> IN note
```

This code declares the client node, sets its URL - using the output of
corresponding concatenate node. We also capture its error packet, so we
can inform user about errors. The `note` node is just a `console`
node with a defined prefix:

```sh
note(console:prefix="note: ")
```

Now we use our timer node, `trigger_get`, to perform a request for HTTP
server current power each second:

```sh
trigger_get OUT -> GET http_power
http_power OUT -> POWER controller
http_power OUT -> IN _(console:prefix="Server power level is: ")
```

'Power' request response is then set to our `controller` node, as well as
informed to user.

Besides getting information about server status, we will also update
our HTTP server:

```sh
# Notify server of power level change
controller POWER -> POST http_power
```

Then, we repeat the same to get and set 'state':

```sh
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

Then, run it (remember that our server must be running on another
terminal):

```sh
$ sol-fbp-runner http-controller.fbp http://localhost:8002
Hint: Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit. (string)
Server power level is: 43 (integer range)
Power value changed: 43 (integer range)
Server state is: false (boolean)
Server power level is: 43 (integer range)
State changed: true (boolean)
Server state is: true (boolean)
Server power level is: 43 (integer range)
Server state is: true (boolean)
Server power level is: 43 (integer range)
Server state is: true (boolean)
Power value changed: 44 (integer range)
Server power level is: 44 (integer range)
Server power level is: 44 (integer range)
Server state is: true (boolean)
Power value changed: 45 (integer range)
Server power level is: 45 (integer range)
Power value changed: 44 (integer range)
Server power level is: 44 (integer range)
Server power level is: 44 (integer range)
Server state is: true (boolean)
```

You'll notice it's much more verbose, as it asks server current state
each second. You can run OIC and HTTP controllers and see how
they interact.
This step was a big one: we refactored our OIC controller to extract
interactive controller from it and created our HTTP controller.
Final `controller.fbp` should looks like:

```sh
OUTPORT=power_value.OUT:POWER
OUTPORT=state_value.OUT:STATE

INPORT=power_value.SET:POWER
INPORT=state_value_from_boolean.IN:STATE

increase_button(keyboard/boolean:binary_code=106)
decrease_button(keyboard/boolean:binary_code=107)
state_button(keyboard/boolean:binary_code=97)
quit_button(keyboard/boolean:binary_code=113)

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

A small `oic-controller.fbp`:

```sh

DECLARE=controller:fbp:controller.fbp

cmdline_arguments(app/argc-argv)
cmdline_arguments ERROR -> IN cmdline_usage(converter/empty-to-string:output_value="Missing device ID in the command line. Example: 2f3089a1dbfb43d38cab64383bdf9380") OUT -> IN _(console:prefix="ERROR: ")
cmdline_usage OUT -> IN _(converter/empty-to-int:output_value=1) OUT -> CODE _(app/quit)
_(constant/int:value=1) OUT -> IN cmdline_arguments

# create a controller
controller(controller)

oic(oic/client-light)
cmdline_arguments OUT -> DEVICE_ID oic 

oic POWER -> POWER controller
oic STATE -> STATE controller

controller POWER -> POWER oic 
controller STATE -> STATE oic 

# This prints server current state. It is not updated
# when this client updates server though: only if server is changed
# elsewhere `oic` node will send output packets.
oic POWER -> IN _(console:prefix="Server power level is: ")
oic STATE -> IN _(console:prefix="Server state is: ")
```

And finally, `http-controller.fbp`:

```sh
DECLARE=controller:fbp:controller.fbp

# create a controller
controller(controller)

cmdline_arguments(app/argc-argv)
cmdline_arguments ERROR -> IN cmdline_usage(converter/empty-to-string:output_value="Missing URL in the command line. Example: http://localhost:8002") OUT -> IN _(console:prefix="ERROR: ")
cmdline_usage OUT -> IN _(converter/empty-to-int:output_value=1) OUT -> CODE _(app/quit)
_(constant/int:value=1) OUT -> IN cmdline_arguments

# Build URL for each resource
state_url(string/concatenate:separator="/")
cmdline_arguments OUT -> IN[0] state_url
_(constant/string:value="state") OUT -> IN[1] state_url

power_url(string/concatenate:separator="/")
cmdline_arguments OUT -> IN[0] power_url
_(constant/string:value="power") OUT -> IN[1] power_url

# HTTP provides no callback or observe, so trigger will keep polling
# server for status
trigger_get(timer:interval=1000)

note(console:prefix="note: ")

http_power(http-client/int)
power_url OUT -> URL http_power
http_power ERROR -> IN _(converter/empty-to-string:output_value="Error communicating with HTTP server at /power. Check if the server is running.") OUT -> IN note.

# load power level from server
trigger_get OUT -> GET http_power
http_power OUT -> POWER controller
http_power OUT -> IN _(console:prefix="Server power level is: ")

# Notify server of power level change
controller POWER -> POST http_power

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

Even those a little bit bigger, are really small since the features
they have, don't you think? We have two controllers, each talking a
different protocol, with barely more than a hundred lines of code.
Our application is complete, but runs on developer's machine only.

[Next](../step6/tutorial.md) we'll take it to some real board, like MinnowBoard MAX.
