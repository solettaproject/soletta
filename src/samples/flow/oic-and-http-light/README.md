# Sample: OIC and HTTP light-server

This sample provides a light-server that uses both OIC and HTTP to
communicate with controllers, persisting data to disk so it can be
restored on restart.

It also provides two controllers, one via HTTP and the other via
OIC. The OIC is very simple, since the resource is exposed as a single
node with multiple ports and the protocol supports observing, thus no
polling is required. HTTP uses individual nodes per sub-resource
(power and state) and needs to poll in a timely manner to synchronize
the controller status with the light-server. However both controllers
share the same logic to read input and quit the application, so it is
isolated in its own `controller.fbp`.

The input and output are declared in `sol-flow.json` and the board
variants. By default it will read from keyboard and print to console,
in supported boards it will use GPIO and PWM to act on hardware.

## Concepts

 * running multiple network servers in the same application;
 * splitting functionality into different FBP files;
 * using per-board configuration of nodes, with reasonable fallback;
 * how to use the integer accumulator;
 * how to convert between packet types;
 * synchronization using cross-link between node types;
 * receiving command from command line;
 * quitting the application;
 * using the intrinsic ERROR port;
 * persistence to file storage;
 * HTTP server and client;
 * OIC server and client.

## Running

To test the setup open 3 console terminals:

### Server Terminal

```sh
user/host$ export SOL_MACHINE_ID="580a3d6a9d194a23b90a24573558d2f4"
user/host$ sol-fbp-runner oic-and-http-light-server.fbp

OIC device-id: 580a3d6a9d194a23b90a24573558d2f4 (string)
note: http-light-server running on port 8002, paths: /state and /power (string)
state: true (boolean)
power: 100 (integer range)

```

Defining the `SOL_MACHINE_ID` is optional, but recommended to keep
execution consistent. You'll need the machine ID since it is used to
create the OIC Device ID, however this is printted to the console as
displayed above.

The server will save its state, thus it can be stopped (with `^C` or
kill) and restarted.

### HTTP Controller Terminal

```sh
user/host$  export no_proxy="localhost,$no_proxy"
user/host$ sol-fbp-runner http-controller.fbp http://localhost:8002
Hint: Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit. (string)
Server power level is: 0 (integer range)
Power value changed: 0 (integer range)
Server state is: true (boolean)
State changed: true (boolean)
Server power level is: 0 (integer range)
Server state is: true (boolean)
State changed: false (boolean)
Server power level is: 0 (integer range)
Server state is: false (boolean)
Power value changed: 1 (integer range)
Server power level is: 1 (integer range)
Server state is: false (boolean)
```

Defining `no_proxy` is optional, but recommended to avoid incorrect
proxy configurations.

Upon start, the controller will query the server using HTTP `GET`
requests and will update its internal state.

The state can then be changed using the keyboard:
 * `a` (keycode 97): change state
 * `j` (keycode 106): increase power level
 * `k` (keycode 107): decrease power level
 * `q` (keycode 113): quit the application

Once the state is changed, it will execute HTTP `POST` requests to the
server, which will show the updated values.

### OIC Controller Terminal

```sh
user/host$ sol-fbp-runner oic-controller.fbp 580a3d6a9d194a23b90a24573558d2f4
Hint: Press 'a' to change state, 'j' to increase power and 'k' to decrease it, 'q' to quit. (string)
Server state is: true (boolean)
Server power level is: 1 (integer range)
Power value changed: 1 (integer range)
State changed: true (boolean)
State changed: false (boolean)
Power value changed: 2 (integer range)

```

Upon start, the controller will start a scan and if the given device
ID is found, it will be observed.

The state can then be changed using the keyboard:
 * `a` (keycode 97): change state
 * `j` (keycode 106): increase power level
 * `k` (keycode 107): decrease power level
 * `q` (keycode 113): quit the application

Once the state is changed, it will execute CoAP `POST` requests to the
server, which will show the updated values.

### TODO

 * create a second http-controller that reads the URL from form/string + grove/lcd-string and GPIO, and persist. Press and hold one button to reconfigure URL;
 * create a second oic-controller that SCAN the DEVICE-ID and uses form/selector + grove/lcd-string to select, and persist. Press and hold one button to reconfigure DEVICE-ID;
 * create a tutorial covering incremental steps on how to do the whole sample (could be in a sub-directory of it or in doc/tutorial), how to compile and run for multiple boards and OS.
