# Step 0 - Overview and environment setup

In this tutorial, we'll show how to develop a simple application using
Soletta. Although simple, this application will show some important
features of Soletta, like OIC and HTTP communication, besides GPIO
interaction.
Initially, we are going to run the application on desktop [[1](#footnote_01)], but
later we're going to show how to run it on devices, such as [MinnowBoard
MAX](http://wiki.minnowboard.org/MinnowBoard_Wiki_Home).

## Setting up Soletta

Soletta must be installed on developer's computer. One can use a package
provided for his or her distro, or [build Soletta](https://github.com/solettaproject/soletta/wiki/How-to-start).
One can also run Soletta on an [Ostro](https://ostroproject.org/) image
on supported device. Ostro [quick start](https://ostroproject.org/documentation/quick_start/quick_start.html)
is a very valuable guide on how to get Ostro running.
It's also possible to [run Ostro on a virtual machine](https://ostroproject.org/documentation/howtos/booting-and-installation.html#running-ostro-os-in-a-virtualbox-vm).

## The application we want

Before start coding, we must define the application we want to build.
Imagine one of these cool lights in which you do not only control if
its on or off, but also its intensity. Let's write an application for
that, so we can control remotely its features. In this sample we will
talk with this hypothetical light, but it's easy to imagine how it could
control a variety of devices, like fans, water pumps, etc.
Next figure shows an overview of what we expect:

![alt tag](diagram1.png)

We are going to have a server, which controls the light itself. It also
persist current settings, so they can be restored after a power outage.
This server also exposes two control interfaces: HTTP and [OIC](http://openinterconnect.org/).
We will also provide two controllers, one that connects to HTTP interface
and another for OIC interface.

[Next](../step1/tutorial.md), we are going to start our HTTP server and Flow Based Programming.

<a name="footnote_01">[1]</a> On this tutorial, 'desktop' refers to the machine a developer
is using to develop application. In opposition, 'device' is used
to refer to a small low-end device, like a MinnowBoard MAX [2].

[2] We're aware that MinnowBoard MAX is not so 'small low-end' =D

