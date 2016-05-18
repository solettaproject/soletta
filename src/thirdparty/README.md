# Thirdparty

## Duktape
 * Duktape is an embeddable Javascript engine, with focus on
   portabillity and compact footprint. This is used in order
   to create JS node types in flow.
 * License: MIT - src/thirdparty/duktape/LICENSE.txt
 * Links: [Site](http://duktape.org) -
   [Repository](https://github.com/svaarala/duktape)

## OpenInterConnect - IoTDataModels
  * oic-data-models are IoT data models to be used by OIC protocol.
    JSON files in this repository should be used by sol-oic-gen to
    create Soletta components for such resources.
  * License: BSD 2-Clause as can see on [Readme.md](https://github.com/OpenInterConnect/IoTDataModels/blob/master/README.md)
  * Links: [Site](http://openconnectivity.org/)
  [Repository](https://github.com/OpenInterConnect/IoTDataModels)

## MAVLink
 * Lightweight communication protocol between Micro Air Vehicles (swarm)
   and/or ground control stations.
 * License: LGPLv3

   > "The C-language version of MAVLink is a header-only library,
   and as such compiling an application with it is considered
   "using the library", not a derived work
   (copied from [Readme.md](https://github.com/mavlink/mavlink/blob/master/README.md))
 * Links: [Repository](https://github.com/mavlink/mavlink) -
   [Generated](https://github.com/mavlink/c_library)

## TinyCBOR
 * CBOR is the "Concise Binary Object Representation", and is the
   format used by OIC to encode network payload. TinyCBOR is a library
   to encode and decode data in CBOR.
 * License: MIT - src/thirdparty/tinycbor/src/cbor.h
 * Links: [Repository](https://github.com/01org/tinycbor)

## TinyDTLS
 * Very simple datagram server with DTLS support, designed to support
   session multiplexing in single-threaded
   applications and thus targets specifically on embedded systems.
 * License: MIT
 * Links: [Repository](http://sourceforge.net/projects/tinydtls/)
