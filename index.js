const assert = require('assert');
const {posix_spawn} = require('./build/Release/posix_spawn.node');

function exec (command, options, callback) {
	assert(typeof command === 'string', 'Argument command must be a string');
	if (typeof options === 'function') {
		callback = options;
		options = {}
	}
	if (typeof callback !== 'function') {
		callback = () => {};
	}
	posix_spawn(command, (err, status, stdout, stderr) => {
		if (err) return callback(err);
		if (status) return callback(new Error(`Non-zero exit code: ${status}`));
		callback(null, stdout, stderr);
	});
}

module.exports = {exec};
