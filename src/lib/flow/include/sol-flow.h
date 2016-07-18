/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
 * @brief The flow system consists of a series of connected nodes that send
 * packets to each other via ports.
 *
 * Each node may have multiple input/output ports.
 * It is responsibility of the parent (see @ref sol_flow_node_container_type)
 * node to deliver the packets sent by its children nodes (one thing the "static flow" node,
 * returned by sol_flow_static_new(), already does).
 *
 * @{
 */

struct sol_flow_node_options;

struct sol_flow_node_type;

struct sol_flow_packet;

struct sol_flow_packet_type;

/**
 * @typedef sol_flow_node
 *
 * @brief A node is an entity that has input/output ports.
 *
 * Its operations are described by a node type,
 * so that the node can be seen as a class instance, being the node type the class.
 *
 * Nodes receive packets in their input ports and can send packets to
 * their output ports.
 */
struct sol_flow_node;
typedef struct sol_flow_node sol_flow_node;

/**
 * @brief Creates a new node.
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
 *                sol_flow_node_options_new())
 *
 * @return A new node instance on success, otherwise @c NULL
 */
struct sol_flow_node *sol_flow_node_new(struct sol_flow_node *parent, const char *id, const struct sol_flow_node_type *type, const struct sol_flow_node_options *options);

/**
 * @brief Deletes a node.
 *
 * The root node should be deleted in the sol_main_callbacks::shutdown
 * function -- it will take care of recursively deleting its children
 * nodes.
 *
 * @param node The node to be deleted
 */
void sol_flow_node_del(struct sol_flow_node *node);

/**
 * @brief Retrieves a node private data.
 *
 * Private data is the data allocated by the system for each node,
 * whose size is described by @c data_size attribute of the node's type.
 *
 * @param node The Node
 */
void *sol_flow_node_get_private_data(const struct sol_flow_node *node);

/**
 * @brief Retrieves the node ID string.
 *
 * As given to the node on sol_flow_node_new()
 *
 * @param node The Node
 *
 * @return The Node ID on success, @c NULL otherwise.
 */
const char *sol_flow_node_get_id(const struct sol_flow_node *node);

/**
 * @brief Gets the node's parent.
 *
 * @param node The node to get the parent from
 *
 * @return The parent node of @a node, @c NULL if @a node is the
 *         top/root node.
 */
const struct sol_flow_node *sol_flow_node_get_parent(const struct sol_flow_node *node);

/**
 * @brief Send a packet from a given node to one of its output ports.
 *
 * The parent node of @a src will take care of routing the packet to
 * the appropriated input ports of connected nodes.
 *
 * @note This function takes the ownership of the packet, becoming
 * responsible to release its memory.
 *
 * @note There are helper functions that already create a packet of a given
 *       type and send it, e. g. sol_flow_send_bool_packet().
 *
 * @param src The node that is to output a packet
 * @param src_port The port where the packet will be output
 * @param packet The packet to output
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_packet(struct sol_flow_node *src, uint16_t src_port, struct sol_flow_packet *packet);

/*
 * Helper functions to create and send packets of specific types.
 *
 * They work like sol_flow_send_packet(), but besides creating,
 * sending and freeing the packet, they'll also set their value.
 */

/**
 * @brief Convenience function to create and send an Error packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Error packets.
 * It will create a new Error packet and send it through @c src node.
 *
 * @param src The node that is to output the error packet
 * @param code Error code
 * @param msg_fmt A standard 'printf()' format string. Used to create the error message.
 * @param ... Arguments to @c msg_fmt
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_error_packet(struct sol_flow_node *src, int code, const char *msg_fmt, ...) SOL_ATTR_PRINTF(3, 4);

/**
 * @brief Similar to sol_flow_send_error_packet,
 * but uses a default error message based on @c code.
 *
 * @param src The node that is to output the error packet
 * @param code Error code
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_error_packet_errno(struct sol_flow_node *src, int code);

/**
 * @brief Similar to sol_flow_send_error_packet, but the error message is ready to be used.
 *
 * @param src The node that is to output the error packet
 * @param code Error code
 * @param str Error message
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_error_packet_str(struct sol_flow_node *src, int code, const char *str);

/**
 * @brief Convenience function to create and send a Boolean packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Boolean packets.
 * It will create a new Boolean packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Boolean packet
 * @param src_port The port where the packet will be output
 * @param value Boolean packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_bool_packet(struct sol_flow_node *src, uint16_t src_port, unsigned char value);

/**
 * @brief Convenience function to create and send a Blob packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Blob packets.
 * It will create a new Blob packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Blob packet
 * @param src_port The port where the packet will be output
 * @param value Blob packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_blob_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value);

/**
 * @brief Convenience function to create and send a JSON Object packet.
 *
 * Similar to sol_flow_send_packet(), but specific for JSON Object packets.
 * It will create a new JSON Object packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the JSON Object packet
 * @param src_port The port where the packet will be output
 * @param value JSON Object packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_json_object_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value);

/**
 * @brief Convenience function to create and send a JSON Array packet.
 *
 * Similar to sol_flow_send_packet(), but specific for JSON Array packets.
 * It will create a new JSON Array packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the JSON Array packet
 * @param src_port The port where the packet will be output
 * @param value JSON Array packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_json_array_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_blob *value);

/**
 * @brief Convenience function to create and send a Byte packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Byte packets.
 * It will create a new Byte packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Byte packet
 * @param src_port The port where the packet will be output
 * @param value Byte packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_byte_packet(struct sol_flow_node *src, uint16_t src_port, unsigned char value);

/**
 * @brief Convenience function to create and send a Drange packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Drange packets.
 * It will create a new Drange packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Drange packet
 * @param src_port The port where the packet will be output
 * @param value Drange packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_drange_value_packet
 */
int sol_flow_send_drange_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_drange *value);

