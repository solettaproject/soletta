# Step 1 - An HTTP server

## Flow Based Programming overview

Soletta provides two API [[3](#footnote_03)] for developers. A C API and a
flow based API. In this tutorial, we're going to show the flow based API.
As you can see, this concept fits better our design.
The flow based API uses [Flow Based Programming](https://en.wikipedia.org/wiki/Flow-based_programming)
concepts. It is a higher level API than its C counterpart, and it's
much easier to use when expressing relationships among application
components.
Basically, a flow based application contains 'nodes' which represents
entities - like sensors or actuators, as well as operations like
concatenating string. These nodes have input or output 'ports' that
send or receive 'packets' of information. These ports can be connected
to express the flow of information among components. For instance:

```sh
button(gpio/reader:pin=1) OUT -> IN light(gpio/writer:pin=2)
```

Here, `button` is a node of node type `gpio/reader` and light is
another node, of node type `gpio/writer`. Another way to see it is that
`button` is an instance of `gpio/reader` node type.
'Node type' refers to the description of a node, its 'type'. Only 'node'
refers to an instance of a node type, akin to 'class' and 'object' on
Object-Oriented Programming.
A `gpio/reader` node has an output port `OUT` - which sends boolean
packets - that is connected to input port `IN` of the `gpio/writer` -
which in turn, receives boolean packets as well. So, in this sample,
we use `button` output to feed `light`. The `pin=N` on those
nodes is a 'node option'. Node options are used to setup nodes,
so each node, in this sample, is aware of which GPIO pin they
represent.
Note that subsequent uses of a node don't take its setup information
again, so, if we wanted to debug `button` output to console,
we would do:

```sh
button OUT -> IN output(console)
```

For more information about Soletta flow programming approach,
you can see [Soletta README](https://github.com/solettaproject/soletta#flow-based-programming).
For more information about Soletta available node types, its options,
input and output ports, see Soletta node type [Cheat Sheet](http://solettaproject.github.io/docs/nodetypes/).

## A really simple HTTP server

Soletta provides some HTTP server node types. These node types are
really simple, they basically serve a value of some data type. For
instance, there's an `http-server/int` node type that serves a plain
integer value. It looks very naive, but it's exactly what we need to
control light power intensity: serve an integer value.

```sh
power(http-server/int:path="/power",port=8002)
```

This will create a node `power` of node type `http-server-int`, whose
path is `/power`, on port `8002`. Do not confuse this port with flow
ports, like `IN` or `OUT`.
If you save this file as `http-light-server.fbp`, then you can run it
with:

```sh
$ sol-fbp-runner http-light-server.fbp
```

Soletta will initiate its mainloop and start serving on port 8002.
If you access `http://localhost:8002/power` on you browser you can
see its current value, 0.
You can also use `curl`:

```sh
$ curl  http://localhost:8002/power
0
```

Soletta HTTP node types have also an interesting feature: if you add an
HTTP header `Accept: application/json`, to use `application/json` MIME type,
then you get current value as JSON:

```sh
$ curl -H "Accept:application/json"  http://localhost:8002/power
{"/power":{"value":0,"min":-2147483648,"max":2147483647,"step":1}}
```

You may notice a `min`, `max` and `step` values, they relate with
the range of value. For instance, a temperature sensor would define

```sh
"min":-30.0,"max":100.0,"step":0.5
```

For our sample, we won't use this information.
A server with read only values may be useful, but it would be better
if we could change its value. And we can do so! Soletta HTTP node types
may have their values changed with a POST request:

```sh
$ curl --data "value=42" http://localhost:8002/power
42
```

Now, we can improve our server by adding an `http-server/boolean` node
to hold 'state' value (if light is ON or OFF):

```sh
state(http-server/boolean:path="/state",port=8002)
```

Soletta will serve 'state' on  `/state` path on the same `8002` port:

```sh
$ curl http://localhost:8002/state
true
$curl --data "value=false" http://localhost:8002/state
false
```

So our .fbp file should be like this:

```sh
power(http-server/int:path="/power",port=8002)
state(http-server/boolean:path="/state",port=8002)
```

You may be wondering if we can have two HTTP servers at the same time
running. Yes, we can! Actually, Soletta creates internally a single HTTP
server and integrates it with its mainloop, managing requests
appropriately. Declaring servers also **does not** block execution. In
fact, a fbp file **describes** flow of information between nodes, so
there's nothing like 'running line by line' the fbp file. And for those
who may be curious about, Soletta internal server doesn't involve
anything like Apache HTTP Server: it's currently based on
[libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/).

## Exposing our flow as a subflow

Let's recall how our server was designed:

![alt tag](../step0/diagram1.png)

We can see that our server is composed of three parts: HTTP server,
OIC server and persistence. We just created our HTTP server, but how will
we integrate it on our server? We may use subflows: small flows that do
only one task and expose input and output ports that can be integrated
into another flow. This allows flow based programming applications to
keep a better granularity of code, providing separation of concerns:
each subflow is responsible for a particular part of application. As a
bonus, it also permit code reuse.
To enable our HTTP server to be used on other flows, we basically need
to declare its input and output ports:

```sh
INPORT=power.IN:POWER
INPORT=state.IN:STATE

OUTPORT=power.OUT:POWER
OUTPORT=state.OUT:STATE
```

First, we declare two input ports: `POWER` and `STATE`, that
are `power.IN` and `state.IN`, respectively. Then we declare two
output ports: `POWER` and `STATE`, that are `power.OUT` and
`state.OUT` respectively. Thus, for instance, each time a packet comes
to subflow input port `POWER`, this packet is sent to port IN of node
`power`. Conversely, each time a packet is sent to node `power` OUT port,
it is sent to subflow port `POWER`.
Before finishing our server, let's print, for debug purposes, some
information about our running server. It is just a matter of sending
a constant string to `console` node type:

```sh
_(constant/string:value="http-light-server running on port 8002, paths: /state and /power") OUT -> IN _(console:prefix="note: ")
```

First node is a `constant/string` node, it's behaviour is just to send to
its `OUT` port a string packet containing its `value` option value. This
packet is sent to `IN` port of `console` node type that prints it to
console. Option `prefix` of `console` node type is a prefix that is
always added before sending text to console, so when running our server,
now you'll see:

```sh
$ sol-fbp-runner http-light-server.fbp
note: http-light-server running on port 8002, paths: /state and /power (string)
```

One thing that you may have noticed is that both nodes are named `_`.
This means that they are anonymous nodes. Useful when nodes are used on
a single connection on flow, so there's no need to name them.
Finally, let's log values changes:

```sh
http_server POWER -> IN print_power(console)
http_server STATE -> IN print_state(console)
```

## Tips about parsing errors

If you write something that `sol-fbp-runner` fails to parse, don't panic!
Small typos may always happen and Soletta tries to provide meaningful
error messages to help us understand what is wrong and how to fix.
Suppose we wrote something like:

```sh
power(http-server/imt:path="/power",port=8002)
```

You may get an error like:

```sh
$ sol-fbp-runner http-server.fbp
WRN:sol-flow ./src/lib/flow/sol-flow-resolver-conffile.c:214 _resolver_conffile_get_module() Type='http-server/imt' not found.
http-server.fbp:1:17 Couldn't resolve type 'http-server/imt' of node 'power'
```

Look at last line: it states the error - that it doesn't know about type
`http-server/imt`. It also show the place of occurrence of first
appearance of problematic node: `http-server.fbp:1:17`. We just have to
find its declaration and fix the typo: node type is called
`http-server/int`.
Another common mistake is a typo at an option name, like:

```sh
_(constant/string:value="http-light-server running on port 8002, paths: /state and /power") OUT -> IN _(console:prefixo="note: ")
```

We then get an error like:

```sh
$ sol-fbp-runner http-server.fbp
http-server.fbp:10:113 Unknown option name 'prefixo' of node '#anon:10:103'
```

As this error is on an anonymous node, it shows where it's declared:
`'#anon:10:103'`.

We can use this information to find node and make the proper fix: option
name is `prefix`.

This concludes our HTTP server. You should have something like this:

```sh
INPORT=power.IN:POWER
INPORT=state.IN:STATE

OUTPORT=power.OUT:POWER
OUTPORT=state.OUT:STATE

power(http-server/int:path="/power",port=8002)
state(http-server/boolean:path="/state",port=8002)

power OUT -> IN print_power(console)
state OUT -> IN print_state(console)

_(constant/string:value="http-light-server running on port 8002, paths: /state and /power") OUT -> IN _(console:prefix="note: ")
```

Really small, huh?

[Next](../step2/tutorial.md), let's move on with our server and add persistence.

<a name="footnote_03">[3]</a> Application programming interface
