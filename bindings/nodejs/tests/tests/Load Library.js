var testUtils = require( "../assert-to-console" );
var theError = null;

console.log( JSON.stringify( { assertionCount: 1 } ) );

try {
	require( "../../../../lowlevel.js" );
} catch( anError ) {
	theError = anError;
}

testUtils.assert( "deepEqual", theError, null,
	"No error was thrown attempting to load the bindings" );