/**
 * @brief Convenience function to create and send a Drange packet of value @c value and default spec.
 *
 * Similar to sol_flow_send_drange_packet(), but creates a Drange struct from @c value and set
 * the default spec to it.
 *
 * @param src The node that is to output the Drange packet
 * @param src_port The port where the packet will be output
 * @param value Desired Drange value
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_drange_packet
 */
int sol_flow_send_drange_value_packet(struct sol_flow_node *src, uint16_t src_port, double value);

/**
 * @brief Convenience function to create and send a RGB packet.
 *
 * Similar to sol_flow_send_packet(), but specific for RGB packets.
 * It will create a new RGB packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the RGB packet
 * @param src_port The port where the packet will be output
 * @param value RGB packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_rgb_components_packet
 */
int sol_flow_send_rgb_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_rgb *value);

/**
 * @brief Similar to sol_flow_send_rgb_packet(), but takes the RGB components as arguments.
 *
 * @param src The node that is to output the RGB packet
 * @param src_port The port where the packet will be output
 * @param red Red value
 * @param green Green value
 * @param blue Blue value
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_rgb_packet
 */
int sol_flow_send_rgb_components_packet(struct sol_flow_node *src, uint16_t src_port, uint32_t red, uint32_t green, uint32_t blue);

/**
 * @brief Convenience function to create and send a Direction Vector packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Direction Vector packets.
 * It will create a new Direction Vector packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Direction Vector packet
 * @param src_port The port where the packet will be output
 * @param value Direction Vector packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_direction_vector_components_packet
 */
int sol_flow_send_direction_vector_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_direction_vector *value);

/**
 * @brief Similar to sol_flow_send_direction_vector_packet(), but takes the vector components as arguments.
 *
 * @param src The node that is to output the Direction Vector packet
 * @param src_port The port where the packet will be output
 * @param x X value
 * @param y Y value
 * @param z Z value
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_direction_vector_packet
 */
int sol_flow_send_direction_vector_components_packet(struct sol_flow_node *src, uint16_t src_port, double x, double y, double z);

