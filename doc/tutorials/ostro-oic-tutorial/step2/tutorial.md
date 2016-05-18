# Step 2 - Persisting information

## A persistence subflow

Currently, if we execute our server twice, it will 'forget' values
set on previous execution. But it doesn't have to be like this: thanks
to Soletta persistence nodes, we can persist their values and restore
on subsequent executions.
To help keep our code organised, let's write a subflow on another file,
just exposing needed ports, so if in the future we need to change
anything about persistence, there's only one file to change.
Soletta persistence nodes save packets to persistent storage. In our
sample we are going to use 'filesystem' persistent storage. To do
so, we just need two new nodes:

```sh
power(persistence/int:name="power",storage=fs)
state(persistence/boolean:name="state",storage=fs)
```

That's it. First node shall store 'power' value and second one 'state'
value. Option `storage=fs`states that they will use `filesystem` as
storage media.
Now, let's export input and output ports from this subflow, so it can be
used elsewhere:

```sh
INPORT=power.IN:POWER
INPORT=state.IN:STATE

OUTPORT=power.OUT:POWER
OUTPORT=state.OUT:STATE
```

`INPORT` and `OUTPORT` statements maps which ports of which nodes
will be exported from this subflow. In this case, we export ports
`IN` of `power` and `state` nodes as `POWER` and `STATE`,
respectively. We do the same for output ports: `OUT` of `power` and
`state` are exported as `POWER` and `STATE`, respectively.
But what if there are no persisted value so far? Soletta persistence
node types also have a `default_value` for this case. However, it is not
responsibility of subflow to know such defaults. So we export them and
those who use our subflow may set it appropriately:

```sh
OPTION=power.default_value:default_power
OPTION=state.default_value:default_state
```

These `OPTION` statements on our sample export node `power` and
`state` option `default_value` as `default_power` and `default_state`.
You can save this fbp file as `persistence-light-server.fbp`.
It should look like this, at this point:

```sh
INPORT=power.IN:POWER
INPORT=state.IN:STATE

OUTPORT=power.OUT:POWER
OUTPORT=state.OUT:STATE

OPTION=power.default_value:default_power
OPTION=state.default_value:default_state

power(persistence/int:name="power",storage=fs)
state(persistence/boolean:name="state",storage=fs)
```

## A server of servers

So far, so good. But we need to test our subflow, to see if it's working.
How? The simple answer is to write a flow to use our subflow. Let's write
it!
To be able to use subflows, a fbp file needs to declare the subflows it
intends to use. We do so via `DECLARE` statement:

```sh
DECLARE=http-light-server:fbp:http-light-server.fbp
DECLARE=persistence-light-server:fbp:persistence-light-server.fbp
```

Now, we have access to node types `http-light-server` and
`persistence-light-server`. That's how we use subflows: as any other
node type. So, we have to declare them:

```sh
http_server(http-light-server)
persistence(persistence-light-server:default_state=true,default_power=100)
```

Now we can connect our HTTP server subflow and persistence subflow, so
changes on HTTP server are persisted:

```ssh
http_server POWER -> POWER persistence
http_server STATE -> STATE persistence
```

And to finalise, when we start our HTTP server, we want to restore
persisted values, then we just need to connect our persistence output
to server input:

```ssh
persistence POWER -> POWER http_server
persistence STATE -> STATE http_server
```

If you look carefully, we cross linked them: output from one node is
input to another and vice versa. Why don't this cause an infinite loop
of packets between nodes? As both HTTP server and persistence node types
only send an output packet if their current value didn't change, they
will stabilise communication after first round of packets. Thus, it is
safe to cross-link them.
If you save this new file as `http-server-and-persistence.fbp`, then you
can run it like:

```sh
$ sol-fbp-runner http-server-and-persistence.fbp
note: http-light-server running on port 8002, paths: /state and /power (string)
```

We can interact once again using `curl`:

```sh
$ curl --data "value=42" http://localhost:8002/power
```

If we close our server and start it again after, you'll notice that
previously set value is still there:

```sh
$ curl http://localhost:8002/power
42
```

Our server final code should be like:

```sh
DECLARE=http-light-server:fbp:http-light-server.fbp
DECLARE=persistence-light-server:fbp:persistence-light-server.fbp

http_server(http-light-server)

persistence(persistence-light-server:default_state=true,default_power=100)

http_server POWER -> POWER persistence
http_server STATE -> STATE persistence

persistence POWER -> POWER http_server
persistence STATE -> STATE http_server
```

Pretty small, don't you think?

[Next](../step3/tutorial.md), we'll add OIC interface, this should complete our server.
