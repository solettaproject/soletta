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
#include <time.h>

#include "sol-common-buildopts.h"
#include "sol-str-slice.h"
#include "sol-types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief These routines are used for Soletta flow packets manipulation.
 */

/**
 * @defgroup FlowPacket Flow Packet
 * @ingroup Flow
 *
 * @brief Flow packet is the fundamental data structure used to pass
 * information between nodes in a flow.
 *
 * @{
 */

/**
 * @typedef sol_flow_packet
 *
 * @brief A packet is a generic container for different kinds (types) of contents.
 */
struct sol_flow_packet;
typedef struct sol_flow_packet sol_flow_packet;

/**
 * @brief A packet type defines what's the content of a packet and how it's stored and
 * retrieved.
 */
typedef struct sol_flow_packet_type {
#ifndef SOL_NO_API_VERSION
#define SOL_FLOW_PACKET_TYPE_API_VERSION (1)
    uint16_t api_version; /**< @brief API version number */
#endif
    uint16_t data_size; /**< @brief Data's size of a given packet type */
    const char *name; /**< @brief Type's name */

    /**
     * @brief Initializes a packet instance of this type.
     *
     * @param type The packet's type
     * @param mem Packet's data memory (the content)
     * @param input Initial data for the packet's content
     *
     * @return @c 0 on success, error code (always negative) otherwise
     */
    int (*init)(const struct sol_flow_packet_type *packet_type, void *mem, const void *input);

    /**
     * @brief Retrieves the content from a packet of this type.
     * @param type The packet's type
     * @param mem Packet's data memory (the content)
     * @param output Where to store a copy the packet's content
     *
     * @return @c 0 on success, error code (always negative) otherwise
     */
    int (*get)(const struct sol_flow_packet_type *packet_type, const void *mem, void *output);

    /**
     * @brief Disposes a packet instance of this type.
     *
     * @param type The packet's type
     * @param mem Packet's data to be disposed
     */
    void (*dispose)(const struct sol_flow_packet_type *packet_type, void *mem);

    /**
     * @brief Internal. Used for types that have a set of constant values.
     *
     * This way no allocation is needed.
     *
     * @param type The packet's type
     * @param value Constant initial value
     * */
    struct sol_flow_packet *(*get_constant)(const struct sol_flow_packet_type *packet_type, const void *value);
} sol_flow_packet_type;

/**
 * @brief Creates a packet.
 *
 * @param type The packet's type
 * @param value Data to use in the packet initialization
 *
 * @return A new packet of type @a type, @c NULL on errors
 */
struct sol_flow_packet *sol_flow_packet_new(const struct sol_flow_packet_type *type, const void *value);

/**
 * @brief Deletes a packet.
 *
 * @param packet Packet to be deleted
 */
void sol_flow_packet_del(struct sol_flow_packet *packet);

/**
 * @brief Retrieves the packet's type.
 *
 * @param packet The packet
 *
 * @return Type of the packet, @c NULL on errors
 */
const struct sol_flow_packet_type *sol_flow_packet_get_type(const struct sol_flow_packet *packet);

/**
 * @brief Retrieves the packet's content.
 *
 * @param packet The packet
 * @param output Where to store a copy the packet's content
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_flow_packet_get(const struct sol_flow_packet *packet, void *output);

/**
 * @brief Type of the Empty packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_EMPTY;

/**
 * @brief Type of the Boolean packet
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_BOOL;

/**
 * @brief Type of the Byte packet
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_BYTE;

/**
 * @brief Type of the Irange packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_IRANGE;

/**
 * @brief Type of the String packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_STRING;

/**
 * @brief Type of the Blob packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_BLOB;

/**
 * @brief Type of the JSON Object packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_JSON_OBJECT;

/**
 * @brief Type of the JSON Array packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_JSON_ARRAY;

/**
 * @brief Type of the Drange packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_DRANGE;

/**
 * @brief Type of the Any packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_ANY;

/**
 * @brief Type of the Error packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_ERROR;

/**
 * @brief Type of the RGB packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_RGB;

/**
 * @brief Type of the Direction Vector packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_DIRECTION_VECTOR;

/**
 * @brief Type of the Location packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_LOCATION;

/**
 * @brief Type of the Timestamp packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_TIMESTAMP;

/**
 * @brief Type of the Http Response packet.
 */