/**
 * @brief Convenience function to create and send a Location packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Location packets.
 * It will create a new Location packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Location packet
 * @param src_port The port where the packet will be output
 * @param value Location packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_location_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_location *value);

/**
 * @brief Similar to sol_flow_send_location_packet(), but takes the location components as arguments.
 *
 * @param src The node that is to output the Location packet
 * @param src_port The port where the packet will be output
 * @param lat Latitude value
 * @param lon Longitude value
 * @param alt Altitude value
 *
 * @return @c 0 on success, a negative error code otherwise.
 *
 * @see sol_flow_send_location_packet
 */
int sol_flow_send_location_components_packet(struct sol_flow_node *src, uint16_t src_port, double lat, double lon, double alt);

/**
 * @brief Convenience function to create and send a Timestamp packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Timestamp packets.
 * It will create a new Timestamp packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Timestamp packet
 * @param src_port The port where the packet will be output
 * @param value Timestamp packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_timestamp_packet(struct sol_flow_node *src, uint16_t src_port, const struct timespec *value);

/**
 * @brief Convenience function to create and send an Empty packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Empty packets.
 * It will create a new Empty packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Empty packet
 * @param src_port The port where the packet will be output
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_empty_packet(struct sol_flow_node *src, uint16_t src_port);

/**
 * @brief Convenience function to create and send an Irange packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Irange packets.
 * It will create a new Irange packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Irange packet
 * @param src_port The port where the packet will be output
 * @param value Irange packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_irange_value_packet
 */
int sol_flow_send_irange_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_irange *value);

/**
 * @brief Convenience function to create and send a Irange packet of value @c value and default spec.
 *
 * Similar to sol_flow_send_irange_packet(), but creates a Irange struct from @c value and set
 * the default spec to it.
 *
 * @param src The node that is to output the Irange packet
 * @param src_port The port where the packet will be output
 * @param value Desired Irange value
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_irange_packet
 */
int sol_flow_send_irange_value_packet(struct sol_flow_node *src, uint16_t src_port, int32_t value);

/**
 * @brief Convenience function to create and send a String packet.
 *
 * Similar to sol_flow_send_packet(), but specific for String packets.
 * It will create a new String packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the String packet
 * @param src_port The port where the packet will be output
 * @param value String packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_string_slice_packet()
 * @see sol_flow_send_string_take_packet()
 */
int sol_flow_send_string_packet(struct sol_flow_node *src, uint16_t src_port, const char *value);

/**
 * @brief Convenience function to create and send a String packet from a string slice.
 *
 * Similar to sol_flow_send_string_packet(), but takes a @ref sol_str_slice instead of a C string.
 *
 * @param src The node that is to output the String packet
 * @param src_port The port where the packet will be output
 * @param value String slice to create the packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_string_packet()
 * @see sol_flow_send_string_take_packet()
 */
int sol_flow_send_string_slice_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_str_slice value);

/**
 * @brief Convenience function to create and send a String packet but takes ownership of @c value
 * to use as the packet content.
 *
 * Instead of copying the initial string to the packets content, takes ownership of @c value
 * memory.
 *
 * @warning The caller should not free @a value anymore.
 *
 * @param src The node that is to output the String packet
 * @param src_port The port where the packet will be output
 * @param value String packet content
 *
 * @return @c 0 on success, a negative error code on errors.
 *
 * @see sol_flow_send_string_packet()
 * @see sol_flow_send_string_slice_packet()
 */
int sol_flow_send_string_take_packet(struct sol_flow_node *src, uint16_t src_port, char *value);

/**
 * @brief Convenience function to create and send a Composed packet.
 *
 * Similar to sol_flow_send_packet(), but specific for Composed packets.
 * It will create a new Composed packet from the @c composed_type and @c children
 * and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the Blob packet
 * @param src_port The port where the packet will be output
 * @param composed_type The composed packet type
 * @param children List of children packets
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_composed_packet(struct sol_flow_node *src, uint16_t src_port, const struct sol_flow_packet_type *composed_type, struct sol_flow_packet **children);

/**
 * @brief Convenience function to create and send a HTTP Response packet.
 *
 * Similar to sol_flow_send_packet(), but specific for HTTP Response packets.
 * It will create a new HTTP Response packet and send it through @c src node at @c src_port port.
 *
 * @param src The node that is to output the HTTP Response packet
 * @param src_port The port where the packet will be output
 * @param response_code The response code
 * @param url Response URL
 * @param content_type The response content type
 * @param content The response content
 * @param cookies Response cookies
 * @param headers Response headers
 *
 * @return @c 0 on success, a negative error code on errors.
 */
