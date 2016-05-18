{
	"variables": {
		"BUILD_SOLETTA": '<!(test "x${SOLETTA_CFLAGS}x" = "xx" -o "x${SOLETTA_LIBS}x" = "xx" && echo "true" || echo "false")',
	},
	"conditions": [
		[ "'<(BUILD_SOLETTA)'=='true'", {
			"targets": [
				{
					"target_name": "csdk",
					"type": "none",
					"actions": [ {
						"action_name": "build-csdk",
						"message": "Building C SDK",
						"outputs": [ "build/soletta_sysroot" ],
						"inputs": [ "" ],
						"action": [ "sh", "bindings/nodejs/build-for-npm.sh" ]
					} ]
				}
			]
		}, {
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
						"action": [
							"sh",
							"-c",
							'./bindings/nodejs/generate-main.sh'
						]
					} ]
				},
				{
					"target_name": "copyapis",
					"type": "none",
					"actions": [ {
						"action_name": "copyapis",
						"message": "Copying JS APIs",
						"inputs": [ "./bindings/nodejs/lib" ],
						"outputs": [ "" ],
						"action": [
							"sh",
							"-c",
							"cp -a ./bindings/nodejs/lib/* ."
						]
					} ]
				},
				{
					"target_name": "soletta",
					"includes": [
						"bindings/nodejs/generated/nodejs-bindings-sources.gyp"
					],
					"include_dirs": [
						"<!(node -e \"require('nan')\")"
					],
					"cflags": [ '<!@(echo "${SOLETTA_CFLAGS}")' ],
					"xcode_settings": {
						"OTHER_CFLAGS": [ '<!@(echo "${SOLETTA_CFLAGS}")' ]
					},
					"libraries": [ '<!@(echo "${SOLETTA_LIBS}")' ],
					"dependencies": [ "collectbindings", "copyapis" ]
				}
			]
		} ]
	]
}
