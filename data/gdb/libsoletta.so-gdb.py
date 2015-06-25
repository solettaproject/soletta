# This file is part of the Soletta Project
#
# Copyright (C) 2015 Intel Corporation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#   * Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in
#     the documentation and/or other materials provided with the
#     distribution.
#   * Neither the name of Intel Corporation nor the names of its
#     contributors may be used to endorse or promote products derived
#     from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import gdb
import re

## IMPORTANT NOTE:
#
# This file is a Python GDB script that is highly dependent on
# symbol names, even the internal functions and parameters.
#
# Whenever depending on a symbol, mark them in the source file
# so people know they have to adapt this file on changes.

## LOADING:
#
# This file should be auto-loaded by gdb if it is installed in GDB's
# auto-load directory and matches the installed libsoletta.so,
# including the final so-version.
#
# If soletta is installed to custom directory, then make sure GDB knows
# about this location and that the directory is marked as safe-path:
#
# (gdb) add-auto-load-scripts-directory ${soletta_prefix}/share/gdb/auto-load
# (gdb) add-auto-load-safe-path ${soletta_prefix}/share/gdb/auto-load
#
# It may be included directly if not auto-loaded:
#
# (gdb) source ${soletta_prefix}/share/gdb/auto-load/libsoletta.so-gdb.py
#

## Usage:
# commands start with 'sol_' prefix, then you can use 'apropos ^sol_' to
# filter commands in our namespace or tabl-completion.
# GDB's "help command" to get more information

defvalue_member_map = {
    "string": "s",
    "byte": "byte",
    "boolean": "b",
    "int": "i",
    "float": "f",
    "rgb": "rgb",
    "vector_3f": "vector_3f",
    }

def get_type_description(type):
    try:
        tdesc = type["description"]
        if tdesc:
            return tdesc.dereference()
    except KeyError:
        pass
    return None


def get_node_type_description(node):
    type = node["type"]
    return get_type_description(type)


def _get_node_port_index_by_name(node, member, port_name):
    tdesc = get_node_type_description(node)
    if not tdesc:
        return -1
    array = tdesc[member]
    if not array:
        return -1
    i = 0
    while array[i]:
        port = array[i]
        if port["name"] and port["name"].string() == port_name:
            return i
        i += 1
    return -1


def get_node_port_out_index_by_name(node, port_name):
    return _get_node_port_index_by_name(node, "ports_out", port_name)


def get_node_port_in_index_by_name(node, port_name):
    return _get_node_port_index_by_name(node, "ports_in", port_name)


def _get_node_port_name_by_index(node, member, port_index):
    tdesc = get_node_type_description(node)
    if not tdesc:
        return None
    array = tdesc[member]
    if not array:
        return None
    i = 0
    while array[i]:
        if i == port_index:
            port = array[i]
            if port["name"]:
                return port["name"].string()
            return None
        elif i > port_index:
            break
        i += 1
    return None


def get_node_port_out_name_by_index(node, port_index):
    return _get_node_port_name_by_index(node, "ports_out", port_index)


def get_node_port_in_name_by_index(node, port_index):
    return _get_node_port_name_by_index(node, "ports_in", port_index)


