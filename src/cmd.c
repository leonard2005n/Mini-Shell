// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	/* TODO: Execute cd. */

	return chdir(dir->string);
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	/* TODO: Execute exit/quit. */

	return SHELL_EXIT; /* TODO: Replace with actual exit code. */
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	/* TODO: Sanity checks. */

	/* TODO: If builtin command, execute the command. */

	/* TODO: If variable assignment, execute the assignment and return
	 * the exit status.
	 */

	/* TODO: If external command:
	 *   1. Fork new process
	 *     2c. Perform redirections in child
	 *     3c. Load executable in child
	 *   2. Wait for child
	 *   3. Return exit status
	 */

	if (strcmp(s->verb->string, "exit") == 0) {
		return shell_exit();
	}

	if (strcmp(s->verb->string, "cd") == 0) {

		if (!s->params) {
			return 0;
		}

		if (s->out) {
			int fd;
			if ((s->io_flags & IO_OUT_APPEND) != IO_OUT_APPEND) {
				fd = open(s->out->string, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			} else {
				fd = open(s->out->string, O_WRONLY | O_APPEND, 0666);
			}
			close(fd);
		}

		int fd = 3;

		if (s->err) {

			if ((s->io_flags & IO_ERR_APPEND) != IO_ERR_APPEND) {
				fd = open(s->err->string, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			} else {
				fd = open(s->err->string, O_WRONLY | O_APPEND | O_CREAT, 0666);
			}
		}

		return shell_cd(s->params);
	}

	char *command = get_word(s->verb);

	if (strchr(command, '=')) {

		char *word = get_word(s->verb->next_part->next_part);
		setenv(s->verb->string, word, 1);
		free(word);
		free(command);
		return 0;
	}

	free(command);

	int childpid = fork();
	int ret;

	int size = 0;
	char **argv = get_argv(s, &size);

	if (childpid == 0) {

		if (s->out) {
			int fd;

			char *file_name = get_word(s->out);
			if ((s->io_flags & IO_OUT_APPEND) != IO_OUT_APPEND) {
				fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			} else {
				fd = open(file_name, O_WRONLY | O_APPEND | O_CREAT, 0666);
			}

			free(file_name);
			dup2(fd, 1);
			close(fd);
		}

		if (s->in) {

			char *file_name = get_word(s->in);
			int fd = open(file_name, O_RDONLY, 0666);

			free(file_name);
			dup2(fd, 0);
			close(fd);
		}

		if (s->err) {
			int fd;

			char *file_name = get_word(s->err);
			if (s->out != NULL && strcmp(s->out->string, s->err->string) == 0) {
				fd = 1;
			} else if ((s->io_flags & IO_ERR_APPEND) != IO_ERR_APPEND) {
				fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			} else {
				fd = open(file_name, O_WRONLY | O_APPEND | O_CREAT, 0666);
			}

			free(file_name);
			dup2(fd, 2);

			if (!(s->out != NULL && strcmp(s->out->string, s->err->string) == 0)) {
				close(fd);
			}
		}

		execvp(s->verb->string, argv);
	} else {
		int status;
		waitpid(childpid, &status, 0);

		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);

		for (int i = 0; i < size; i++) {
			free(argv[i]);
		}

		free(argv);
	}

	return ret;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Execute cmd1 and cmd2 simultaneously. */\

	int ret;

	int childpid = fork();

	if (childpid == 0) {
		ret = parse_command(cmd1, level + 1, father);
		exit(ret);
	} else {
		int status;

		childpid = fork();

		if (childpid == 0) {
			ret = parse_command(cmd2, level + 1, father);
			exit(ret);
		}

		waitpid(childpid, &status, 0);
		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);
	}

	return ret; /* TODO: Replace with actual exit status. */
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Redirect the output of cmd1 to the input of cmd2. */

	int ret;
	int fd[2];
	pipe(fd);

	int childpid1 = fork();
	
	if (childpid1 == 0) {
		close(fd[0]);


		dup2(fd[1], 1);
		ret = parse_command(cmd1, level + 1, father);
		close(fd[1]);
		
		exit(ret);
	} else {

		int status;
		int childpid2 = fork();

		if (childpid2 == 0) {
			close(fd[1]);


			dup2(fd[0], 0);
			ret = parse_command(cmd2, level + 1, father);
			close(fd[0]);
			exit(ret);
		}

		close(fd[0]);
		close(fd[1]);

		waitpid(childpid1, &status, 0);
		waitpid(childpid2, &status, 0);

		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);

	}
	

	return ret; /* TODO: Replace with actual exit status. */
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	/* TODO: sanity checks */
	int ret;

	if (c->op == OP_NONE) {
		/* TODO: Execute a simple command. */
		ret = parse_simple(c->scmd, level, father);
		return ret;
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* TODO: Execute the commands one after the other. */

		parse_command(c->cmd1, level + 1, c);

		ret = parse_command(c->cmd2, level + 1, c);

		break;

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */

		ret = run_in_parallel(c->cmd1, c->cmd2, level, c);

		break;

	case OP_CONDITIONAL_NZERO:
		/* TODO: Execute the second command only if the first one
		 * returns non zero.
		 */

		ret = parse_command(c->cmd1, level + 1, c);

		if (ret != 0) {
			ret = parse_command(c->cmd2, level + 1, c);
		}

		break;

	case OP_CONDITIONAL_ZERO:
		/* TODO: Execute the second command only if the first one
		 * returns zero.
		 */

		ret = parse_command(c->cmd1, level + 1, c);

		if (ret == 0) {
			ret = parse_command(c->cmd2, level + 1, c);
		}

		break;

	case OP_PIPE:
		/* TODO: Redirect the output of the first command to the
		 * input of the second.
		 */

		ret = run_on_pipe(c->cmd1, c->cmd2, level, c);
		
		break;

	default:
		return SHELL_EXIT;
	}

	return ret; /* TODO: Replace with actual exit code of command. */
}