int sol_flow_send_http_response_packet(struct sol_flow_node *src, uint16_t src_port, int response_code, const char *url, const char *content_type, const struct sol_blob *content, const struct sol_vector *cookies, const struct sol_vector *headers);

/**
 * @brief Get a node's type.
 *
 * @param node The node get the type from
 *
 * @return The node type of @a node or @c NULL, on errors
 */
const struct sol_flow_node_type *sol_flow_node_get_type(const struct sol_flow_node *node);

/**
 * @brief Node options are a set of attributes defined by the Node Type that
 * can change the behavior of a Node.
 */
typedef struct sol_flow_node_options {
#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_NODE_OPTIONS_API_VERSION (1) /**< @brief Compile time API version to be checked during runtime */
    uint16_t api_version; /**< @brief Must match SOL_FLOW_NODE_OPTIONS_API_VERSION at runtime */
    uint16_t sub_api; /**< @brief To version each subclass */
#endif
} sol_flow_node_options;

/**
 * @brief Possible types for option attributes (or members).
 */
enum sol_flow_node_options_member_type {
    SOL_FLOW_NODE_OPTIONS_MEMBER_UNKNOWN,
    SOL_FLOW_NODE_OPTIONS_MEMBER_BOOL,
    SOL_FLOW_NODE_OPTIONS_MEMBER_BYTE,
    SOL_FLOW_NODE_OPTIONS_MEMBER_DIRECTION_VECTOR,
    SOL_FLOW_NODE_OPTIONS_MEMBER_DRANGE_SPEC,
    SOL_FLOW_NODE_OPTIONS_MEMBER_FLOAT,
    SOL_FLOW_NODE_OPTIONS_MEMBER_INT,
    SOL_FLOW_NODE_OPTIONS_MEMBER_IRANGE_SPEC,
    SOL_FLOW_NODE_OPTIONS_MEMBER_RGB,
    SOL_FLOW_NODE_OPTIONS_MEMBER_STRING,
};

/**
 * @brief Returns a string for the name of a given option member type.
 *
 * @param type Type to be named
 *
 * @return String of the type's name, @c NULL on errors
 */
const char *sol_flow_node_options_member_type_to_str(enum sol_flow_node_options_member_type type);

/**
 * @brief Returns the option member type which name is @c data_type.
 *
 * @param data_type Name of the option member type
 *
 * @return Option member type which name is @a data_type or
 * SOL_FLOW_NODE_OPTIONS_MEMBER_UNKNOWN if not found
 */
enum sol_flow_node_options_member_type sol_flow_node_options_member_type_from_string(const char *data_type);

/**
 * @brief Structure of a Options Member
 */
typedef struct sol_flow_node_named_options_member {
    const char *name; /**< @brief Member's name */
    enum sol_flow_node_options_member_type type; /**< Member's type */
    union {
        bool boolean; /**< @brief Value of a boolean member */
        unsigned char byte; /**< @brief Value of a byte member */
        int32_t i; /**< @brief Value of a integer member */
        struct sol_irange_spec irange_spec; /**< @brief Value of a irange spec member */
        struct sol_drange_spec drange_spec; /**< @brief Value of a drange spec member */
        struct sol_rgb rgb; /**< @brief Value of a rgb member */
        struct sol_direction_vector direction_vector; /**< @brief Value of a direction vector member */
        const char *string; /**< @brief Value of a string member */
        double f; /**< @brief Value of a float member */
    };
} sol_flow_node_named_options_member;

/**
 * @brief Named options is an intermediate structure to handle Node Options parsing
 *
 * Used to help the options parser to parse an options string.
 */
typedef struct sol_flow_node_named_options {
    struct sol_flow_node_named_options_member *members; /**< @brief List of option members */
    uint16_t count; /**< @brief Number of members */
} sol_flow_node_named_options;