extern const struct sol_flow_packet_type *SOL_FLOW_PACKET_TYPE_HTTP_RESPONSE;


/*
 * Convenience functions to use certain types of common packets.
 */

/**
 * @brief Creates a new packet of type Empty.
 *
 * @return A new Empty packet
 */
struct sol_flow_packet *sol_flow_packet_new_empty(void);

/**
 * @brief Creates a new packet of type Boolean.
 *
 * @param boolean Initial value
 *
 * @return A new Boolean packet
 */
struct sol_flow_packet *sol_flow_packet_new_bool(bool boolean);

/**
 * @brief Retrieves the content of a Boolean packet.
 *
 * @param packet The packet
 * @param boolean The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_bool(const struct sol_flow_packet *packet, bool *boolean);

/**
 * @brief Creates a new packet of type Byte.
 *
 * @param byte Initial value
 *
 * @return A new Byte packet
 */
struct sol_flow_packet *sol_flow_packet_new_byte(unsigned char byte);

/**
 * @brief Retrieves the content of a Byte packet.
 *
 * @param packet The packet
 * @param byte The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_byte(const struct sol_flow_packet *packet, unsigned char *byte);

/**
 * @brief Creates a new packet of type Irange.
 *
 * @param irange Initial value
 *
 * @return A new Irange packet
 *
 * @see sol_flow_packet_new_irange_value
 */
struct sol_flow_packet *sol_flow_packet_new_irange(const struct sol_irange *irange);

/**
 * @brief Creates a new packet of type Irange with initial value @c value and default spec.
 *
 * @param value Initial value
 *
 * @return A new Irange packet
 *
 * @see sol_flow_packet_new_irange
 */
struct sol_flow_packet *sol_flow_packet_new_irange_value(int32_t value);

/**
 * @brief Retrieves the content of an Irange packet.
 *
 * @param packet The packet
 * @param irange The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 *
 * @see sol_flow_packet_get_irange_value
 */
int sol_flow_packet_get_irange(const struct sol_flow_packet *packet, struct sol_irange *irange);

/**
 * @brief Retrieves the Irange value of an Irange packet.
 *
 * @param packet The packet
 * @param value Irange value contained in the packet
 *
 * @return @c 0 if the value was successfully retrieved, error code (always negative) otherwise.
 *
 * @see sol_flow_packet_get_irange
 */
int sol_flow_packet_get_irange_value(const struct sol_flow_packet *packet, int32_t *value);

/**
 * @brief Creates a new packet of type String.
 *
 * @param value Initial string
 *
 * @return A new String packet
 *
 * @see sol_flow_packet_new_string_slice
 * @see sol_flow_packet_new_string_take
 */
struct sol_flow_packet *sol_flow_packet_new_string(const char *value);

/**
 * @brief Creates a new packet of type String from string slice @c slice.
 *
 * @param slice String slice with the initial content
 *
 * @return A new String packet
 *
 * @see sol_flow_packet_new_string
 * @see sol_flow_packet_new_string_take
 */
struct sol_flow_packet *sol_flow_packet_new_string_slice(struct sol_str_slice slice);

/**
 * @brief Retrieves the content of a String packet.
 *
 * @param packet The packet
 * @param value The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_string(const struct sol_flow_packet *packet, const char **value);

/**
 * @brief Similar to sol_flow_packet_new_string() but takes ownership of @c value
 * to use as the packet content.
 *
 * Instead of copying the initial string to the packets content, takes ownership of @c value
 * memory.
 *
 * @param value Initial string
 *
 * @return A new String packet
 *
 * @see sol_flow_packet_new_string
 * @see sol_flow_packet_new_string_slice
 */
struct sol_flow_packet *sol_flow_packet_new_string_take(char *value);