class FlowTypePrinter(object):
    "Print a 'struct sol_flow_node_type'"

    def __init__(self, val):
        self.val = val
        self.port_in_type = gdb.lookup_type("struct sol_flow_port_type_in").const().pointer()

    def display_hint(self):
        return 'sol_flow_node_type'

    def _port_description_to_string(self, index, port, port_type):
        s = ("\n    %d %s (%s)\n" \
             "        description: %s\n") % (
                 index,
                 port["name"].string(),
                 port["data_type"].string(),
                 port["description"].string())
        if port_type["connect"]:
            s += "        connect(): %s\n" % (port_type["connect"],)
        if port_type["disconnect"]:
            s += "        disconnect(): %s\n" % (port_type["disconnect"],)
        if port_type.type == self.port_in_type and port_type["process"]:
            s += "        process(): %s\n" % (port_type["process"],)
        return s

    def _option_description_to_string(self, option):
        data_type = option["data_type"].string()
        defvalue_member = defvalue_member_map.get(data_type)
        if not defvalue_member:
            defvalue = ""
        else:
            defvalue = option["defvalue"][defvalue_member]
            if data_type == "string":
                if defvalue:
                    defvalue = defvalue.string()
                else:
                    defvalue = "NULL"

            defvalue = " (default=%s)" % (defvalue,)

        return "\n    %s(%s) \"%s\"%s," % (
            option["name"].string(),
            data_type,
            option["description"].string(),
            defvalue)


    def _ports_description_to_string(self, array, get_port_type):
        if not array:
            return ""
        i = 0
        r = []
        while array[i]:
            port_type = get_port_type(i)
            r.append(self._port_description_to_string(i, array[i], port_type))
            i += 1
        if i > 0:
            r.append("\n  ")
        return "".join(r)

    def _options_description_to_string(self, opts):
        if not opts:
            return ""
        opts = opts.dereference()
        array = opts["members"]
        if not array:
            return ""
        i = 0
        r = []
        while array[i]["name"]:
            r.append(self._option_description_to_string(array[i]))
            i += 1
        if i > 0:
            r.append("\n  ")
        return "".join(r)

    def to_string(self):
        type = self.val
        tdesc = get_type_description(type)
        if tdesc:
            get_port_in = gdb.parse_and_eval("sol_flow_node_type_get_port_in")
            get_port_out = gdb.parse_and_eval("sol_flow_node_type_get_port_out")
            p_type = type.address
            ports_in = self._ports_description_to_string(tdesc["ports_in"], lambda idx: get_port_in(p_type, idx))
            ports_out = self._ports_description_to_string(tdesc["ports_out"], lambda idx: get_port_out(p_type, idx))
            options = self._options_description_to_string(tdesc["options"])

            return "%s=%s" \
                "\n  name=\"%s\"," \
                "\n  category=\"%s\"," \
                "\n  description=\"%s\"," \
                "\n  ports_in={%s}," \
                "\n  ports_out={%s}," \
                "\n  options={%s})" % (
                    tdesc["symbol"].string(),
                    type.address,
                    tdesc["name"].string(),
                    tdesc["category"].string(),
                    tdesc["description"].string(),
                    ports_in,
                    ports_out,
                    options)

        return "(struct sol_flow_node_type)%s (no node type description)" % (type.address,)


class FlowPrinter(object):
    "Print a 'struct sol_flow_node'"

    def __init__(self, val):
        self.val = val

    def display_hint(self):
        return 'sol_flow_node'

    def to_string(self):
        id = self.val["id"]
        type = self.val["type"]
        if not type:
            return "sol_flow_node(%s) is under construction." % (
                self.val.address,)
        tname = "%#x (no node type description)" % (type.address,)
        tdesc = get_type_description(type)
        if tdesc:
            tname = "%s(%s=%s)" % (
                tdesc["name"].string(),
                tdesc["symbol"].string(),
                type.address)

        return "sol_flow_node(%s, id=\"%s\", type=%s)" % (
            self.val.address, id.string(), tname)


def sol_flow_pretty_printers(val):
    lookup_tag = val.type.tag
    if lookup_tag == "sol_flow_node":
        return FlowPrinter(val)
    elif lookup_tag == "sol_flow_node_type":
        return FlowTypePrinter(val)
    return None


def register_pretty_printers(objfile):
    gdb.pretty_printers.append(sol_flow_pretty_printers)


def get_type_options_string(type, options):
    if not options:
        return ""
    tdesc = get_type_description(type)
    if not tdesc or not tdesc["options"] or not tdesc["options"]["members"]:
        return "OPTIONS: %s (no node type description)\n" % (options,)

    string = ""
    opts_desc = tdesc["options"]
    array = opts_desc["members"]
    i = 0
    string += "OPTIONS: (struct %s*)%s\n" % (tdesc["options_symbol"].string(), options)
    opt_type = gdb.lookup_type("struct %s" % (tdesc["options_symbol"].string(),))
    options = options.cast(opt_type.pointer())
    while array[i]["name"]:
        m = array[i]
        name = m["name"].string()
        data_type = m["data_type"].string()
        description = m["description"].string()
        value = options[name]
        if data_type == "string":
            if value:
                value = value.string()
            else:
                value = "NULL"

        defvalue_member = defvalue_member_map.get(data_type)
        if not defvalue_member:
            defvalue = ""
        else:
            defvalue = m["defvalue"][defvalue_member]
            if data_type == "string":
                if defvalue:
                    defvalue = defvalue.string()
                else:
                    defvalue = "NULL"

            defvalue = " (default=%s)" % (defvalue,)

        string += "  %s (%s) = %s // %s%s\n" % (name, data_type, value, description, defvalue)
        i += 1
    string += "\n"
    return string