/**
 * @brief Creates a new Node Options.
 *
 * @param type The Node Type
 * @param named_opts Named Options to parse
 * @param out_opts The final Node Options
 *
 * @return @c 0 on success, a negative error code on errors
 */
int sol_flow_node_options_new(
    const struct sol_flow_node_type *type,
    const struct sol_flow_node_named_options *named_opts,
    struct sol_flow_node_options **out_opts);

/**
 * @brief Initializes a Named options structure from a options string.
 *
 * @param named_opts Named Options to be initialized
 * @param type The Node Type
 * @param strv Options string used to initialize the Named Options
 *
 * @return @c 0 on success, a negative error code on errors
 */
int sol_flow_node_named_options_init_from_strv(
    struct sol_flow_node_named_options *named_opts,
    const struct sol_flow_node_type *type,
    const char *const *strv);

/**
 * @brief Delete an options handle.
 *
 * @param type The node type
 * @param options The node options
 */
void sol_flow_node_options_del(const struct sol_flow_node_type *type, struct sol_flow_node_options *options);

/**
 * @brief Delete a key-value options array.
 *
 * @param opts_strv Options array to be deleted
 *
 * @see sol_flow_resolve_strv
 */
void sol_flow_node_options_strv_del(char **opts_strv);

/**
 * @brief Finalize named options.
 *
 * @param named_opts Named options to be finalized
 */
void sol_flow_node_named_options_fini(struct sol_flow_node_named_options *named_opts);

#include "sol-flow-buildopts.h"

/**
 * @brief Error port identifier.
 */
#define SOL_FLOW_NODE_PORT_ERROR_NAME ("ERROR")

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
/**
 * @brief Description object used for introspection of Ports.
 */
typedef struct sol_flow_port_description {
    const char *name; /**< @brief Port's name */
    const char *description; /**< @brief Port's description */
    const char *data_type; /**< @brief Textual representation of the port's accepted packet data type(s), e. g. "int" */
    uint16_t array_size; /**< @brief Size of array for array ports, or 0 for single ports */
    uint16_t base_port_idx; /**< @brief for array ports, the port number where the array begins */
    /**
     * @brief Whether at least one connection has to be made on this port or not.
     *
     * Soletta does not check that at runtime, this is mostly a hint for visual editors
     * that can output flows/code from visual representations of a flow
     */
    bool required;
} sol_flow_port_description;

/**
 * @brief Description object used for introspection of Node options members.
 */
typedef struct sol_flow_node_options_member_description {
    const char *name; /**< @brief Option member's name */
    const char *description; /**< @brief Option member's description */
    const char *data_type; /**< @brief Textual representation of the options data type(s), e. g. "int" */
    union {
        bool b; /**< @brief Option member's default boolean value */
        unsigned char byte; /**< @brief Option member's default byte value */
        int32_t i; /**< @brief Option member's default int value */
        struct sol_irange_spec irange_spec; /**< @brief Option member's default integer range spec */
        struct sol_drange_spec drange_spec; /**< @brief Option member's default float range spec */
        struct sol_direction_vector direction_vector; /**< @brief Option member's default direction vector value */
        struct sol_rgb rgb; /**< @brief Option member's default RGB value */
        const char *s; /**< @brief Option member's default string value */
        const void *ptr; /**< @brief Option member's default "blob" value */
        double f; /**< @brief Option member's default float value */
    } defvalue; /**< @brief Option member's default value, according to its #data_type */
    uint16_t offset; /**< @brief Option member's offset inside the final options blob for a node */
    uint16_t size; /**< @brief Option member's size inside the final options blob for a node */
    bool required; /**< @brief Whether the option member is mandatory or not when creating a node */
} sol_flow_node_options_member_description;

/**
 * @brief Description object used for introspection of Node options.
 */
typedef struct sol_flow_node_options_description {
    const struct sol_flow_node_options_member_description *members; /**< @brief Node options members (@c NULL terminated) */
    uint16_t data_size; /**< @brief Size of the whole sol_flow_node_options derivate */
#ifndef SOL_NO_API_VERSION
    uint16_t sub_api; /**< @brief What goes in sol_flow_node_options::sub_api */
#endif
    bool required; /**< @brief If true then options must be given for the node (if not, the node has no parameters) */
} sol_flow_node_options_description;

