var cities = [
	"Helsinki",
	"Hamilton",
	"Halifax",
	"Hong Kong",
	"Haifa",
	"Honolulu",
	"Harare",
	"Hiroshima"
];

module.exports = {
	generate: function() {
		return {
			destination: cities[ Math.round( Math.random() * ( cities.length - 1 ) ) ],
			speed: Math.round( Math.random() * 500 ) + 500
		};
	},
	validate: function( payload ) {
		return ( ( payload && cities.indexOf( payload.destination ) >= 0 && payload.speed >= 500 &&
			payload.speed < 1000 ) ? payload : "invalid" );
	}
};
