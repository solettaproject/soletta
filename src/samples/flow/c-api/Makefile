sample-$(FLOW_C_API_CUSTOM_NODE_TYPES_SAMPLE) += custom-node-types
sample-custom-node-types-$(FLOW_C_API_CUSTOM_NODE_TYPES_SAMPLE) := custom-node-types.o
sample-custom-node-types-$(FLOW_C_API_CUSTOM_NODE_TYPES_SAMPLE) += custom-node-types.json

sample-$(FLOW_C_API_HIGHLEVEL_SAMPLE) += highlevel
sample-highlevel-$(FLOW_C_API_HIGHLEVEL_SAMPLE) := highlevel.c
sample-highlevel-$(FLOW_C_API_HIGHLEVEL_SAMPLE) += custom-node-types.json
sample-highlevel-$(FLOW_C_API_HIGHLEVEL_SAMPLE)-deps := sample-custom-node-types

sample-$(FLOW_C_API_LOWLEVEL_SAMPLE) += lowlevel
sample-lowlevel-$(FLOW_C_API_LOWLEVEL_SAMPLE) := lowlevel.c
sample-lowlevel-$(FLOW_C_API_LOWLEVEL_SAMPLE) += custom-node-types.json
sample-lowlevel-$(FLOW_C_API_LOWLEVEL_SAMPLE)-deps := \
	sample-custom-node-types

sample-$(FLOW_C_API_SIMPLE_C_TYPE_SAMPLE) += simple-c-type
sample-simple-c-type-$(FLOW_C_API_SIMPLE_C_TYPE_SAMPLE) := simple-c-type.c

sample-$(FLOW_C_API_FIND_TYPE_SAMPLE) += find-type
sample-find-type-$(FLOW_C_API_FIND_TYPE_SAMPLE) := find-type.c
sample-find-type-$(FLOW_C_API_FIND_TYPE_SAMPLE)-extra-ldflags := -ldl

sample-$(FLOW_C_API_SINGLE_NODE_SAMPLE) += single-node
sample-single-node-$(FLOW_C_API_SINGLE_NODE_SAMPLE) := single-node.c