/**
 * @brief Description object used for introspection of Node types.
 *
 * A node type description provides more information about a node
 * type, such as textual description, name, URL, version, author as
 * well as ports and options meta information.
 */
typedef struct sol_flow_node_type_description {
#ifndef SOL_NO_API_VERSION
    /**
     * @brief Compile time API version to be checked during runtime
     *
     * @warning Both @ref sol_flow_node_type_description,
     * @ref sol_flow_port_description and @ref sol_flow_node_options_description
     * are subject to @ref SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION,
     * then whenever one of these structures are changed, the version number
     * should be incremented.
     */
#define SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION (1)
    uint16_t api_version; /**< @brief Must match SOL_FLOW_NODE_TYPE_DESCRIPTION_API_VERSION at runtime */
#endif
    const char *name; /**< @brief The user-visible name. @b Mandatory. */

    /**
     * @brief Node category. @b Mandatory.
     *
     * The convention is: category/subcategory/..., such as @c input/hw/sensor for
     * a pressure sensor or @c input/sw/oic/switch for an OIC compliant @c on/off switch.
     */
    const char *category;
    const char *symbol; /**< @brief The symbol that exports this type, useful to code that generates C code.  */
    const char *options_symbol; /**< @brief The options symbol that exports this options type, useful to code that generates C code.  */
    const char *description; /**< @brief Description for a node */
    const char *author; /**< @brief Node's author */
    const char *url; /**< @brief Node author/vendor's URL */
    const char *license; /**< @brief Node's license */
    const char *version; /**< @brief Version string */
    const struct sol_flow_port_description *const *ports_in; /**< @brief @c NULL terminated input ports array */
    const struct sol_flow_port_description *const *ports_out; /**< @brief @c NULL terminated output ports array */
    const struct sol_flow_node_options_description *options; /**< @brief Node options */
} sol_flow_node_type_description;
#endif

/**
 * @brief Flags used to set some @ref sol_flow_node_type characteristics
 */
enum sol_flow_node_type_flags {
    SOL_FLOW_NODE_TYPE_FLAGS_CONTAINER = (1 << 0), /**< @brief Flag to set the node as Container (a "static flow" node is an example) */
};

/**
 * @brief The node type describes the capabilities and operations of a node.
 *
 * So a node can be seen as a class instance, being the node type the class.
 *
 * This description is usually defined as @c const @c static
 * and shared by many different nodes.
 */
typedef struct sol_flow_node_type {
#define SOL_FLOW_NODE_PORT_ERROR (UINT16_MAX - 1) /**< @brief Built-in output port's number, common to every node, meant to output error packets */
#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_NODE_TYPE_API_VERSION (1) /**< @brief Compile time API version to be checked during runtime */
    uint16_t api_version; /**< @brief Must match SOL_FLOW_NODE_TYPE_API_VERSION at runtime */
#endif
    uint16_t data_size; /**< @brief Size of the whole @ref sol_flow_node_type derivate in bytes */
    uint16_t options_size; /**< @brief Options size in bytes */
    uint16_t flags; /**< @brief Node type flags */

    const void *type_data; /**< @brief Pointer to per-type user data */
    const void *default_options; /**< @brief The default options for this type */

    uint16_t ports_in_count; /**< @brief Number of Input ports */
    uint16_t ports_out_count; /**< @brief Number of Output ports */

    /**
     * @brief Member function to get the array of the node's input ports
     */
    const struct sol_flow_port_type_in *(*get_port_in)(const struct sol_flow_node_type *type, uint16_t port);

    /**
     * @brief Member function to get the array of the node's output ports
     */
    const struct sol_flow_port_type_out *(*get_port_out)(const struct sol_flow_node_type *type, uint16_t port);

    /**
     * @brief Member function to instantiate the node
     */
    int (*open)(struct sol_flow_node *node, void *data, const struct sol_flow_node_options *options);

    /**
     * @brief Member function to delete the node
     */
    void (*close)(struct sol_flow_node *node, void *data);

    /**
     * @brief Member function that allows initialization of node-specific data (packet types, logging domains, etc)
     *
     * Called at least once for each node type.
     */
    void (*init_type)(void);

    /**
     * @brief Called to dispose any extra resources.
     *
     * Called as part of sol_flow_node_type_del() to dispose
     * extra resources associated with the node type.
     */
    void (*dispose_type)(struct sol_flow_node_type *type);

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    const struct sol_flow_node_type_description *description; /**< @brief Pointer to node's description */
#endif
} sol_flow_node_type;

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
/**
 * @brief Parses a member of a Named Options.
 *
 * @param m Member to be parsed
 * @param value Parsed member value
 * @param mdesc Output member description object
 *
 * @return @c 0 on success, a negative error code on errors
 */
