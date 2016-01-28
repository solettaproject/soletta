{
	"variables": {
		"BUILD_SOLETTA": '<!(bindings/nodejs/establish-flags.sh BUILD_SOLETTA)',
		"SOLETTA_CFLAGS": '<!(bindings/nodejs/establish-flags.sh SOLETTA_CFLAGS)',
		"SOLETTA_LIBS": '<!(bindings/nodejs/establish-flags.sh SOLETTA_LIBS)'
	},
	"conditions": [
		[ "'<(BUILD_SOLETTA)'=='true'", {
			"targets+": [
				{
					"target_name": "csdk",
					"type": "none",
					"actions": [ {
						"action_name": "build-csdk",
						"message": "Building C SDK",
						"outputs": [ "build/soletta_sysroot" ],
						"inputs": [ "" ],
						"action": [
							"sh",
							"bindings/nodejs/build-csdk.sh",
							'<!@(if test "x${npm_config_debug}x" == "xtruex"; then echo "--debug"; else echo ""; fi)'
						]
					} ]
				}
			]
		} ]
	],
	"targets": [
		{
			"target_name": "collectbindings",
			"type": "none",
			"actions": [ {
				"action_name": "collectbindings",
				"message": "Collecting bindings",
				"outputs": [ "bindings/nodejs/generated/main.cc" ],
				"inputs": [
					"bindings/nodejs/generated/main.cc.prologue",
					"bindings/nodejs/generated/main.cc.epilogue",
				],
				"action": [ "sh", "-c", "cd ./bindings/nodejs && ./generate-main.sh <(SOLETTA_CFLAGS)" ]
			} ],

			# Ensure that soletta is built first if it needs to be built at all
			"conditions": [
				[ "'<(BUILD_SOLETTA)'=='true'", {
					"dependencies": [ "csdk" ]
				} ]
			]
		},
		{
			"target_name": "soletta",
			"sources": [
				"bindings/nodejs/generated/main.cc",
				"bindings/nodejs/src/functions/simple.cc"
			],
			"include_dirs": [
				"<!(node -e \"require('nan')\")"
			],
			"cflags": [ '<(SOLETTA_CFLAGS)' ],
			"xcode_settings": {
				"OTHER_CFLAGS": [ '<SOLETTA_CFLAGS)' ]
			},
			"libraries": [ '<(SOLETTA_LIBS)' ],
			"dependencies": [ "collectbindings" ]
		}
	]
}
