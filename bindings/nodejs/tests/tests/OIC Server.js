var soletta = require( require( "path" )
	.join( require( "../closestSoletta" )( __dirname ), "lowlevel" ) );
var testUtils = require( "../assert-to-console" );
var theResource;
var anError = {};

console.log( JSON.stringify( { assertionCount: 4 } ) );

testUtils.assert( "strictEqual", soletta.sol_oic_server_init(), 0,
	"sol_oic_server_init() is successful" );

theResource = soletta.sol_oic_server_add_resource( {
		path: "/a/" + process.argv[ 2 ],
		resource_type: "core.light",
		interface: "oic.if.baseline"
	},
	soletta.sol_oic_resource_flag.SOL_OIC_FLAG_ACTIVE |
	soletta.sol_oic_resource_flag.SOL_OIC_FLAG_DISCOVERABLE );

testUtils.assert( "ok", !!theResource, "sol_oic_server_add_resource() is successful" );

soletta.sol_oic_server_del_resource( theResource );
try {
	soletta.sol_oic_server_del_resource( theResource );
} catch( theError ) {
	anError = theError;
}

testUtils.assert( "strictEqual", anError instanceof TypeError, true,
	"Attempting to delete a resource twice causes a TypeError" );
testUtils.assert( "strictEqual", anError.message,
	"Object is not of type SolOicServerResource",
	"TypeError message is as expected" );