/**
 * @brief Creates a new packet of type Blob.
 *
 * @param value Initial blob
 *
 * @return A new Blob packet
 */
struct sol_flow_packet *sol_flow_packet_new_blob(const struct sol_blob *value);

/**
 * @brief Retrieves the content of a Blob packet.
 *
 * @param packet The packet
 * @param value The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_blob(const struct sol_flow_packet *packet, struct sol_blob **value);

/**
 * @brief Creates a new packet of type JSON Object.
 *
 * @note The blob content should be a string with a valid JSON object.
 *
 * @note May or may not be @c NUL terminated.
 *
 * @param value Initial blob containing a JSON Object.
 *
 * @return A new JSON Object packet
 */
struct sol_flow_packet *sol_flow_packet_new_json_object(const struct sol_blob *value);

/**
 * @brief Retrieves the content of a JSON Object packet.
 *
 * @param packet The packet
 * @param value The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_json_object(const struct sol_flow_packet *packet, struct sol_blob **value);

/**
 * @brief Creates a new packet of type JSON Array.
 *
 * @note The blob content should be a string with a valid JSON Array.
 *
 * @note May or may not be @c NUL terminated.
 *
 * @param value Initial blob containing a JSON Array.
 *
 * @return A new JSON Array packet
 */
struct sol_flow_packet *sol_flow_packet_new_json_array(const struct sol_blob *value);

/**
 * @brief Retrieves the content of a JSON Array packet.
 *
 * @param packet The packet
 * @param value The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_json_array(const struct sol_flow_packet *packet, struct sol_blob **value);

/**
 * @brief Creates a new packet of type Drange.
 *
 * @param drange Initial value
 *
 * @return A new Drange packet
 *
 * @see sol_flow_packet_new_drange_value
 */
struct sol_flow_packet *sol_flow_packet_new_drange(const struct sol_drange *drange);

/**
 * @brief Creates a new packet of type Drange with initial value @c value and default spec.
 *
 * @param value Initial value
 *
 * @return A new Drange packet
 *
 * @see sol_flow_packet_new_drange
 */
struct sol_flow_packet *sol_flow_packet_new_drange_value(double value);

/**
 * @brief Retrieves the content of an Drange packet.
 *
 * @param packet The packet
 * @param drange The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 *
 * @see sol_flow_packet_get_drange_value
 */
int sol_flow_packet_get_drange(const struct sol_flow_packet *packet, struct sol_drange *drange);

/**
 * @brief Retrieves the Drange value of an Drange packet.
 *
 * @param packet The packet
 * @param value Drange value contained in the packet
 *
 * @return @c 0 if the value was successfully retrieved, error code (always negative) otherwise.
 *
 * @see sol_flow_packet_get_drange
 */
int sol_flow_packet_get_drange_value(const struct sol_flow_packet *packet, double *value);

/**
 * @brief Creates a new packet of type Error.
 *
 * @param code Error code
 * @param msg Error message
 *
 * @return A new Error packet
 */
struct sol_flow_packet *sol_flow_packet_new_error(int code, const char *msg);

/**
 * @brief Retrieves the content of an Error packet.
 *
 * @param packet The packet
 * @param code Retrieved error code
 * @param msg Retrieved error message
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_error(const struct sol_flow_packet *packet, int *code, const char **msg);

/**
 * @brief Creates a new packet of type RGB.
 *
 * @param rgb Initial RGB value
 *
 * @return A new RGB packet
 *
 * @see sol_flow_packet_new_rgb_components
 */
struct sol_flow_packet *sol_flow_packet_new_rgb(const struct sol_rgb *rgb);

/**
 * @brief Creates a new packet of type RGB from the given @c red, @c green and @c blue components.
 *
 * @param red Initial red value
 * @param green Initial green value
 * @param blue Initial blue value
 *
 * @return A new RGB packet
 *
 * @see sol_flow_packet_new_rgb
 */
struct sol_flow_packet *sol_flow_packet_new_rgb_components(uint32_t red, uint32_t green, uint32_t blue);

/**
 * @brief Retrieves the content of a RGB packet.
 *
 * @param packet The packet
 * @param rgb The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 *
 * @see sol_flow_packet_get_rgb_components
 */
