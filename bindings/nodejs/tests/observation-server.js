module.exports = function( setObservable ) {

var _ = require( "lodash" );
var soletta = require( require( "path" )
	.join( require( "./closestSoletta" )( __dirname ), "lowlevel" ) );
var testUtils = require( "./assert-to-console" );
var payload = require( "./payload" );
var uuid = process.argv[ 2 ];
var observationCount = 0;
var theResource;
var theInterval;

console.log( JSON.stringify( { assertionCount: 0 } ) );

soletta.sol_oic_server_init();

theResource = soletta.sol_oic_server_add_resource( _.extend( {
		interface: "oic.if.baseline",
		resource_type: "core.light",
		path: "/a/" + uuid,
		put: function putHandler( clientAddress, input, output ) {
			if ( input.finished === uuid ) {
				observationCount++;
				output.clientsFinished = observationCount;
			}
			if ( observationCount >= 2 && !setObservable ) {
				clearInterval(theInterval);
			}
			return soletta.sol_coap_responsecode_t.SOL_COAP_RSPCODE_OK;
		},
	}, setObservable ? {} : {
		get: function getHandler( clientAddress, input, output ) {
			_.extend( output, payload.generate() );
			return soletta.sol_coap_responsecode_t.SOL_COAP_RSPCODE_OK;
		}
	} ),
		soletta.sol_oic_resource_flag.SOL_OIC_FLAG_DISCOVERABLE |
		( setObservable ? soletta.sol_oic_resource_flag.SOL_OIC_FLAG_OBSERVABLE : 0 ) |
		soletta.sol_oic_resource_flag.SOL_OIC_FLAG_ACTIVE );

console.log( JSON.stringify( { ready: true } ) );

if ( !setObservable ) {
	theInterval = setInterval( function() {
		soletta.sol_oic_notify_observers( theResource, payload.generate() );
	}, 200 );
}

process.on( "exit", function() {
	soletta.sol_oic_server_del_resource( theResource );
} );

};
