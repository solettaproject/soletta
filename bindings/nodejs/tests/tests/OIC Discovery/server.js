var soletta = require( require( "path" )
	.join( require( "../../closestSoletta" )( __dirname ), "lowlevel" ) );
var testUtils = require( "../../assert-to-console" );
var putRequests = 0;
var theResource;

console.log( JSON.stringify( { assertionCount: 2 } ) );

soletta.sol_oic_server_init();

theResource = soletta.sol_oic_server_add_resource( {
		interface: "oic.if.baseline",
		resource_type: "core.light",
		path: "/a/" + process.argv[ 2 ],
		put: function putHandler( clientAddress, input, output ) {
			testUtils.assert( "deepEqual", input, { uuid: process.argv[ 2 ] },
				"Server: PUT request payload is as expected" );
			output.putRequests = ++putRequests;
			return soletta.sol_coap_responsecode_t.SOL_COAP_RSPCODE_OK;
		}
	}, soletta.sol_oic_resource_flag.SOL_OIC_FLAG_DISCOVERABLE |
		soletta.sol_oic_resource_flag.SOL_OIC_FLAG_ACTIVE );

console.log( JSON.stringify( { ready: true } ) );

process.on( "exit", function() {
	soletta.sol_oic_server_del_resource( theResource );
} );