int sol_flow_node_named_options_parse_member(
    struct sol_flow_node_named_options_member *m,
    const char *value,
    const struct sol_flow_node_options_member_description *mdesc);
#endif

/**
 * @brief Get a node type's input port definition struct, given a port index.
 *
 * @param type The node type to get a port definition from
 * @param port The port's index to retrieve
 *
 * @return The input port's definition struct or @c NULL, on errors.
 */
const struct sol_flow_port_type_in *sol_flow_node_type_get_port_in(const struct sol_flow_node_type *type, uint16_t port);

/**
 * @brief Get a node type's output port definition struct, given a port index.
 *
 * @param type The node type to get a port definition from
 * @param port The port's index to retrieve
 *
 * @return The output port's definition struct or @c NULL, on errors.
 */
const struct sol_flow_port_type_out *sol_flow_node_type_get_port_out(const struct sol_flow_node_type *type, uint16_t port);

/**
 * @brief Delete a node type.
 *
 * It should be used only for types dynamically created and
 * returned by functions like sol_flow_static_new_type().
 *
 * @param type The node type to be deleted
 */
void sol_flow_node_type_del(struct sol_flow_node_type *type);

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
/**
 * @brief Iterator on all node types that were compiled as built-in.
 *
 * @param cb The callback function to be issued on each built-in node
 *           type found
 * @param data The user data to forward to @a cb
 */
void sol_flow_foreach_builtin_node_type(bool (*cb)(void *data, const struct sol_flow_node_type *type), const void *data);

/**
 * @brief Get the port description associated with a given input port index.
 *
 * @param type The node type to get a port description from
 * @param port The port index
 *
 * @return The port description for the given port
 */
const struct sol_flow_port_description *sol_flow_node_get_description_port_in(const struct sol_flow_node_type *type, uint16_t port);

/**
 * @brief Get the port description associated with a given output port index.
 *
 * @param type The node type to get a port description from
 * @param port The port index
 *
 * @return The port description for the given port
 */
const struct sol_flow_port_description *sol_flow_node_get_description_port_out(const struct sol_flow_node_type *type, uint16_t port);

/**
 * @brief Find the input port index given its name.
 *
 * @param type The node type to get a port index from name
 * @param name The port name. If an array port, one should use
 *       "NAME[INDEX]" notation, such as "IN[0]". Names are case
 *       sensitive.
 *
 * @return The port index or UINT16_MAX if not found.
 */
uint16_t sol_flow_node_find_port_in(const struct sol_flow_node_type *type, const char *name);

/**
 * @brief Find the output port index given its name.
 *
 * @param type The node type to get a port index from name
 * @param name The port name. If an array port, one should use
 *       "NAME[INDEX]" notation, such as "OUT[0]". Names are case
 *       sensitive.
 *
 * @return The port index or UINT16_MAX if not found.
 */
uint16_t sol_flow_node_find_port_out(const struct sol_flow_node_type *type, const char *name);
#endif

/**
 * @brief Structure of Container Node
 *
 * When a node type is a container (i.e. may act as parent of other nodes),
 * it should provide extra operations. This is the case of the "static flow" node.
 */
