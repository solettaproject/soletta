static const struct sol_flow_node_type *
create_0_root_type(void)
{
    static const struct sol_flow_node_type_gpio_writer_options opts1 =
        SOL_FLOW_NODE_TYPE_GPIO_WRITER_OPTIONS_DEFAULTS(
            .pin = {
                .val = 473,
            },
        );

    static const struct sol_flow_node_type_gpio_writer_options opts2 =
        SOL_FLOW_NODE_TYPE_GPIO_WRITER_OPTIONS_DEFAULTS(
            .pin = {
                .val = 475,
            },
        );

    static const struct sol_flow_node_type_gpio_writer_options opts3 =
        SOL_FLOW_NODE_TYPE_GPIO_WRITER_OPTIONS_DEFAULTS(
            .pin = {
                .val = 340,
            },
        );

    static const struct sol_flow_node_type_gpio_writer_options opts4 =
        SOL_FLOW_NODE_TYPE_GPIO_WRITER_OPTIONS_DEFAULTS(
            .pin = {
                .val = 474,
            },
        );

    static const struct sol_flow_static_conn_spec conns[] = {
        { 0, 0, 1, 0 },
        { 0, 1, 2, 0 },
        { 0, 2, 3, 0 },
        { 0, 3, 4, 0 },
        SOL_FLOW_STATIC_CONN_SPEC_GUARD
    };

    static const struct sol_flow_static_port_spec exported_in[] = {
        { 0, 0 },
        { 0, 1 },
        SOL_FLOW_STATIC_PORT_SPEC_GUARD
    };


    static struct sol_flow_static_node_spec nodes[] = {
        [0] = {NULL, "ctl", NULL},
        [1] = {NULL, "clear", (struct sol_flow_node_options *) &opts1},
        [2] = {NULL, "latch", (struct sol_flow_node_options *) &opts2},
        [3] = {NULL, "clock", (struct sol_flow_node_options *) &opts3},
        [4] = {NULL, "data", (struct sol_flow_node_options *) &opts4},
        SOL_FLOW_STATIC_NODE_SPEC_GUARD
    };

    struct sol_flow_static_spec spec = {
        .api_version = 1,
        .nodes = nodes,
        .conns = conns,
        .exported_in = exported_in,
        .exported_out = NULL,
    };

    nodes[0].type = SOL_FLOW_NODE_TYPE_CALAMARI_SEGMENTS_CTL;
    nodes[1].type = SOL_FLOW_NODE_TYPE_GPIO_WRITER;
    nodes[2].type = SOL_FLOW_NODE_TYPE_GPIO_WRITER;
    nodes[3].type = SOL_FLOW_NODE_TYPE_GPIO_WRITER;
    nodes[4].type = SOL_FLOW_NODE_TYPE_GPIO_WRITER;

    return sol_flow_static_new_type(&spec);
}

