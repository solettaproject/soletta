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

sample-$(FLOW_C_API_SIMPLECTYPE_SAMPLE) += simplectype
sample-simplectype-$(FLOW_C_API_SIMPLECTYPE_SAMPLE) := simplectype.c
