/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "sol-flow-packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta flows manipulation.
 */

/**
 * @defgroup Flow Flow Based Programming
 *
 * The flow system consists of a series of connected nodes that send
 * packets to each other via ports. Each node may have multiple
 * input/output ports. It is responsibility of the parent (container)
 * node to deliver the packets sent by its children nodes (one thing
 * the "static flow" node, returned by sol_flow_static_new(), already
 * does).
 *
 * @{
 */

struct sol_flow_node_options;
struct sol_flow_node_type;
struct sol_flow_packet;
struct sol_flow_packet_type;
struct sol_flow_port;



/* A node is an entity that has input/output ports. Its operations are
 * described by a node type, so that the node can be seen as a class
 * instance, being the node type the class.
 *
 * Nodes receive packets in their input ports and can send packets to
 * their output ports.
 */
struct sol_flow_node;

/**
 * Creates a new node.
 *
 * Nodes should be created in the sol_main_callbacks::startup
 * function, and at least the root one must be a "static flow" node.
 * (@see sol_flow_static_new(), sol_flow_builder_get_node_type() and
 * sol_flow_static_new_type()).
 *
 * @param parent The parent node. Pass @c NULL if you're creating the
 *               root node of the flow.
 * @param id A string to identify the node
 * @param type The type of the node
 * @param options Options to individually parametrize the type
 *                instance (@see
 *                sol_flow_node_options_new_from_strv())
 *
 * @return A new node instance on success, otherwise @c NULL.
 */
struct sol_flow_node *sol_flow_node_new(struct sol_flow_node *parent, const char *id, const struct sol_flow_node_type *type, const struct sol_flow_node_options *options);

/**
 * Deletes a node.
 *
 * The root node should be deleted in the sol_main_callbacks::shutdown
 * function -- it will take care of recursively deleting its children
 * nodes.
 *
 * @param node The node to be deleted
 */
void sol_flow_node_del(struct sol_flow_node *node);

/* Private data is allocated by the system for each node, with size
 * described by the node types' @c data_size attribute. */
void *sol_flow_node_get_private_data(const struct sol_flow_node *node);

/* Retrieve the ID strings given to the node on sol_flow_node_new() */
const char *sol_flow_node_get_id(const struct sol_flow_node *node);

/**
 * Get a node's parent.
 *
 * @param node The node to get the parent from
 *
 * @return The parent node of @a node or @c NULL, if @a node is the
 *         top/root node.
 */
const struct sol_flow_node *sol_flow_node_get_parent(const struct sol_flow_node *node);

/**
 * Send a packet from a given node to one of its output ports.
 *
 * The parent node of @a src will take care of routing the packet to
 * the appropriated input ports of other nodes.
 *
 * This function takes the ownership of the packet, becoming
 * responsible to release its memory.
 *
 * @param src The node that is to output a packet
 * @param src_port The port where the packet will be output
 * @param packet The packet to output
 *
 * @note There are helper functions that already create a packet of a given
 *       type and send it, e. g. sol_flow_send_boolean_packet().
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_packet(struct sol_flow_node *src, uint16_t src_port, struct sol_flow_packet *packet);

/* Works exaclty as @sol_flow_send_packet(). This function is a helper
 * to send an error packet.
 */
int sol_flow_send_error_packet(struct sol_flow_node *src, int code, const char *msg_fmt, ...) SOL_ATTR_PRINTF(3, 4);

int sol_flow_send_error_packet_errno(struct sol_flow_node *src, int code);

int sol_flow_send_error_packet_str(struct sol_flow_node *src, int code, const char *str);

/* Helper functions to create and send packets of specific types. They
 * work like sol_flow_send_packet(), but besides creating, sending and
 * freeing the packet, they'll also set their value.
 */
int sol_flow_send_boolean_packet(struct sol_flow_node *src, uint16_t src_port, unsigned char value);

int sol_flow_send_blob_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value);

int sol_flow_send_json_object_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value);

int sol_flow_send_json_array_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value);

int sol_flow_send_byte_packet(struct sol_flow_node *src, uint16_t src_port, unsigned char value);

int sol_flow_send_drange_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_drange *value);
int sol_flow_send_drange_value_packet(struct sol_flow_node *src, uint16_t src_port, double value);

int sol_flow_send_rgb_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_rgb *value);
int sol_flow_send_rgb_components_packet(struct sol_flow_node *src, uint16_t src_port, uint32_t red, uint32_t green, uint32_t blue);

int sol_flow_send_direction_vector_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_direction_vector *value);
int sol_flow_send_direction_vector_components_packet(struct sol_flow_node *src, uint16_t src_port, double x, double y, double z);

