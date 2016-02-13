var fs = require( "fs" );
var path = require( "path" );
var dirToCheck;

// Find the closest instance of soletta via its package.json
function closestSoletta( startDir ) {
	for ( dirToCheck = startDir; dirToCheck !== "/";
			dirToCheck = path.resolve( dirToCheck, ".." ) ) {
		try {
			if ( JSON.parse( fs.readFileSync(
					path.join( dirToCheck, "package.json" ), "utf8" ) ).name == "soletta" ) {
				return dirToCheck;
			}
		} catch( errors ) {}
	}
}

module.exports = closestSoletta;
