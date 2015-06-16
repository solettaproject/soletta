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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "sol-fbp.h"
#include "sol-file-reader.h"
#include "sol-flow-builder.h"
#include "sol-flow-internal.h"
#include "sol-flow.h"
#include "sol-log-internal.h"
#include "sol-mainloop.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util.h"
#include "sol-vector.h"

SOL_LOG_INTERNAL_DECLARE_STATIC(_log_domain, "flow-to-dot");
#define SLICE_PRINT(slice) (int)slice.len, slice.data

static struct sol_str_slice
get_node_name(const struct sol_fbp_node *node)
{
    static const struct sol_str_table_ptr component_to_glyph[] = {
        SOL_STR_TABLE_PTR_ITEM("timer", "🕐 Timer"),
        SOL_STR_TABLE_PTR_ITEM("boolean/and", "∧ And"),
        SOL_STR_TABLE_PTR_ITEM("boolean/or", "∨ Or"),
        SOL_STR_TABLE_PTR_ITEM("boolean/not", "¬ Not"),
        SOL_STR_TABLE_PTR_ITEM("boolean/xor", "⊕ Xor"),
        { }
    };
    const char *glyph;

    if (sol_str_table_ptr_lookup(component_to_glyph, node->component, &glyph))
        return sol_str_slice_from_str(glyph);

    return node->name;
}

static uint32_t
crc24(uint32_t crc, const char *ptr, size_t len)
{
    while (len--) {
        size_t bit;

        crc ^= *ptr++ << (24 - 8);
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x800000UL)
                crc = (crc << 1) ^ 0x783836UL; // x86 in ASCII
            else
                crc <<= 1UL;
            crc &= 0xffffffUL;
        }
    }

    return crc;
}

static uint32_t
rgb_to_yiq(uint32_t rgb)
{
    uint8_t r = (rgb >> 16) & 0xff;
    uint8_t g = (rgb >> 8) & 0xff;
    uint8_t b = rgb & 0xff;

    return ((r * 299) + (g * 587) + (b * 114)) / 1000;
}

static uint32_t
get_connection_color(const struct sol_fbp_node *in, const struct sol_fbp_node *out,
    const struct sol_fbp_conn *conn)
{
    uint32_t color = 0;

    color = crc24(color, conn->src_port.data, conn->src_port.len);
    color = crc24(color, in->component.data, in->component.len);
    color = crc24(color, out->component.data, out->component.len);
    color = crc24(color, conn->dst_port.data, conn->dst_port.len);
    color = crc24(color, (const char *)&conn->dst, sizeof(conn->dst));
    color = crc24(color, (const char *)&conn->src, sizeof(conn->src));

    if (rgb_to_yiq(color) > 0xBB)   /* avoid almost-white colors by flipping bits */
        return color ^ 0xa5a5a5;
    return color;
}

static uint32_t
get_node_color(const struct sol_fbp_node *node)
{
    char *slash = memchr(node->component.data, '/', node->component.len);

    if (slash)
        return crc24(0, node->component.data, slash - node->component.data);

    return crc24(0, node->component.data, node->component.len);
}

static uint32_t
calculate_contrasting_color(uint32_t color)
{
    return !(rgb_to_yiq(color) >= 128) * 0xffffff;
}

static uint32_t
darken_color(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xff;
    uint8_t g = (color >> 8) & 0xff;
    uint8_t b = color & 0xff;

    return ((r * 3) / 4) << 16 | ((g * 3) / 4) << 8 | ((b * 3) / 4);
}