int sol_flow_send_location_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_location *value);

int sol_flow_send_timestamp_packet(struct sol_flow_node *src, uint16_t src_port, const struct timespec *value);

int sol_flow_send_empty_packet(struct sol_flow_node *src, uint16_t src_port);

int sol_flow_send_irange_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_irange *value);
int sol_flow_send_irange_value_packet(struct sol_flow_node *src, uint16_t src_port, int32_t value);

int sol_flow_send_string_packet(struct sol_flow_node *src, uint16_t src_port, const char *value);
int sol_flow_send_string_slice_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_str_slice value);

int sol_flow_send_string_take_packet(struct sol_flow_node *src, uint16_t src_port, char *value);

int sol_flow_send_composed_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_flow_packet_type *composed_type, struct sol_flow_packet **children);
int sol_flow_send_http_response_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_http_response_type *value);

/**
 * Get a node's type.
 *
 * @param node The node get the type from
 *
 * @return The node type of @a node or @c NULL, on errors
 */
const struct sol_flow_node_type *sol_flow_node_get_type(const struct sol_flow_node *node);

struct sol_flow_node_options {
#define SOL_FLOW_NODE_OPTIONS_API_VERSION (1) /**< compile time API version to be checked during runtime */
    uint16_t api_version; /**< must match SOL_FLOW_NODE_OPTIONS_API_VERSION at runtime */
    uint16_t sub_api; /**< to version each subclass */
};

enum sol_flow_node_options_member_type {
    SOL_FLOW_NODE_OPTIONS_MEMBER_UNKNOWN,
    SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN,
    SOL_FLOW_NODE_OPTIONS_MEMBER_BYTE,
    SOL_FLOW_NODE_OPTIONS_MEMBER_IRANGE,
    SOL_FLOW_NODE_OPTIONS_MEMBER_DRANGE,
    SOL_FLOW_NODE_OPTIONS_MEMBER_RGB,
    SOL_FLOW_NODE_OPTIONS_MEMBER_DIRECTION_VECTOR,
    SOL_FLOW_NODE_OPTIONS_MEMBER_STRING,
};

const char *sol_flow_node_options_member_type_to_string(enum sol_flow_node_options_member_type type);
enum sol_flow_node_options_member_type sol_flow_node_options_member_type_from_string(const char *data_type);

struct sol_flow_node_named_options_member {
    const char *name;
    enum sol_flow_node_options_member_type type;
    union {
        bool boolean;
        unsigned char byte;
        struct sol_irange irange;
        struct sol_drange drange;
        struct sol_rgb rgb;
        struct sol_direction_vector direction_vector;
        const char *string;
    };
};

struct sol_flow_node_named_options {
    struct sol_flow_node_named_options_member *members;
    uint16_t count;
};

int sol_flow_node_options_new(
    const struct sol_flow_node_type *type,
    const struct sol_flow_node_named_options *named_opts,
    struct sol_flow_node_options **out_opts);

int sol_flow_node_named_options_init_from_strv(
    struct sol_flow_node_named_options *named_opts,
    const struct sol_flow_node_type *type,
    const char *const *strv);

/*
 * Duplicate an options handle.
 */
struct sol_flow_node_options *sol_flow_node_options_copy(const struct sol_flow_node_type *type, const struct sol_flow_node_options *opts);

/*
 * Delete an options handle.
 */
void sol_flow_node_options_del(const struct sol_flow_node_type *type, struct sol_flow_node_options *options);

/*
 * Delete a key-value options array (see @sol_flow_resolve_strv()).
 */
void sol_flow_node_options_strv_del(char **opts_strv);

void sol_flow_node_named_options_fini(struct sol_flow_node_named_options *named_opts);

#include "sol-flow-buildopts.h"

#define SOL_FLOW_NODE_PORT_ERROR_NAME ("ERROR")

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
/* A node type description provides more information about a node
 * type, such as textual description, name, URL, version, author as
 * well as ports and options meta information.
 */

struct sol_flow_port_description {
    const char *name; /**< port's name */
    const char *description; /**< port's description */
    const char *data_type; /**< textual representation of the port's accepted packet data type(s), e. g. "int" */
    uint16_t array_size; /**< Size of array for array ports, or 0 for single ports */
    uint16_t base_port_idx; /**< For array ports, the port number where the array begins */
    bool required; /**< whether at least one connection has to be made on this port in order to the node to function (Soletta does not check that at runtime, this is mostly a hint for visual editors that can output flows/code from visual representations of a flow) */
};

