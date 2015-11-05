# OIC Examples

These examples communicate usin OIC - http://openinterconnect.org/

The OIC communication is done over IP, including 6loWPAN, Ethernet or
WiFi.

## Light Client

The examples rely on client and servers. Since the pairing is still
undefined and thus not implemeted, please edit a configuration file to
state your device_id in your node.

Usage example (server and clients may be run in different machines,
or in the same machine):

 * server:
```sh
export SOL_MACHINE_ID="580a3d6a9d194a23b90a24573558d2f4"
export SOL_FLOW_MODULE_RESOLVER_CONFFILE=light-server.json
sol-fbp-runner light-server.fbp
```

 * client:
```sh
export SOL_FLOW_MODULE_RESOLVER_CONFFILE=light-client.json
sol-fbp-runner light-client.fbp
```

Server should print on console light state (it is changed by
client at each second). SOL_MACHINE_ID must match device_id set
on client configuration file.

# Light Scan

Light scan works similarly to Light Client example, but it scans for server with
OIC lights, instead of using SOL_MACHINE_ID to locate the server.

Usage example:

 * server (run as many as you want, in different machines):
```sh
export SOL_FLOW_MODULE_RESOLVER_CONFFILE=light-server.json
sol-fbp-runner light-server.fbp
```

 * client:
```sh
sol-fbp-runner light-scan/light-client.fbp
```
