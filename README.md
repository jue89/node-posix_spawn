# posix_spawn

This module binds [posix_spawn()](https://www.man7.org/linux/man-pages/man3/posix_spawn.3.html) to nodejs. This module aims to become a drop-in replacement for `child_process`, as `child_process` relies on [fork()](https://www.man7.org/linux/man-pages/man2/fork.2.html) and, thus, requires *enough* free memory to spawn child processes. (cf. [Node Issue #25382](https://github.com/nodejs/node/issues/25382))

## API

```js
const childProcess = require('posix_spawn');
```

### Method: exec()

```js
childProcess.exec(cmd[, options], [(err, rc, stdout, stderr) => {}])
```

Executes `cmd` with `/bin/sh`. When the child process exits, the callback function will be called: `rc` is a number hand holds the return code. `stdout` and `stderr` are `Buffer` and hold the data the child wrote into the respective pipe.
