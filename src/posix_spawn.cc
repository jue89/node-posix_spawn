#include <nan.h>
#include <spawn.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <stdlib.h>

#define READ  0
#define WRITE 1

#define MIN_BUFFER_FREE_SPACE 4096

static inline int max(int a, int b) {
	return (a > b) ? a : b;
}

class PosixSpawnWorker : public Nan::AsyncWorker {
private:
	posix_spawn_file_actions_t actions;
	posix_spawnattr_t attr;
	char *cmd;
	char *stdout_buf, *stderr_buf;
	size_t stdout_len, stderr_len;
	size_t stdout_ptr, stderr_ptr;
	int status;

public:
	PosixSpawnWorker(Nan::Callback *cb, const char *cmd) : Nan::AsyncWorker(cb) {
		this->cmd = strdup(cmd);
		posix_spawn_file_actions_init(&this->actions);
		posix_spawnattr_init(&this->attr);
	}

	~PosixSpawnWorker() {
		free(cmd);
		posix_spawn_file_actions_destroy(&this->actions);
		posix_spawnattr_destroy(&this->attr);
	}

	void Execute () {
		int rc;
		int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
		pid_t pid;
		char * args[] = {"sh", "-c", this->cmd, NULL};
		fd_set rfds_active;

		// setup pipes
		if (pipe(stdin_pipe) || pipe(stdout_pipe) || pipe(stderr_pipe)) {
			this->SetErrorMessage("Cannot create pipes");
			return;
		}

		// setup fd for child process
		posix_spawn_file_actions_addclose(&this->actions, stdin_pipe[WRITE]);
		posix_spawn_file_actions_adddup2(&this->actions, stdin_pipe[READ], STDIN_FILENO);
		posix_spawn_file_actions_addclose(&this->actions, stdin_pipe[READ]);
		posix_spawn_file_actions_addclose(&this->actions, stdout_pipe[READ]);
		posix_spawn_file_actions_adddup2(&this->actions, stdout_pipe[WRITE], STDOUT_FILENO);
		posix_spawn_file_actions_addclose(&this->actions, stdout_pipe[WRITE]);
		posix_spawn_file_actions_addclose(&this->actions, stderr_pipe[READ]);
		posix_spawn_file_actions_adddup2(&this->actions, stderr_pipe[WRITE], STDERR_FILENO);
		posix_spawn_file_actions_addclose(&this->actions, stderr_pipe[WRITE]);

		// enforce usage of vfork()
		posix_spawnattr_setflags(&this->attr, POSIX_SPAWN_USEVFORK);

		// spawn child
		rc = posix_spawnp(&pid, args[0], &this->actions, &this->attr, args, NULL);
		if (rc != 0) {
			this->SetErrorMessage("Spawn failed");
			return;
		}

		// setup fd for parent process
		close(stdin_pipe[WRITE]);
		close(stdin_pipe[READ]);
		close(stdout_pipe[WRITE]);
		close(stderr_pipe[WRITE]);

		// perpare buffers
		this->stdout_ptr = 0;
		this->stderr_ptr = 0;
		this->stdout_len = MIN_BUFFER_FREE_SPACE;
		this->stderr_len = MIN_BUFFER_FREE_SPACE;
		this->stdout_buf = (char*) malloc(this->stdout_len);
		this->stderr_buf = (char*) malloc(this->stderr_len);

		// keep track of open pipes
		FD_ZERO(&rfds_active);
		FD_SET(stdout_pipe[READ], &rfds_active);
		FD_SET(stderr_pipe[READ], &rfds_active);

		// get stdout_pipe and stderr_pipe
		while (1) {
			int nfds = -1;
			fd_set rfds;
			FD_ZERO(&rfds);

			// populate set of fds to observe
			if (FD_ISSET(stdout_pipe[READ], &rfds_active)) {
				FD_SET(stdout_pipe[READ], &rfds);
				nfds = max(nfds, stdout_pipe[READ]);
			}
			if (FD_ISSET(stderr_pipe[READ], &rfds_active)) {
				FD_SET(stderr_pipe[READ], &rfds);
				nfds = max(nfds, stderr_pipe[READ]);
			}

			// nothing to wait for ...
			if (nfds == -1) break;

			// wait for pipe to become readable
			rc = select(nfds + 1, &rfds, NULL, NULL, NULL);
			if (rc <= 0) break;

			// check for data
			if (FD_ISSET(stdout_pipe[READ], &rfds)) {
				rc = read(stdout_pipe[READ], &this->stdout_buf[this->stdout_ptr], this->stdout_len - this->stdout_ptr);
				if (rc > 0) {
					this->stdout_ptr += rc;
					this->stdout_len = this->stdout_ptr + MIN_BUFFER_FREE_SPACE;
					this->stdout_buf = (char*) realloc(this->stdout_buf, this->stdout_len);
				} else {
					FD_CLR(stdout_pipe[READ], &rfds_active);
					close(stdout_pipe[READ]);
				}
			}
			if (FD_ISSET(stderr_pipe[READ], &rfds)) {
				rc = read(stderr_pipe[READ], &this->stderr_buf[this->stderr_ptr], this->stderr_len - this->stderr_ptr);
				if (rc > 0) {
					this->stderr_ptr += rc;
					this->stderr_len = this->stderr_ptr + MIN_BUFFER_FREE_SPACE;
					this->stderr_buf = (char*) realloc(this->stderr_buf, this->stderr_len);
				} else {
					FD_CLR(stderr_pipe[READ], &rfds_active);
					close(stderr_pipe[READ]);
				}
			}
		}

		// retrieve status code
		waitpid(pid, &this->status, 0);
	}

	void HandleOKCallback () {
		Nan::HandleScope scope;

		Nan::MaybeLocal<v8::Object> stdout = Nan::NewBuffer(this->stdout_buf, this->stdout_ptr);
		Nan::MaybeLocal<v8::Object> stderr = Nan::NewBuffer(this->stderr_buf, this->stderr_ptr);

		v8::Local<v8::Value> argv[] = {
			Nan::Null(),
			Nan::New<v8::Number>(WEXITSTATUS(this->status)),
			stdout.ToLocalChecked(),
			stderr.ToLocalChecked()
		};

		this->callback->Call(4, argv, this->async_resource);
	}
};

NAN_METHOD(PosixSpawn) {
	Nan::MaybeLocal<v8::String> cmdMaybeLocal = Nan::To<v8::String>(info[0]);
	if (cmdMaybeLocal.IsEmpty()) {
		Nan::ThrowError("Argument 0 must be a string");
		return;
	}
	v8::Local<v8::String> cmdLocal = cmdMaybeLocal.ToLocalChecked();
	Nan::Utf8String cmd(cmdLocal);

	Nan::Callback *cb = new Nan::Callback(Nan::To<v8::Function>(info[1]).ToLocalChecked());

	AsyncQueueWorker(new PosixSpawnWorker(cb, *cmd));
}

NAN_MODULE_INIT(Init) {
	Nan::Set(
		target,
		Nan::New<v8::String>("posix_spawn").ToLocalChecked(),
		Nan::GetFunction(Nan::New<v8::FunctionTemplate>(PosixSpawn)).ToLocalChecked()
	);
}

NODE_MODULE(posix_spawn, Init)