typedef struct sol_flow_node_container_type {
    struct sol_flow_node_type base; /**< @brief base part of the container node */

    /**
     * @brief Member function issued when a child node sends packets to its output ports
     */
    int (*send)(struct sol_flow_node *container, struct sol_flow_node *source_node, uint16_t source_out_port_idx, struct sol_flow_packet *packet);

    /**
     * @brief Member function issued when there is no parent and a sol_flow_send() was called in this container.
     *
     * This method, if present, may be used to redirect the packet to
     * some child node. Otherwise the packet is dropped (deleted).
     *
     * If this method is implemented and returns 0, the ownership of
     * the packet is then handled by the function. If it returns
     * non-zero, then the packet is automatically deleted.
     */
    int (*process)(struct sol_flow_node *container, uint16_t source_in_port_idx, struct sol_flow_packet *packet);

    /**
     * @brief Member function that, if not @c NULL, is issued when child nodes of of an instance of this type are created
     */
    void (*add)(struct sol_flow_node *container, struct sol_flow_node *node);

    /**
     * @brief Member function that, if not @c NULL, is issued when child nodes of an instance of this type this are deleted
     */
    void (*remove)(struct sol_flow_node *container, struct sol_flow_node *node);
} sol_flow_node_container_type;

/**
 * @brief Node's Output port structure.
 */
typedef struct sol_flow_port_type_out {
#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_PORT_TYPE_OUT_API_VERSION (1) /**< @brief Compile time API version to be checked during runtime */
    uint16_t api_version; /**< @brief Must match SOL_FLOW_PORT_TYPE_OUT_API_VERSION at runtime */
#endif
    const struct sol_flow_packet_type *packet_type; /**< @brief The packet type that the port will deliver */

    /**
     * @brief Member function issued every time a new connection is made to the port
     */
    int (*connect)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);

    /**
     * @brief Member function issued every time a connection is unmade on the port
     */
    int (*disconnect)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id);
} sol_flow_port_type_out;

/**
 * @brief Node's Input port structure.
 */
typedef struct sol_flow_port_type_in {
#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_PORT_TYPE_IN_API_VERSION (1) /**< Compile time API version to be checked during runtime */
    uint16_t api_version; /**< Must match SOL_FLOW_PORT_TYPE_OUT_API_VERSION at runtime */
#endif
    const struct sol_flow_packet_type *packet_type; /**< The packet type that the port will receive */

    /**
     * @brief Member function issued every time a new packet arrives to the port.
     * */
    int (*process)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id, const struct sol_flow_packet *packet);
    int (*connect)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id); /**< member function issued every time a new connection is made to the port */
    int (*disconnect)(struct sol_flow_node *node, void *data, uint16_t port, uint16_t conn_id); /**< member function issued every time a connection is unmade on the port */
} sol_flow_port_type_in;

/**
 * @def sol_flow_get_node_type(_mod, _type, _var)
 *
 * @brief Gets the specified node type, loading the necessary module if required.
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

/**
 * @def sol_flow_get_packet_type(_mod, _type, _var)
 *
 * @brief Gets the specified packet type, loading the necessary module if required.
 *
 * Checks if the node type @a _type is built-in, if not, it loads the module
 * @a _mod and fetches the packet's symbol there. The result is stored in @a _var.
 *
 * @param _mod The name of the module to load if the packet is not built-in.
 * @param _type The packet type's symbol.
 * @param _var Variable where to store the type.
 *
 * @return 0 on success, < 0 on error.
 */
#ifdef SOL_DYNAMIC_MODULES

#define sol_flow_get_node_type(_mod, _type, _var) sol_flow_internal_get_node_type(_mod, #_type, _var)

int sol_flow_internal_get_node_type(const char *modname, const char *symbol, const struct sol_flow_node_type ***type);

#define sol_flow_get_packet_type(_mod, _type, _var) sol_flow_internal_get_packet_type(_mod, #_type, _var)

int sol_flow_internal_get_packet_type(const char *module, const char *symbol, const struct sol_flow_packet_type **type);

#else
#define sol_flow_get_node_type(_mod, _type, _var) ({ (*(_var)) = &_type; 0; })
#define sol_flow_get_packet_type(_mod, _type, _var) ({ (*(_var)) = _type; 0; })
#endif /* SOL_DYNAMIC_MODULES */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
