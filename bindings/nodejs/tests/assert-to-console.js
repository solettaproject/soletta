module.exports = {

	// Create an assertion and pass it to the parent process via stdout
	assert: function( assertion ) {
		var copyOfArguments;

		// Copy the arguments and remove the assertion
		copyOfArguments = Array.prototype.slice.call( arguments, 0 );
		copyOfArguments.shift();

		console.log( JSON.stringify( {
			assertion: assertion,
			arguments: copyOfArguments
		} ) );
	},

	die: function( message ) {
		console.log( JSON.stringify( { teardown: true, message: message, isError: true } ) );
		process.exit( 1 );
	}
};
