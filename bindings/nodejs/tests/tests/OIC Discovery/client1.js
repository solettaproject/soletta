var messagePrefix = "Client 1: ";
var async = require( "async" );
var _ = require( "lodash" );
var soletta = require( require( "path" )
	.join( require( "../../closestSoletta" )( __dirname ), "lowlevel" ) );
var testUtils = require( "../../assert-to-console" );
var lastDiscoveryReturnValue;
var theResource;

console.log( JSON.stringify( { assertionCount: 4 } ) );

var defaultAddress = {
	bytes: _.fill( Array( 16 ), 0 ),
	family: soletta.sol_network_family.SOL_NETWORK_FAMILY_INET,
	port: 0
};

var client = soletta.sol_oic_client_new();

var destination = soletta.sol_network_addr_from_str( {
		bytes: _.fill( Array( 16 ), 0 ),
		family: soletta.sol_network_family.SOL_NETWORK_FAMILY_INET,
		port: 5683
	}, "224.0.1.187" );
testUtils.assert( "ok", !!destination, messagePrefix + "sol_network_addr_from_str() successful" );

async.series( [
	function findResource( callback ) {
		soletta.sol_oic_client_find_resource( client, destination, "",
			function( client, resource ) {
				lastDiscoveryReturnValue = true;
				if ( resource && resource.href === "/a/" + process.argv[ 2 ] ) {
					testUtils.assert( "ok", true, messagePrefix + "Resource found" );
					lastDiscoveryReturnValue = false;
					theResource = resource;
					callback();
				}
				return lastDiscoveryReturnValue;
			} );
	},
	function tellServerImDone( callback ) {
		soletta.sol_oic_client_resource_request( client, theResource,
			soletta.sol_coap_method_t.SOL_COAP_METHOD_PUT, { uuid: process.argv[ 2 ] },
				function( code, client, address, response ) {
					testUtils.assert( "strictEqual", code,
						soletta.sol_coap_responsecode_t.SOL_COAP_RSPCODE_OK,
						messagePrefix + "server acknowledged PUT request" );

					// If the server has heard from all of us clients, we can conclude the test
					if ( response.putRequests >= 2 ) {
						console.log( JSON.stringify( { killPeer: true } ) );
					}
					callback();
				} );
	}
] );

process.on( "exit", function() {
	testUtils.assert( "strictEqual", lastDiscoveryReturnValue, false,
		messagePrefix + "Last discovery return value was false" );
} );
