# Step 3 - OIC server interface

[OIC](http://openinterconnect.org/) is a specification to facilitate
communication among a variety of devices, focused on Internet of Things.
It allows for instance, discovery and observation of a resource. So, in
our application, our server could expose an OIC resource that can be
observed by an OIC enabled controller.
One can specify a resource - using OIC format - and Soletta is able to
generate client and server node types for that OIC resource. This is very
handy, but we won't cover this feature in this tutorial. Thankfully,
Soletta has samples of OIC node types, useful for tests. We will use
them instead. These OIC node types are client and server and provides
access to a light resource, that exposes 'state' and 'power' values,
exactly what we need! Coincidence? =D
Those nodes are `oic/server-light` and `oic/client-light`. Naturally,
our server will use server node.
We start by adding OIC server node:

```sh
oic_server(oic/server-light)
```

After, we cross-link it with our HTTP server, so changes on one
are reflected on other:

```sh
oic_server POWER -> POWER http_server
oic_server STATE -> STATE http_server

http_server POWER -> POWER oic_server
http_server STATE -> STATE oic_server
```

OIC specifies that devices have an ID to identify them. Soletta OIC
node types use a 'machine ID' as OIC ID. To ease our testing, let's
show what is our server ID. We do so with:

```sh
# show machine-id as device-id
_(platform/machine-id) OUT -> IN _(console:prefix="OIC device-id: ")
```

And that's it. With this simple modifications, our server exposes an
OIC interface. Final code should looks like:

```sh
DECLARE=http-light-server:fbp:http-light-server.fbp
DECLARE=persistence-light-server:fbp:persistence-light-server.fbp

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
```

Rename it to something like `oic-and-http-light-server.fbp`, as it's not
a simple HTTP server anymore ;)

[Next](../step4/tutorial.md), we'll create a controller application to access our OIC interface,
after all, we need to test it.