class InspectAndBreakIfMatches(gdb.Breakpoint):
    class InternalBreak(gdb.Breakpoint):
        def __init__(self, method, banner=None, matches=None, values=None):
            addr = "*%s" % (method.cast(gdb.lookup_type("long")),)
            self.method = method
            self.banner = banner
            self.matches = matches or {}
            self.values = values or {}
            gdb.Breakpoint.__init__(self, addr, gdb.BP_BREAKPOINT, internal=True, temporary=True)

        def stop(self):
            if self.banner:
                if callable(self.banner):
                    self.banner(self.matches, self.values)
                else:
                    gdb.write(self.banner)
            return True

    def __init__(self, spec, matches):
        gdb.Breakpoint.__init__(self, spec, gdb.BP_BREAKPOINT, internal=False)
        self.matches = {}
        for k, v in matches.items():
            self.matches[k] = get_str_or_regexp_match(v)

    def print_matches(self, values=None):
        gdb.write("%s matches:\n" % (self.__class__.__name__,), gdb.STDERR)
        if not values:
            values = {}
        for k, func in self.matches.items():
            v = values.get(k)
            if v is None:
                gdb.write("   %s = %s (no value provided)\n" % (k, func.__doc__), gdb.STDERR)
            else:
                try:
                    res = func(v)
                except Exception as e:
                    res = "Exception executing match: %s" % (e,)
                gdb.write("   %s = %s (value: '%s', match: %s)\n" %
                          (k, func.__doc__, v, res), gdb.STDERR)
        gdb.write("\n", gdb.STDERR)

    def get_values(self):
        raise NotImplemented()

    def stop(self):
        try:
            values = self.get_values()
        except Exception as e:
            gdb.write("Exception at %s.get_values(): %s\n" % (
                self.__class__.__name__, e), gdb.STDERR)
            return False
        if not values:
            gdb.write("%s.get_values() did not return values.\n" % (
                self.__class__.__name__,), gdb.STDERR)
            return False

        def print_values():
            gdb.write("Values:\n", gdb.STDERR)
            for k, v in values.items():
                gdb.write("   %s: %s\n" % (k, v), gdb.STDERR)
            gdb.write("\n", gdb.STDERR)

        for k, match_func in self.matches.items():
            try:
                v = values[k]
            except KeyError:
                gdb.write("%s.get_values() did not provide key '%s'.\n" % (
                    self.__class__.__name__, k), gdb.STDERR)
                self.print_matches(values)
                print_values()
                return False
            try:
                if not match_func(v):
                    return False
            except Exception as e:
                gdb.write("Exception at %s.stop() while matching %s %s (%s): %s\n" % (
                    self.__class__.__name__, k, v, match_func.__doc__, e,), gdb.STDERR)
                self.print_matches(values)
                return False

        method = values.get("method")
        banner = values.get("banner")
        if not method:
            node = values.get("node")
            if node:
                gdb.write("NODE: %s\n" % (node,), gdb.STDERR)
            gdb.write("%s did not return the internal method to break at.\n" % (
                self.__class__.__name__,), gdb.STDERR)
            self.print_matches(values)
            gdb.write("Breaking at the caller function %s\n" % (self.location,),
                      gdb.STDERR)
            return True

        def add_breakpoint():
            try:
                self.InternalBreak(method, banner, self.matches, values)
            except Exception as e:
                gdb.write("Could not add internal breakpoint: %s\n" % (e,), gdb.STDERR)
                self.print_matches(values)
        gdb.post_event(add_breakpoint)
        return False


def get_str_or_regexp_match(string):
    if not string:
        string = "/.*/"
    if len(string) > 2 and string.startswith("/") and string.endswith("/"):
        r = re.compile(string[1:-1])
        match = lambda x: bool(r.match(x))
    else:
        match = lambda x: string == x

    match.__doc__ = string
    return match


class FlowBreakOpen(InspectAndBreakIfMatches):
    def __init__(self, matches):
        InspectAndBreakIfMatches.__init__(self, "sol_flow_node_init", matches)

    def get_values(self):
        node_id = gdb.parse_and_eval("name")
        if node_id:
            node_id = node_id.string()
        type = gdb.parse_and_eval("type")
        method = type["open"]
        node = gdb.parse_and_eval("*node")
        options = gdb.parse_and_eval("options")
        def banner(matches, values):
            gdb.write("""\
Break before opening node:
FUNCTION: %s
NODE....: %s (filter: %s)
%s""" % (method, node,
         matches["node_id"].__doc__,
       get_type_options_string(node["type"], options)))
        return {
            "node": node,
            "node_id": node_id,
            "method": method,
            "banner": banner,
            }


