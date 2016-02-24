var soletta = require( "../../../../lowlevel" );
var testUtils = require( "../assert-to-console" );
var exec = require( "child_process" ).exec;
var fs = require( "fs" );
var path = require( "path" );
var childEnv = require( "lodash" ).extend( {}, process.env, { LD_PRELOAD: undefined } );

console.log( JSON.stringify( { assertionCount: 3 } ) );

// Use the UUID we were given as the soletta machine ID
process.env.SOL_MACHINE_ID = process.argv[ 2 ].replace( /[-]/g, "" );
testUtils.assert( "strictEqual", soletta.sol_platform_get_machine_id(),
	process.env.SOL_MACHINE_ID, "Machine ID value is as expected" );

exec( "hostname", { env: childEnv }, function( error, stdout ) {
	if ( error ) {
		testUtils.die( error );
	}
	testUtils.assert( "strictEqual", stdout.toString().trim(),
		soletta.sol_platform_get_hostname(), "Hostname matches `hostname`" );
} );

exec( "uname -r", { env: childEnv }, function( error, stdout ) {
	if ( error ) {
		testUtils.die( error );
	}
	testUtils.assert( "strictEqual", stdout.toString().trim(),
		soletta.sol_platform_get_os_version(), "OS version matches `uname -r`");
} );