static int
convert_fbp_to_dot(struct sol_fbp_graph *g, const char *out)
{
    uint16_t idx = 0;
    struct sol_fbp_conn *conn = NULL;
    struct sol_fbp_node *node = NULL;
    FILE *dot_file = NULL;

    dot_file = fopen(out, "w+");
    if (!dot_file) {
        fprintf(stderr, "Couldn't open input file %s : %s", out,  sol_util_strerrora(errno));
        return 1;
    }

    fprintf(dot_file,
        "digraph fbp {\n"
        "\trankdir = LR\n"
        "\tlabelalloc = 3\n"
        "\tconcentrate = true\n"
        "\tnode [\n"
        "\t\tfontsize = \"14\"\n"
        "\t\tfontname = \"helvetica\"\n"
        "\t];\n"
        "\tedge [\n"
        "\t\tfontsize = \"8\"\n"
        "\t\tfontname = \"helvetica\"\n"
        "\t\tarrowsize = 0.5\n"
        "\t\tarrowhead = dot\n"
        "\t\tstyle = bold\n"
        "\t];\n"
        );

    /* The dot format specifies that the old 'record' types should be saved using
     * HTML based labels instead of hardcoded records. so we need to create a table
     * with cells that will resemble the correct structure that we need.
     *
     * the PORT constructor tells graphviz that it's a 'named cell' and we can direct
     * edges directly to it by using Node:CellName -> OtherNode:CellName
     */

    SOL_VECTOR_FOREACH_IDX (&g->nodes, node, idx) {
        int i;
        struct sol_fbp_port *port;
        uint32_t node_color = get_node_color(node);
        uint32_t input_color = darken_color(node_color);
        uint32_t output_color = darken_color(input_color);
        uint32_t border_color = darken_color(output_color);
        uint32_t input_label_color = calculate_contrasting_color(input_color);
        uint32_t output_label_color = calculate_contrasting_color(output_color);

        fprintf(dot_file, "\t\"%.*s\" [\n", SLICE_PRINT(node->name));
        fprintf(dot_file, "\t\tshape = \"none\"\n");
        fprintf(dot_file, "\t\tlabel = <<table border=\"0\" cellspacing=\"0\" color=\"#%06x\">\n",
            border_color);

        if (strncmp(node->name.data, "#anon:", sizeof("#anon:") - 1) == 0) {
            fprintf(dot_file, "\t\t\t<tr><td border=\"1\" bgcolor=\"#%06x\"><font color=\"#%06x\">"
                "%.*s</font></td></tr>\n",
                node_color, calculate_contrasting_color(node_color),
                SLICE_PRINT(node->component));
        } else {
            fprintf(dot_file, "\t\t\t<tr><td border=\"1\" bgcolor=\"#%06x\"><font color=\"#%06x\">"
                "%.*s<br/><font point-size=\"8\">%.*s</font></font></td></tr>\n",
                node_color, calculate_contrasting_color(node_color),
                SLICE_PRINT(get_node_name(node)), SLICE_PRINT(node->component));
        }

        SOL_VECTOR_FOREACH_IDX (&node->in_ports, port, i) {
            fprintf(dot_file, "\t\t\t<tr><td port=\"%.*s\" border=\"1\" align=\"left\" bgcolor=\"#%06x\">"
                "<font point-size=\"10\" color=\"#%06x\">◎ %.*s</font></td></tr>\n",
                SLICE_PRINT(port->name), input_color, input_label_color, SLICE_PRINT(port->name));
        }
        SOL_VECTOR_FOREACH_IDX (&node->out_ports, port, i) {
            fprintf(dot_file, "\t\t\t<tr><td port=\"%.*s\" border=\"1\" align=\"right\" bgcolor=\"#%06x\">"
                "<font point-size=\"10\" color=\"#%06x\">%.*s ◉</font></td></tr>\n",
                SLICE_PRINT(port->name), output_color, output_label_color, SLICE_PRINT(port->name));
        }
        fprintf(dot_file, "\t\t</table>>\n\t];\n");
    }

    SOL_VECTOR_FOREACH_IDX (&g->conns, conn, idx) {
        struct sol_fbp_node *in_node = sol_vector_get(&g->nodes, conn->src);
        struct sol_fbp_node *out_node = sol_vector_get(&g->nodes, conn->dst);

        /* Node:Port -> Node:Port */
        fprintf(dot_file, "\t\"%.*s\":%.*s:e -> \"%.*s\":%.*s:w [color=\"#%06x\"]\n",
            SLICE_PRINT(in_node->name),
            SLICE_PRINT(conn->src_port),
            SLICE_PRINT(out_node->name),
            SLICE_PRINT(conn->dst_port),
            get_connection_color(in_node, out_node, conn));

        /* FIXME: append [label = "port type"] to each connection */
    }

    fprintf(dot_file, "}\n");
    fclose(dot_file);
    return 0;
}

static void
print_help(void)
{
    printf("sol-fbp-to-dot : easily convert between fbp graph format to dot format.\n\n");
    printf("Usage:\n");
    printf("\t sol-fbp-to-dot --fbp=file.fbp\n\t\t--dot=outfile.dot\n\t\t--process-type=png\n\t\t--graphviz=/usr/bin/dot");
    printf("\n\n");
    printf("\t --fbp\tthe input graph file\n");
    printf("\t --dot\tthe output dot resulting file\n\n");
}

static char *
parse_params(int argc, char *argv[], const char *token)
{
    char *token_begin = NULL;
    char *ret_value = NULL;
    int str_length;
    int i = 1;

    for (; i < argc; ++i) {
        if ((token_begin = strstr(argv[i], token))) {
            token_begin += strlen(token) + 1; // adds the '=' sign.
            str_length = strlen(token_begin);
            ret_value = malloc(sizeof(char) * (str_length + 1));
            if (!ret_value)
                return NULL;

            strcpy(ret_value, token_begin);
            ret_value[str_length] = '\0';
            return ret_value;
        }
    }
    return NULL;
}

static int
init_graph_from_file(struct sol_fbp_graph *g,  struct sol_file_reader *fr, const char *filename)
{
    struct sol_fbp_error *fbp_error;

    sol_fbp_graph_init(g);

    fbp_error = sol_fbp_parse(sol_file_reader_get_all(fr), g);
    if (fbp_error) {
        sol_fbp_log_print(filename, fbp_error->position.line, fbp_error->position.column, fbp_error->msg);
        sol_fbp_error_free(fbp_error);
        sol_file_reader_close(fr);
        return EXIT_FAILURE;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    char *fbpfile = NULL;
    char *dotfile = NULL;
    struct sol_file_reader *fr;

    struct sol_fbp_graph input_graph;

    sol_init();
    sol_log_domain_init_level(&_log_domain);

    if (argc == 1) {
        goto usage_error;
    }

    fbpfile = parse_params(argc, argv, "--fbp");
    dotfile = parse_params(argc, argv, "--dot");
    if (!fbpfile) {
        printf("You need to indicate a fbp file\n");
        goto usage_error;
    }
    if (!dotfile) {
        printf("You need to indicate a dot file\n");
        goto usage_error;
    }

    fr = sol_file_reader_open(fbpfile);
    if (!fr) {
        fprintf(stderr, "couldn't open input file '%s': %s", fbpfile, sol_util_strerrora(errno));
        goto quit;
    }

    if (init_graph_from_file(&input_graph, fr, fbpfile)) {
        sol_fbp_graph_fini(&input_graph);
        goto quit;
    }

    if (convert_fbp_to_dot(&input_graph, dotfile)) {
        fprintf(stderr, "Couldn't convert from fbp to dot. \n Verify that you'r FBP file conforms to the standard.");
    }
    sol_file_reader_close(fr);
    sol_fbp_graph_fini(&input_graph);

    goto quit;

usage_error:
    print_help();

quit:
    free(fbpfile);
    free(dotfile);
    sol_shutdown();
    return 0;
}