class FlowBreakClose(InspectAndBreakIfMatches):
    def __init__(self, matches):
        InspectAndBreakIfMatches.__init__(self, "sol_flow_node_fini", matches)

    def get_values(self):
        node = gdb.parse_and_eval("*node")
        node_id = node["id"]
        if node_id:
            node_id = node_id.string()
        type = node["type"]
        method = type["close"]
        def banner(matches, values):
            gdb.write("""\
Break before closing node:
FUNCTION: %s
NODE....: %s (filter: %s)
""" % (method, node,
         matches["node_id"].__doc__))
        return {
            "node": node,
            "node_id": node_id,
            "method": method,
            "banner": banner,
            }


class FlowBreakSend(InspectAndBreakIfMatches):
    def __init__(self, matches):
        InspectAndBreakIfMatches.__init__(self, "inspector_will_send_packet", matches)

    def get_values(self):
        node = gdb.parse_and_eval("*src_node")
        port = gdb.parse_and_eval("src_port")
        packet = gdb.parse_and_eval("*packet")

        node_id = node["id"]
        if node_id:
            node_id = node_id.string()
        port_name = get_node_port_out_name_by_index(node, port)
        packet_type = packet["type"]["name"].string()

        type = gdb.parse_and_eval("(struct sol_flow_node_container_type *)src_node->parent->type")
        method = type["send"]

        def banner(matches, values):
            gdb.write("""\
Break before sending packet:
FUNCTION: %s
NODE....: %s (filter: %s)
PORT....: %s (index: %s, filter: %s)
PACKET..: %s (filter: %s)
""" % (
    method,
    node,
    matches["node_id"].__doc__,
    port_name,
    port,
    matches["port_name"].__doc__,
    packet,
    matches["packet_type"].__doc__))

        return {
            "node": node,
            "node_id": node_id,
            "port_name": port_name,
            "packet_type": packet_type,
            "method": method,
            "banner": banner,
            }


class FlowBreakProcess(InspectAndBreakIfMatches):
    def __init__(self, matches):
        InspectAndBreakIfMatches.__init__(self, "inspector_will_deliver_packet", matches)

    def get_values(self):
        node = gdb.parse_and_eval("*dst_node")
        port = gdb.parse_and_eval("dst_port")
        packet = gdb.parse_and_eval("*packet")

        node_id = node["id"]
        if node_id:
            node_id = node_id.string()
        port_name = get_node_port_in_name_by_index(node, port)
        packet_type = packet["type"]["name"].string()

        get_port_in = gdb.parse_and_eval("sol_flow_node_type_get_port_in")
        type = node["type"]
        port_type = get_port_in(type, port)
        if not port_type:
            method = None
        else:
            method = port_type["process"]

        def banner(matches, values):
            gdb.write("""\
Break before processing packet:
FUNCTION: %s
NODE....: %s (filter: %s)
PORT....: %s (index: %s, filter: %s)
PACKET..: %s (filter: %s)
""" % (
    method,
    node,
    matches["node_id"].__doc__,
    port_name,
    port,
    matches["port_name"].__doc__,
    packet,
    matches["packet_type"].__doc__))

        return {
            "node": node,
            "node_id": node_id,
            "port_name": port_name,
            "packet_type": packet_type,
            "method": method,
            "banner": banner,
            }

class FlowCommand(gdb.Command):
    "Commands to operate with 'sol_flow'"

    def __init__(self):
        gdb.Command.__init__(self, "sol_flow", gdb.COMMAND_USER, gdb.COMPLETE_COMMAND, True)

    def invoke(self, arg, from_tty):
        raise gdb.GdbError("missing sub-command: break or print")