int sol_flow_packet_get_rgb(const struct sol_flow_packet *packet, struct sol_rgb *rgb);

/**
 * @brief Retrieves the RGB components contained in a RGB packet.
 *
 * @param packet The packet
 * @param red Retrieved red component
 * @param green Retrieved green component
 * @param blue Retrieved blue component
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 *
 * @see sol_flow_packet_get_rgb
 */
int sol_flow_packet_get_rgb_components(const struct sol_flow_packet *packet, uint32_t *red, uint32_t *green, uint32_t *blue);

/**
 * @brief Creates a new packet of type Direction Vector.
 *
 * @param direction_vector Initial value
 *
 * @return A new Direction Vector packet
 *
 * @see sol_flow_packet_new_direction_vector_components
 */
struct sol_flow_packet *sol_flow_packet_new_direction_vector(const struct sol_direction_vector *direction_vector);

/**
 * @brief Creates a new packet of type Direction Vector from the given @c x, @c y and @c z components.
 *
 * @param x Initial x value
 * @param y Initial y value
 * @param z Initial z value
 *
 * @return A new Direction Vector packet
 *
 * @see sol_flow_packet_new_direction_vector
 */
struct sol_flow_packet *sol_flow_packet_new_direction_vector_components(double x, double y, double z);

/**
 * @brief Retrieves the content of a Direction Vector packet.
 *
 * @param packet The packet
 * @param direction_vector The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 *
 * @see sol_flow_packet_get_direction_vector_components
 */
int sol_flow_packet_get_direction_vector(const struct sol_flow_packet *packet, struct sol_direction_vector *direction_vector);

/**
 * @brief Retrieves the direction components contained in a Direction Vector packet.
 *
 * @param packet The packet
 * @param x Retrieved x component
 * @param y Retrieved y component
 * @param z Retrieved z component
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 *
 * @see sol_flow_packet_get_direction_vector
 */
int sol_flow_packet_get_direction_vector_components(const struct sol_flow_packet *packet, double *x, double *y, double *z);

/**
 * @brief Creates a new packet of type Location.
 *
 * @param location Initial value
 *
 * @return A new Location packet
 */
struct sol_flow_packet *sol_flow_packet_new_location(const struct sol_location *location);

/**
 * @brief Creates a new packet of type Location from the given @c lat, @c lon, @c alt components.
 *
 * @param lat Initial latitude value
 * @param lon Initial longitude value
 * @param alt Initial altitude value
 *
 * @return A new Location packet
 *
 * @see sol_flow_packet_new_location
 */
struct sol_flow_packet *sol_flow_packet_new_location_components(double lat, double lon, double alt);

/**
 * @brief Retrieves the content of a Location packet.
 *
 * @param packet The packet
 * @param location The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_location(const struct sol_flow_packet *packet, struct sol_location *location);

/**
 * @brief Retrieves the location components contained in a Location packet
 *
 * @param packet The packet
 * @param lat Retrieved latitude component
 * @param lon Retrieved longitutde component
 * @param alt Retrieved altitude component
 *
 * @return @c 0 if the content was successfullu retrieved, error code (always negative) otherwise.
 *
 * @see sol_flow_packet_get_location
 */
int sol_flow_packet_get_location_components(const struct sol_flow_packet *packet, double *lat, double *lon, double *alt);

/**
 * @brief Creates a new packet of type Timestamp.
 *
 * @param timestamp Initial value
 *
 * @return A new Timestamp packet
 */
struct sol_flow_packet *sol_flow_packet_new_timestamp(const struct timespec *timestamp);

/**
 * @brief Retrieves the content of a Timestamp packet.
 *
 * @param packet The packet
 * @param timestamp The retrieved content
 *
 * @return @c 0 if the content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_timestamp(const struct sol_flow_packet *packet, struct timespec *timestamp);

/**
 * @brief Creates a new packet type that is composed by the packets types in @c types.
 *
 * @param types Initial list of packet types
 *
 * @return A new composed packet type packet
 */