struct sol_flow_node_options_member_description {
    const char *name; /**< option member's name */
    const char *description; /**< option member's description */
    const char *data_type; /**< textual representation of the options data type(s), e. g. "int" */
    union {
        bool b; /**< option member's default boolean value */
        unsigned char byte; /**< option member's default byte value */
        struct sol_irange i; /**< option member's default integer range value */
        struct sol_drange f; /**< option member's default float range value */
        struct sol_direction_vector direction_vector; /**< option member's default direction vector value */
        struct sol_rgb rgb; /**< option member's default RGB value */
        const char *s; /**< option member's default string value */
        const void *ptr; /**< option member's default "blob" value */
    } defvalue; /**< option member's default value, according to its #data_type */
    uint16_t offset; /**< option member's offset inside the final options blob for a node */
    uint16_t size; /**< option member's size inside the final options blob for a node */
    bool required; /**< whether the option member is mandatory or not when creating a node */
};

struct sol_flow_node_options_description {
    const struct sol_flow_node_options_member_description *members; /** @c NULL terminated */
    uint16_t data_size; /**< size of the whole sol_flow_node_options derivate */
    uint16_t sub_api; /**< what goes in sol_flow_node_options::sub_api */
    bool required; /**< if true then options must be given for the node (if not, the node has no parameters) */
};

struct sol_flow_node_type_description {
    /* both sol_flow_node_type_description, sol_flow_port_description
     * and sol_flow_node_options_description are subject to
     * SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION, then whenever one of
     * these structures are changed, the version number should be
     * incremented.
     */
#define SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION (1)
    unsigned long api_version;
    const char *name; /**< mandatory, the user-visible name */
    const char *category; /**< mandatory, convention is: category/subcategory/..., such as input/hw/sensor for a pressure sensor or input/sw/oic/switch for an OIC compliant on/off switch */
    const char *symbol; /**< the symbol that exports this type, useful to code that generates C code.  */
    const char *options_symbol; /**< the options symbol that exports this options type, useful to code that generates C code.  */
    const char *description; /**< description for a node */
    const char *author; /**< node's author */
    const char *url; /**< node author/vendor's URL */
    const char *license; /**< node's license */
    const char *version; /**< version string */
    const struct sol_flow_port_description *const *ports_in; /**< @NULL terminated input ports array */
    const struct sol_flow_port_description *const *ports_out; /**< @NULL terminated output ports array */
    const struct sol_flow_node_options_description *options; /**< node options */
};
#endif

/* A node type describes a node. This description is usually defined
 * as const static and shared by many different nodes.
 */

enum sol_flow_node_type_flags {
    SOL_FLOW_NODE_TYPE_FLAGS_CONTAINER = (1 << 0), /**< flag the node as being a container one (a "static flow" node is an example) */
};

struct sol_flow_node_type {
#define SOL_FLOW_NODE_PORT_ERROR (UINT16_MAX - 1) /**< built-in output port's number, common to every node, meant to output error packets */
#define SOL_FLOW_NODE_TYPE_API_VERSION (1) /**< compile time API version to be checked during runtime */
    uint16_t api_version; /**< must match SOL_FLOW_NODE_TYPE_API_VERSION at runtime */
    uint16_t data_size; /**< size of the whole sol_flow_node_type derivate */
    uint16_t options_size;
    uint16_t flags; /**< @see #sol_flow_node_type_flags */

    const void *type_data; /**< pointer to per-type user data */
    const void *default_options;

    uint16_t ports_in_count;
    uint16_t ports_out_count;

    const struct sol_flow_port_type_in *(*get_port_in)(const struct sol_flow_node_type *type, uint16_t port); /**< member function to get the array of the node's input ports */
    const struct sol_flow_port_type_out *(*get_port_out)(const struct sol_flow_node_type *type, uint16_t port); /**< member function to get the array of the node's output ports */

    int (*open)(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options); /**< member function to instantiate the node */
    void (*close)(struct sol_flow_node *node, void *data); /**< member function to delete the node */

    void (*init_type)(void); /**< member function called at least once for each node type, that allows initialization of node-specific data (packet types, logging domains, etc) */

    /** Called as part of sol_flow_node_type_del() to dispose any
     * extra resources associated with the node type. */
    void (*dispose_type)(struct sol_flow_node_type *type);

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    const struct sol_flow_node_type_description *description; /**< pointer to node's description */
#endif
};

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
int sol_flow_node_named_options_parse_member(
    struct sol_flow_node_named_options_member *m,
    const char *value,
    const struct sol_flow_node_options_member_description *mdesc);
#endif