class FlowBreakCommand(gdb.Command):
    "Add an execution break when sol_flow events happen."

    def __init__(self):
        gdb.Command.__init__(self, "sol_flow break", gdb.COMMAND_BREAKPOINTS, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        raise gdb.GdbError("missing sub-command: open, close, send or process")


class FlowBreakFilterBaseCommand(gdb.Command):
    """Base command for 'sol_flow break' subcommands.

    The subcommand will be registered and will take matches as list of
    optional arguments. If not available then None is assumed. These
    parameters will be sent to breakpoint in order.

    """
    def __init__(self, subcommand, matches, breakpoint):
        gdb.Command.__init__(self, "sol_flow break " + subcommand, gdb.COMMAND_BREAKPOINTS, gdb.COMPLETE_SYMBOL, True)
        self.matches = matches
        self.breakpoint = breakpoint

    def invoke(self, arg, from_tty):
        arg =  gdb.string_to_argv(arg)
        params = {}
        for i, name in enumerate(self.matches):
            if len(arg) > i:
                p = arg[i]
            else:
                p = None
            params[name] = p
        self.breakpoint(params)
        self.dont_repeat()


class FlowBreakOpenCommand(FlowBreakFilterBaseCommand):
    """Add an execution break when sol_flow_node is created (type->open).

    Arguments: node_id

    node_id may be an exact string or a regular expression if enclosed
    in "//". Examples:

    sol_flow break open timer
        will break on nodes with id "timer" (exact match)

    sol_flow break open /^timer.*$/
        will break on nodes with id that matches regular expression
        "^timer.*$" (starts with "timer")
    """

    def __init__(self):
        matches = ["node_id"]
        FlowBreakFilterBaseCommand.__init__(self, "open", matches, FlowBreakOpen)


class FlowBreakCloseCommand(FlowBreakFilterBaseCommand):
    """Add an execution break when sol_flow_node is destroyed (type->close).

    Arguments: node_id

    node_id may be an exact string or a regular expression if enclosed
    in "//". Examples:

    sol_flow break close timer
        will break on nodes with id "timer" (exact match)

    sol_flow break close /^timer.*$/
        will break on nodes with id that matches regular expression
        "^timer.*$" (starts with "timer")
    """

    def __init__(self):
        matches = ["node_id"]
        FlowBreakFilterBaseCommand.__init__(self, "close", matches, FlowBreakClose)


class FlowBreakSendCommand(FlowBreakFilterBaseCommand):
    """Add an execution break when sol_flow_node sends a packet on its output port.

    Arguments: node_id port_name packet_type

    Each argument is optional and may be a string or a regular
    expression if enclosed in "//". If omitted the regular expression
    /.*/ is assumed, matching all patterns.

    """

    def __init__(self):
        matches = ["node_id", "port_name", "packet_type"]
        FlowBreakFilterBaseCommand.__init__(self, "send", matches, FlowBreakSend)


class FlowBreakProcessCommand(FlowBreakFilterBaseCommand):
    """Add an execution break when sol_flow_node will receive a packet on its input port (port's process()).

    Arguments: node_id port_name packet_type

    Each argument is optional and may be a string or a regular
    expression if enclosed in "//". If omitted the regular expression
    /.*/ is assumed, matching all patterns.
    """

    def __init__(self):
        matches = ["node_id", "port_name", "packet_type"]
        FlowBreakFilterBaseCommand.__init__(self, "process", matches, FlowBreakProcess)


class FlowPrintCommand(gdb.Command):
    "Print sol_flow types"

    def __init__(self):
        gdb.Command.__init__(self, "sol_flow print", gdb.COMMAND_BREAKPOINTS, gdb.COMPLETE_COMMAND, True)

    def invoke(self, arg, from_tty):
        raise gdb.GdbError("missing sub-command: type, port or options")


def get_node_type_from_exp(arg):
    node = gdb.parse_and_eval(arg)
    if not node:
        raise gdb.GdbError("invalid node: %s" % (arg,))

    gt = node.type.unqualified()
    sol_flow_node_type = gdb.lookup_type("struct sol_flow_node")
    sol_flow_node_type_type = gdb.lookup_type("struct sol_flow_node_type")
    if gt == sol_flow_node_type or gt == sol_flow_node_type.pointer() or \
       gt == sol_flow_node_type.const().pointer():
        return node["type"]
    elif gt == sol_flow_node_type_type or gt == sol_flow_node_type_type.pointer() or \
         gt == sol_flow_node_type_type.const().pointer():
        return node
    else:
        raise gdb.GdbError("invalid node: %s" % (arg,))


class FlowPrintTypeCommand(gdb.Command):
    """Prints the type information for the given 'struct sol_flow_node'.

    Arguments: node
    """
    def __init__(self):
        gdb.Command.__init__(self, "sol_flow print type", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        arg =  gdb.string_to_argv(arg)
        if len(arg) < 1:
            raise gdb.GdbError("missing pointer to struct sol_flow_node")
        type = get_node_type_from_exp(arg[0])
        gdb.write("%s\n" % (type.dereference(),))


class FlowPrintPortCommand(gdb.Command):
    """Prints the port information for the given node.

    Arguments: node [direction] [filter_type] [filter_specifier]

    node is the pointer to node where to find the port.

    direction may be 'in', 'out' or 'both'. If omitted, both will be
    assumed. May be omitted and 'both' is used.

    filter_type may be 'all', 'number' or 'name'. If omitted, all
    will be assumed.

    If filter_type is 'number', then filter_specifier must be an integer.

    If filter_type is 'name', then filter_specifier must be a string
    or a regular expression enclosed in "//".

    If filter_type is omitted, then it's gussed from filter_specifier.
    """
    def __init__(self):
        gdb.Command.__init__(self, "sol_flow print port", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def _print_ports(self, type, tdesc, member, filter):
        array = tdesc[member]
        if not array:
            return
        did = 0
        i = 0
        if member == "ports_in":
            get_port_type = gdb.parse_and_eval("sol_flow_node_type_get_port_in")
        else:
            get_port_type = gdb.parse_and_eval("sol_flow_node_type_get_port_out")
        while array[i]:
            port = array[i]
            if filter["type"] == "all" or \
                (filter["type"] == "number" and filter["number"] == i) or \
                (filter["type"] == "name" and filter["name"](port["name"].string())):
                if did == 0:
                    gdb.write("%s:\n" % member)
                did += 1
                gdb.write("  %d: %s (%s)\n    description: %s\n" % (
                    i,
                    port["name"].string(),
                    port["data_type"].string(),
                    port["description"].string(),
                    ))
                port_type = get_port_type(type, i)
                if port_type["connect"]:
                    gdb.write("    connect(): %s\n" % (port_type["connect"],))
                if port_type["disconnect"]:
                    gdb.write("    disconnect(): %s\n" % (port_type["disconnect"],))
                if member == "ports_in" and port_type["process"]:
                    gdb.write("    process(): %s\n" % (port_type["process"],))
                gdb.write("\n")
            i += 1

    def invoke(self, arg, from_tty):
        arg =  gdb.string_to_argv(arg)
        if len(arg) < 1:
            raise gdb.GdbError("missing pointer to struct sol_flow_node")

        direction = "both"
        filter = {"type": "all"}
        if len(arg) > 1:
            direction = arg[1]
            if direction not in ("both", "in", "out"):
                direction = "both"
                try:
                    filter["number"] = int(arg[1])
                    filter["type"] = "number"
                except ValueError:
                    filter["name"] = get_str_or_regexp_match(arg[1])
                    filter["type"] = "name"

        if len(arg) > 2:
            filter["type"] = arg[2]
            if filter["type"] not in ("all", "number", "name"):
                try:
                    filter["number"] = int(arg[2])
                    filter["type"] = "number"
                except ValueError:
                    filter["name"] = get_str_or_regexp_match(arg[2])
                    filter["type"] = "name"
            elif filter["type"] == 'number':
                if len(arg) < 4:
                    raise gdb.GdbError("missing port number to filter")
                filter["number"] = int(arg[3])
            elif filter["type"] == 'name':
                if len(arg) < 4:
                    raise gdb.GdbError("missing port name to filter")
                filter["name"] = get_str_or_regexp_match(arg[3])

        type = get_node_type_from_exp(arg[0])
        tdesc = get_type_description(type)
        if not tdesc:
            gdb.write("no node type description\n")
            return

        if direction == "both" or direction == "in":
            self._print_ports(type, tdesc, "ports_in", filter)
        if direction == "both" or direction == "out":
            self._print_ports(type, tdesc, "ports_out", filter)


class FlowPrintOptionsCommand(gdb.Command):
    """Prints the options used to open the given node.

    Arguments: node options

    node is the pointer to node where to find the port.

    options is the pointer to options to open to given node.
    """
    def __init__(self):
        gdb.Command.__init__(self, "sol_flow print options", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        arg =  gdb.string_to_argv(arg)
        if len(arg) != 2:
            raise gdb.GdbError("Usage: sol_flow print options <node> <options>")
        type = get_node_type_from_exp(arg[0])
        options = gdb.parse_and_eval(arg[1])
        gdb.write(get_type_options_string(type, options))


FlowCommand()
FlowBreakCommand()
FlowBreakOpenCommand()
FlowBreakCloseCommand()
FlowBreakSendCommand()
FlowBreakProcessCommand()
FlowPrintCommand()
FlowPrintTypeCommand()
FlowPrintPortCommand()
FlowPrintOptionsCommand()

register_pretty_printers(gdb.current_objfile())