const struct sol_flow_packet_type *sol_flow_packet_type_composed_new(const struct sol_flow_packet_type **types);

/**
 * @brief Checks if a given packet type is a composed packet type.
 *
 * @param type Packet type to check
 *
 * @return @c true if packet type @c type is composed, @c false otherwise
 */
bool sol_flow_packet_is_composed_type(const struct sol_flow_packet_type *type);

/**
 * @brief Retrieves the list of packet types that composes @c type
 *
 * @param type The packet type
 * @param children Retrieved list of packet types that composes @c type
 * @param len Length of the list
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_flow_packet_get_composed_members_packet_types(const struct sol_flow_packet_type *type, const struct sol_flow_packet_type ***children, uint16_t *len);

/**
 * @brief Retrieves the list of packets contained in the composed @c packet.
 *
 * A composed packet is an instance of a composed packet type.
 *
 * @param packet The composed packet
 * @param children Retrieved list of packets that composes @c packet
 * @param len Length of the list
 *
 * @return @c 0 on success, error code (always negative) otherwise
 */
int sol_flow_packet_get_composed_members(const struct sol_flow_packet *packet, struct sol_flow_packet ***children, uint16_t *len);

/**
 * @brief Duplicates a packet.
 *
 * @param packet Packet to be duplicated
 *
 * @return The packet copy on success, @c NULL otherwise
 */
struct sol_flow_packet *sol_flow_packet_dup(const struct sol_flow_packet *packet);

/**
 * @brief Creates a new packet of type HTTP Response.
 *
 * @param response_code The response code
 * @param url Response URL
 * @param content_type The response content type
 * @param content The response content
 * @param cookies Response cookies
 * @param headers Response headers
 *
 * @return A new HTTP Response packet
 */
struct sol_flow_packet *sol_flow_packet_new_http_response(int response_code, const char *url, const char *content_type, const struct sol_blob *content, const struct sol_vector *cookies, const struct sol_vector *headers);

/**
 * @brief Retrieves the content of a Timestamp packet.
 *
 * @param packet The packet
 * @param response_code Retrieved response code
 * @param url Retrieved response URL
 * @param content_type Retrieved response content type
 * @param content Retrieved response content
 * @param cookies Retrieved response cookies
 * @param headers Retrieved response headers
 *
 * @return @c 0 if all content was successfully retrieved, error code (always negative) otherwise.
 */
int sol_flow_packet_get_http_response(const struct sol_flow_packet *packet, int *response_code, const char **url, const char **content_type, const struct sol_blob **content, struct sol_vector *cookies, struct sol_vector *headers);

/**
 * @brief Returns the packet type variable as string.
 *
 * This function will return the sol_flow_packet_type variable for a given type as string.
 * A common use for this function is when one is generating code for meta types nodes
 * that will be used by sol-fbp-generator.
 *
 * Example:
 *
 * @code
 * // will return the string "SOL_FLOW_PACKET_TYPE_IRANGE"
 * const char *int_packet_name = sol_flow_get_packet_type_name(sol_str_slice_from_str("int"));
 *
 * // will return the string "SOL_FLOW_PACKET_TYPE_LOCATION"
 * const char *location_packet_name = sol_flow_get_packet_type_name(sol_str_slice_from_str("location"));
 * @endcode
 *
 * @param type The Soletta type name (int, blob, error, string, location etc.)
 *
 * @return The Soletta packet type variable as string or @c NULL if the type was not found.
 */
const char *sol_flow_get_packet_type_name(const struct sol_str_slice type);

/**
 * @brief Returns the packet type from string.
 *
 * This function will return the sol_flow_packet_type based on its
 * string name. If the @a type starts with "composed:", then it will
 * search for composed packets with that signature and if none exists,
 * then it will try to create one.
 *
 * @param type The Soletta type name (int, blob, error, string,
 *        composed:int,boolean...)
 *
 * @return The Soletta packet type reference or @c NULL if the type
 * was not found.
 */
const struct sol_flow_packet_type *sol_flow_packet_type_from_string(const struct sol_str_slice type);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