/**
 * Get a node type's input port definiton struct, given a port index.
 *
 * @param type The node type to get a port definiton from
 * @param port The port's index to retrieve
 * @return The input port's definition struct or @c NULL, on errors.
 *
 */
const struct sol_flow_port_type_in *sol_flow_node_type_get_port_in(const struct sol_flow_node_type *type, uint16_t port);

/**
 * Get a node type's output port definiton struct, given a port index.
 *
 * @param type The node type to get a port definiton from
 * @param port The port's index to retrieve
 * @return The output port's definition struct or @c NULL, on errors.
 *
 */
const struct sol_flow_port_type_out *sol_flow_node_type_get_port_out(const struct sol_flow_node_type *type, uint16_t port);

/**
 * Delete a node type. It should be used only for types dynamically
 * created and returned by functions like sol_flow_static_new_type().
 */
void sol_flow_node_type_del(struct sol_flow_node_type *type);

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
/**
 * Iterator on all node types that were compiled as built-in.
 *
 * @param cb The callback function to be issued on each built-in node
 *           type found
 * @param data The user data to forward to @a cb
 *
 */
void sol_flow_foreach_builtin_node_type(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data);

/**
 * Get the port description associated with a given input port index.
 *
 * @param type The node type to get a port description from
 * @param port The port index
 * @return The port description for the given port
 */
const struct sol_flow_port_description *sol_flow_node_get_port_in_description(const struct sol_flow_node_type *type, uint16_t port);

/**
 * Get the port description associated with a given output port index.
 *
 * @param type The node type to get a port description from
 * @param port The port index
 * @return The port description for the given port
 */
const struct sol_flow_port_description *sol_flow_node_get_port_out_description(const struct sol_flow_node_type *type, uint16_t port);
#endif

/* When a node type is a container (i.e. may act as parent of other
 * nodes), it should provide extra operations. This is the case of the
 * "static flow" node. */
struct sol_flow_node_container_type {
    struct sol_flow_node_type base; /**< base part of the container node */

    int (*send)(struct sol_flow_node *container, struct sol_flow_node *source_node, uint16_t source_out_port_idx, struct sol_flow_packet *packet); /**< member function issued when a child node sends packets to its output ports */
    void (*add)(struct sol_flow_node *container, struct sol_flow_node *node); /**< member function that, if not @c NULL, is issued when child nodes of of an instance of this type are created */
    void (*remove)(struct sol_flow_node *container, struct sol_flow_node *node); /**< member function that, if not @c NULL, is issued when child nodes of an insance of this type this are deleted */
};

struct sol_flow_port_type_out {
#define SOL_FLOW_PORT_TYPE_OUT_API_VERSION (1) /**< compile time API version to be checked during runtime */
    uint16_t api_version; /**< must match SOL_FLOW_PORT_TYPE_OUT_API_VERSION at runtime */
    const struct sol_flow_packet_type *packet_type; /**< the packet type that the port will deliver */

    int (*connect)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id); /**< member function issued everytime a new connection is made to the port */
    int (*disconnect)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id); /**< member function issued everytime a connection is unmade on the port */
};

struct sol_flow_port_type_in {
#define SOL_FLOW_PORT_TYPE_IN_API_VERSION (1) /**< compile time API version to be checked during runtime */
    uint16_t api_version; /**< must match SOL_FLOW_PORT_TYPE_OUT_API_VERSION at runtime */
    const struct sol_flow_packet_type *packet_type; /**< the packet type that the port will receive */

    int (*process)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet); /**< member function issued everytime a new packet arrives to the port */
    int (*connect)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id); /**< member function issued everytime a new connection is made to the port */
    int (*disconnect)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id); /**< member function issued everytime a connection is unmade on the port */
};

#ifdef SOL_DYNAMIC_MODULES

/**
 * Gets the specified node type, loading the necessary module if required.
 *
 * Checks if the node type @a _type is built-in, if not, it loads the module
 * @a _mod and fetches the type's symbol there. The result is stored in @a _var.
 *
 * @param _mod The name of the module to load if the symbol is not built-in.
 * @param _type The node type's symbol.
 * @param _var Variable where to store the type.
 *
 * @return 0 on success, < 0 on error.
 */
#define sol_flow_get_node_type(_mod, _type, _var) sol_flow_internal_get_node_type(_mod, #_type, _var)

int sol_flow_internal_get_node_type(const char *module, const char *symbol, const struct sol_flow_node_type **type);
#else
#define sol_flow_get_node_type(_mod, _type, _var) ({ (*(_var)) = _type; 0; })
#endif /* SOL_DYNAMIC_MODULES */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
