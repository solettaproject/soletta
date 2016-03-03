var async = require( "async" );
var utils = require( "../assert-to-console" );
var soletta = require( require( "path" )
	.join( require( "../closestSoletta" )( __dirname ), "lowlevel" ) );
var exec = require( "child_process" ).exec;
var initialHostname = require( "os" ).hostname();
var initialTimezone, destinationTimezone;

console.log( JSON.stringify( { assertionCount: 3 } ) );

// commands like sudo don't like having an LD_PRELOAD
delete process.env.LD_PRELOAD;

var hostnameMonitor, timezoneMonitor;

async.series( [
	function getInitialTimezone( callback ) {
		exec( "timedatectl", function( error, stdout ) {
			if ( !error ) {
				initialTimezone = stdout
					.toString()
					.split( "\n" )
					.reduce( function( previousValue, currentValue ) {
						if ( !previousValue ) {
							previousValue = ( currentValue
								.match( /^\s+Time\s*zone:\s*([^ ]*).*$/ ) || [ 0, false ] )[ 1 ];
						}
						return previousValue;
					}, false );
				if ( !initialTimezone ) {
					error = new Error( "Can't establish initial time zone" );
				}
			}
			callback( error );
		} );
	},

	function pickDifferentTimezone( callback ) {
		exec( "timedatectl list-timezones", function( error, stdout ) {
			if ( !error ) {
				destinationTimezone = stdout
					.toString()
					.split( "\n" )
					.reduce( function( previousValue, currentValue, theIndex, theArray ) {
						if ( typeof previousValue !== "string" &&
							Math.round( ( theArray.length - 1 ) * previousValue ) === theIndex ) {
							previousValue = currentValue;
						}
						return previousValue;
					}, Math.random() );
				if ( typeof destinationTimezone !== "string" ) {
					error = new Error( "Can't establish time zone to use for the test" );
				}
			}
			callback( error );
		} );
	},

	function testHostnameMonitor( callback ) {
		hostnameMonitor = soletta.sol_platform_add_hostname_monitor( function( newHostname ) {
			utils.assert( "strictEqual", newHostname, initialHostname,
				"The new hostname is as expected" );
			callback();
		} );
		exec( "sudo hostname " + initialHostname, function( error ) {
			if ( error ) {
				callback( error );
			}
		} );
	},

	function deleteHostnameMonitorAndAddTimezoneMonitor( callback ) {

		// It is important that the timezone monitor be added immediate after the hostname montior
		// has been removed, without an intervening main loop iteration. This tests the synchronous
		// sequence of hijack_ref()/hijack_unref()
		soletta.sol_platform_del_hostname_monitor( hostnameMonitor );
		timezoneMonitor = soletta.sol_platform_add_timezone_monitor( function( newTimeZone ) {
			utils.assert( "strictEqual", newTimeZone, destinationTimezone,
				"The new time zone is '" + destinationTimezone + "'" );
			callback();
		} );
		exec( "sudo timedatectl set-timezone " + destinationTimezone, function( error ) {
			if ( error ) {
				callback( error );
			}
		} );
	},

	function deleteTimezoneMonitorAndRestoreTimezone( callback ) {
		soletta.sol_platform_del_timezone_monitor( timezoneMonitor );
		exec( "sudo timedatectl set-timezone " + initialTimezone, callback );
	}
], function( error ) {
	if ( error ) {
		utils.die( error.message );
	}
} );

process.on( "exit", function() {
	utils.assert( "ok", true, "The process exits when the last handle is released" );
} );
