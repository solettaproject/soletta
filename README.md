# Soletta Project

[![Build Status](https://semaphoreci.com/api/v1/projects/dfd5e3f1-9539-49d5-a2bc-55415867db02/460921/shields_badge.svg)](https://semaphoreci.com/lstrano/soletta--2)<br/>
[![Coverity Scan Build Status](https://scan.coverity.com/projects/5517/badge.svg)](https://scan.coverity.com/projects/5517)

**Soletta Project** is a framework for making IoT devices.
With Soletta Project's libraries developers can easily write software
for devices that control actuators/sensors and communicate using
standard technologies. It enables adding smartness even on the
smallest edge devices.

Portable and scalable, it abstracts details of hardware and OS,
enabling developers to reuse their code and knowledge on different
targets.

## TOC ##

 * [General Information](#general-information)
 * [Building from Source](#building-from-source)
 * [Debug](#debug)
 * [Libraries](#libraries)
  * [Common](#common)
  * [Comms](#comms)
    * [Network](#network)
    * [CoAP](#coap)
    * [OIC](#oic)
    * [MQTT](#mqtt)
  * [Flow](#flow)
 * [Main Loops](#main-loops)
  * [GLib](#glib-kconfig-core-library---mainloop---glib)
  * [POSIX](#posix-kconfig-core-library---mainloop---posix)
 * [Platforms](#platforms)
  * [Systemd](#systemd-kconfig-core-library---target-platform---systemd)
  * [Linux-micro](#linux-micro-kconfig-core-library---target-platform---linux-micro)
 * [Flow Based Programming](#flow-based-programming)
 * [Contributing](#contributing)


## General Information

**Soletta Project** uses `sol` as C namespace, so macros start with `SOL_`
and functions, enumerations, structures and others start with `sol_`.

It uses a main loop to provide single threaded cooperative tasks
(co-routines) triggered by UNIX file-descriptors, timers or idlers
(runs whenever there is nothing else to do). The traditional main loop
is based on Glib's GMainLoop, while some smaller OS have their own
implementation, see [main loops](#main-loops) documentaion.

## Building from Source

The build system is based on linux kernel's kconfig. To configure it
with default configuration values run:

        make alldefconfig

To update the configurations using a curses based interface run:
        make menuconfig

To update the configurations using a line-oriented interface run:
        make config

More options and variables are available with:

        make help

To compile use (from top directory)

        make

or to be verbose and get all the commands being executed:

        make V=1

To install run:
        make install

the default behavior is to install in the root dir (namely /), but
to install in a different root dir run install as:

        make install DESTDIR=/path/to/install/root/

## Debug

**Soletta** provides `sol-log` to provide meaningful critical, error, warning,
informational or debug messages in a simple way. The following
environment variables affect sol-log behavior:

    export SOL_LOG_LEVEL="LEVEL"

        defines the maximum level to show messages. This affects all
        domains. The value of LEVEL can be an integer or the string
        alias such as CRITICAL, ERROR, WARNING, INFO, DEBUG, CRI, ERR,
        WRN, INF, DBG.

    export SOL_LOG_LEVELS="domain1:level1,domain2:level2,...,domainN:levelN"

        the fine-grained version of $SOL_LOG_LEVEL that specifies
        different levels per domain. The specification is a key:value
        pair, the key being the domain name and the level being an
        integer or string alias.

    export SOL_LOG_ABORT="LEVEL"

        if a message_level is less or equal to LEVEL, then the program
        will abort execution with abort(3). Say SOL_LOG_ABORT=ERROR,
        then it will abort on critical or error messages.
        Defaults to critical only.

    export SOL_LOG_SHOW_COLORS=[0|1]

        will disable or enable the color output.
        Defaults to enabled if terminal supports it.


    export SOL_LOG_SHOW_FILE=[0|1]

        will disable or enable the file name in output.
        Enabled by default.

    export SOL_LOG_SHOW_FUNCTION=[0|1]

        will disable or enable the function name in output.
        Enabled by default.

    export SOL_LOG_SHOW_LINE=[0|1]

        will disable or enable the line number in output.
        Enabled by default.

Note that at compile time some levels may be disabled by usage of
`SOL_LOG_LEVEL_MAXIMUM` C-pre-processor macro, which may be set for
Soletta itself (internally) by resetting it on kconfig (i.e menuconfig:
Core library -> Log -> Maximum log level).

or by applications if they define that in some way. Then messages
above that number will be compiled out and using `$SOL_LOG_LEVEL` or
`$SOL_LOG_LEVELS` for those numbers won't have effect.


## Libraries

#### common

Main loop, logging and access to platform details such as
services and state.

Main loop will allow cooperative routines to run in a single
thread, they can be started from a timeout (timer), an
interruption handler, file descriptor monitor (POSIX-like
systems) or when nothing else is running (idlers).

Logging allows different domains to be logged independently,
making it is easy to debug an application or Soletta itself.

Platform allows checking the system state, if it's ready,
still booting, degraded (something failed), going to shutdown
and so on. It has the concept of services that can be started
or stopped as well as monitored for a dynamic lifecycle
management.


#### comms

Comms consists on a few communication modules.
It provides ways to deal with network, [CoAP protocol](#coap) and
[OIC protocol](#oic) (server and client sides).


##### Network

Network library provides a way to handle network link interfaces,
making it possible to observe events, to inquire available links
and to set their states.

##### CoAP

Implementation of The Constrained Application Protocol (CoAP -
[RFC 7252](https://tools.ietf.org/html/rfc7252)).
This network protocol is pretty simple and uses a
HTTP-like message that is space-efficient over UDP.

##### OIC

Implementation of protocol defined by
[Open Interconnect Consortium](http://openinterconnect.org/).

It's a common communication framework based on industry standard
technologies to wirelessly connect and intelligently manage
the flow of information among devices, regardless of form factor,
operating system or service provider.

Both client and server sides are covered by this library.

##### MQTT

Wrapper around the mosquitto library implementation of the [MQTT
protocol](http://mqtt.org/).


MQTT is a machine-to-machine (M2M)/"Internet of Things"
connectivity protocol. It was designed as an extremely
lightweight publish/subscribe messaging transport. It is useful

The broker is not covered by the wrapper, only the client.

#### flow

Implementation of [Flow-Based Programming (FBP)](#flow-based-programming),
allowing the
programmer to express business logic as a directional graph of
nodes connected to type-specific ports.


## Main Loops

Main loops are responsible to deliver events and timers in a single
thread by continuously poll and interleave registered functions. It
abstracts every OS particularity and delivers common behavior to
Soletta applications.

For instance, while in Linux GPIO interruptions are handled at the
kernel level and dispatched to userspace by means of file-descriptors
that get notification via poll(2) syscall, in RIOT you get called from
the interruption service routine (ISR) and you can mess with other
interruptions while you execute your code. Soletta uses mainloop to
remove those differences and in RIOT you'll get an internal ISR that
queues GPIO events to be later delivered by the mainloop in
application's thread.

Same for timers, on some platforms they are preemptive while in others
they will be delayed. Soletta will never preempt user and all timers
will be queued, thus they may be delayed.

Idlers are tasks to be executed when nothing else is running. These
should be implemented by short-run functions as they will not be
preempted in any way, thus effectively delaying the dispatch of timers
and events. Often idlers are used to implement cooperative co-routines
that do a single step of a complex computation and return "true" so
they can run again. They can also be used by a worker thread to
schedule a function to be run from the main thread.

If some software have real-time requirements it's better to consider
writing native code to be as efficient as possible, usually as kernel
drivers or separate userspace service. This should be the case when
one lacks hardware PWM, in Linux it should be a kernel driver "soft
pwm" while in RIOT it would be a kernel thread.

While most of Soletta is not thread-safe, main loop functions
sol_timeout_add(), sol_timeout_del(), sol_idle_add(), sol_idle_del(),
sol_fd_add() and sol_fd_del() are thread-safe and can be used from
worker threads.


#### GLib (kconfig: Core library -> Mainloop -> glib)

The well known GMainLoop is used by Soletta so GLib-based frameworks
can be easily integrated, things like social network services,
multimedia systems and so on.

When using [GLib](https://developer.gnome.org/glib/stable/)
as platform you should also use their debug
infrastructure in addition to our common part.

See
[Running GLib Applications](https://developer.gnome.org/glib/stable/glib-running.html)
for in-depth information, the summary of environment variables to use:

        export G_DEBUG="all" # fatal-warnings, gc-friendly...
        export G_SLICE="all" # always-malloc, debug-blocks
        export G_MESSAGES_DEBUG="all" # print all debug

	make CFLAGS="-O0 -ggdb3" # disable optimizations

#### POSIX (kconfig: Core library -> Mainloop -> posix)

If GLib is too big then you can use a simpler implementation based
solely on POSIX syscalls (poll(2)/ppoll(2)). As it's fully implemented in
Soletta there are no extra variables to debug it.

## Platforms

Platforms is about target states and services.

Target states is what defines the current state such as initializing
(booting), degraded (running with some kind of problems), maintenance
(reset to factory defaults or rescue mode), stopping (shutting down)
or just running, that is the common state.

One can trigger a state change by setting a new target such as
"emergency", "reboot" or "poweroff", then the platform will be put in
such state. Some well-defined names exists, but it should be
extensible per-platform.

Services can be started or stopped dynamically and they will notify
monitors about their state changes. They are platform-specific and
depends on underlying platform setup.


#### Systemd (kconfig: Core library -> Target Platform -> systemd)

This uses systemd as base. Whenever a target is set or a service is
started it will call systemd. The D-Bus events from systemd will be
used to notify monitors.

Thus both targets and services are implemented according to systemd
using units in standard locations such as

        /usr/lib/systemd/system

and

        /etc/systemd/system

#### Linux-micro (kconfig: Core library -> Target Platform -> linux-micro)

Linux-micro implementation allows a Soletta binary to be used as
PID1, it will do required initialization and will handle services as
modules, so you can have a very small system that works.

Targets will be handled by calling Linux's reboot(2) or executing
binaries with well known target name such as "/sbin/rescue" or
"/sbin/emergency".

Services are handled as modules, they can be compiled into Soletta
by selecting them using config or menuconfig as builtin or as dynamically
loadable modules. They have a simple API and is extensible by third party,
so if Soletta lacks a service it easy to add that without patch Soletta
itself.

Soletta provides a init.d/rc.d compatibility service called rc-d.so,
the usage is to run scripts from /etc/init.d or /etc/rc.d with
standard parameters "start", "stop", "restart" and "status". To enable
a service all one needs to do is symlink the service name to rc-d.so,
as an example to enable /etc/init.d/myservice to be used by
Linux-micro:

        ln -s /usr/lib/soletta/modules/linux-micro/rc-d.so \
              /usr/lib/soletta/modules/linux-micro/myservice.so

A set of services are started automatically by Soletta in order to do
initialization, these are listed in

        /usr/lib/soletta/modules/linux-micro/initial-services

## Flow Based Programming

[Flow-Based Programming (**FBP**)](https://en.wikipedia.org/wiki/Flow-based_programming)
allows the programmer to express business
logic as a directional graph of nodes connected to type-specific
ports.

The connected node ports will exchange information packets (IP) that
will be used to run the flow. The packet can be either empty, used
just to trigger an action, or carry a value such as booleans,
integers, floats and even complex data types such as blobs. Ports can
decide if the packets they produce will be queued or replaced if they
were not consumed by target ports when a new packet is to be produced.

For those used to traditional Object Oriented Programming (OOP) it's
like using the observer pattern on source nodes, calling the
associated data getter which is then forwarded to target nodes by
calling their setter. However this is done by the core in an efficient
and safe way as no memory leaks, type mismatch or invalid memory
access can happen.

Node instances should be isolated and independent thus they can be
considered a blackbox that is only defined by their input and output
ports. They do not share state so it's easy to replace a node with
another without affecting the flow. For instance if a flow used a
gpio/reader as a source one could easily replace that by a bluetooth
presence sensor only by replacing that node given that they all have
the same ports.

Nodes can be created from builtin or third party types distributed as
shared objects (.so) stored at

        /usr/lib/soletta/modules/flow/

Builtin nodes include boolean logic,
converters, math, GPIO, Analog I/O, PWM, timers and even Soletta's
platform and services. External modules usually covers board or
operating system specific support such as udev or evdev for Linux,
calamari support for Intel's MinnowBoard extension shield. Node
descriptons can be found in JSON files stored at

        /usr/share/soletta/flow/descriptions/

One can query the node types
database using the Python tool sol-flow-node-type-find.py, as an
example say we want to query the nodes with at least the same ports as
gpio/writer (a single boolean OUT port):

        sol-flow-node-type-find.py --format=simple \
                --similar-ports=gpio/writer

Flow can be created directly in C using low-level primitives from
sol-flow.h or using a higher-level API in sol-flow-builder.h. An
alternative is to write the flow in a domain-specific
language—"FBP"<sup>[1](#footnote_01)</sup>—that is easier to express.
As an example imagine one wants to blink an LED linked to GPIO pin 123
every 200ms:

        MyTimer(timer:interval=200) OUT -> IN MyToggler(boolean/toggle)
        MyToggler OUT -> IN MyLED(gpio/writer:pin=123)

This can be ran directly with sol-fbp-runner or generate C code using
sol-fbp-generator. The syntax is pretty simple:

        Instance1(Type1) OUT_PORT_NAME -> IN_PORT_NAME Instance2(Type2)
        Instance1 OTHER_OUT_PORT -> IN_PORT_NAME Instance3(Type3)

The Type1 and Type2 should only be used once when first defining the
Instance1 or Instance2. They can be just the type name or give
constructor options by appending ":Option1=Value1,Option2=Value2". For
integer options one can specify only the value or the complete
information set by using "SomeIntOption=min:0|max:100|step:2|val:50"

One can link multiple segments at once:

        a out_port -> in_port b out_port -> in_port c

It is much simpler than a program using the high-level C API, even if
we're omitting error checking, as we can see below. For the low-level
API see the output of sol-fbp-generator.
```C
        #include "sol-mainloop.h"
        #include "sol-flow-builder.h"

        int main(void) {
            struct sol_flow_node_type *node_type;
            struct sol_flow_builder *builder;
            struct sol_flow_node *flow;
            struct sol_flow_node_type_timer_options timer_opts =
                SOL_FLOW_NODE_TYPE_TIMER_OPTIONS_DEFAULTS(.interval.val=200);
            struct sol_flow_node_type_gpio_writer_options gpio_opts =
                SOL_FLOW_NODE_TYPE_GPIO_WRITER_OPTIONS_DEFAULTS(.pin.val=123);

            sol_init();

            builder = sol_flow_builder_new();
            sol_flow_builder_add_node(builder, "MyTimer",
                                     SOL_FLOW_NODE_TYPE_TIMER,
                                     &timer_opts.base);
            sol_flow_builder_add_node(builder, "MyToggler",
                                     SOL_FLOW_NODE_TYPE_BOOLEAN_TOGGLE,
                                     NULL);
            sol_flow_builder_add_node(builder, "MyLED",
                                     SOL_FLOW_NODE_TYPE_GPIO_WRITER,
                                     &gpio_opts.base);

            sol_flow_builder_connect(builder,
                                    "MyTimer", "OUT", "MyToggler", "IN");
            sol_flow_builder_connect(builder,
                                    "MyToggler", "OUT", "MyLED", "IN");


            node_type = sol_flow_builder_get_node_type(builder);
            flow = sol_flow_node_new(NULL, "simple", node_type, NULL);

            sol_run();

            sol_flow_node_del(flow);
            sol_flow_builder_del(builder);
            sol_shutdown();
            return 0;
        }
```

## Contributing

When submitting code to this project, please indicate that you certify you are able to contribute the code by adding a signed-off-by line at the end of your commit message (using your real name) such as:

Signed-off-by: Random J Developer <random@developer.example.org>

This indicates that your contribution abides to the following rules:

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.
660 York Street, Suite 102,
San Francisco, CA 94110 USA

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

<a name="footnote_01">1</a>: Soletta expects that FBP files are
utf-8-encoded
