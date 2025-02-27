/*
 * kernel/shell/shell.c
 *
 * Copyright(c) 2007-2021 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <xboot.h>
#include <command/command.h>
#include <shell/readline.h>
#include <shell/shell.h>

static char shell_cwd[VFS_MAX_PATH] = { '/', '\0', };
static int shell_cwd_fd = -1;
static spinlock_t shell_cwd_lock = SPIN_LOCK_INIT();

int shell_realpath(const char * path, char * fpath)
{
	char * p, * q, * s;
	int left_len, full_len;
	char left[VFS_MAX_PATH];
	char next_token[VFS_MAX_PATH];

	if(path[0] == '/')
	{
		fpath[0] = '/';
		fpath[1] = '\0';
		if(path[1] == '\0')
			return 0;

		full_len = 1;
		left_len = strlcpy(left, (const char *)(path + 1), sizeof(left));
	}
	else
	{
		spin_lock(&shell_cwd_lock);
		strlcpy(fpath, shell_cwd, VFS_MAX_PATH);
		spin_unlock(&shell_cwd_lock);
		full_len = strlen(fpath);
		left_len = strlcpy(left, path, sizeof(left));
	}
	if((left_len >= sizeof(left)) || (full_len >= VFS_MAX_PATH))
		return -1;

	while(left_len != 0)
	{
		p = strchr(left, '/');
		s = p ? p : left + left_len;
		if((int)(s - left) >= sizeof(next_token))
			return -1;

		memcpy(next_token, left, s - left);
		next_token[s - left] = '\0';
		left_len -= s - left;
		if(p != NULL)
		{
			memmove(left, s + 1, left_len + 1);
		}

		if(fpath[full_len - 1] != '/')
		{
			if (full_len + 1 >= VFS_MAX_PATH)
				return -1;

			fpath[full_len++] = '/';
			fpath[full_len] = '\0';
		}
		if(next_token[0] == '\0' || strcmp(next_token, ".") == 0)
		{
			continue;
		}
		else if(strcmp(next_token, "..") == 0)
		{
			if(full_len > 1)
			{
				fpath[full_len - 1] = '\0';
				q = strrchr(fpath, '/') + 1;
				*q = '\0';
				full_len = q - fpath;
			}
			continue;
		}

		full_len = strlcat(fpath, next_token, VFS_MAX_PATH);
		if(full_len >= VFS_MAX_PATH)
			return -1;
	}

	if(full_len > 1 && fpath[full_len - 1] == '/')
	{
		fpath[full_len - 1] = '\0';
	}
	return 0;
}

const char * shell_getcwd(void)
{
	return &shell_cwd[0];
}

int shell_setcwd(const char * path)
{
	char fpath[VFS_MAX_PATH];
	int fd;

	if(shell_realpath(path, fpath) < 0)
		return -1;

	spin_lock(&shell_cwd_lock);
	fd = vfs_opendir(fpath);
	if(fd >= 0)
	{
		if(shell_cwd_fd >= 0)
			vfs_closedir(shell_cwd_fd);
		shell_cwd_fd = fd;
		if(strlen(fpath) < VFS_MAX_PATH)
			strncpy(shell_cwd, fpath, sizeof(shell_cwd));
		spin_unlock(&shell_cwd_lock);
		return 0;
	}
	spin_unlock(&shell_cwd_lock);

	return -1;
}

enum paser_state_t {
	PARSER_STATE_TEXT 		= 1,
	PARSER_STATE_ESC		= 2,
	PARSER_STATE_QUOTE		= 3,
	PARSER_STATE_DQUOTE		= 4,
	PARSER_STATE_VAR		= 5,
	PARSER_STATE_VARNAME	= 6,
	PARSER_STATE_VARNAME2	= 7,
	PARSER_STATE_QVAR		= 8,
	PARSER_STATE_QVARNAME	= 9,
	PARSER_STATE_QVARNAME2	= 10,
};

struct parser_state_transition_t {
	enum paser_state_t from_state;
	enum paser_state_t to_state;
	char input;
	int keep_value;
};

static struct parser_state_transition_t state_transitions[] = {
	{ PARSER_STATE_TEXT, PARSER_STATE_QUOTE, '\'', 0},
	{ PARSER_STATE_TEXT, PARSER_STATE_DQUOTE, '\"', 0},
	{ PARSER_STATE_TEXT, PARSER_STATE_VAR, '$', 0},
	{ PARSER_STATE_TEXT, PARSER_STATE_ESC, '\\', 0},
	{ PARSER_STATE_ESC, PARSER_STATE_TEXT, 0, 1},
	{ PARSER_STATE_QUOTE, PARSER_STATE_TEXT, '\'', 0},
	{ PARSER_STATE_DQUOTE, PARSER_STATE_TEXT, '\"', 0},
	{ PARSER_STATE_DQUOTE,PARSER_STATE_QVAR, '$', 0},
	{ PARSER_STATE_VAR, PARSER_STATE_VARNAME2, '{', 0},
	{ PARSER_STATE_VAR, PARSER_STATE_VARNAME, 0, 1},
	{ PARSER_STATE_VARNAME, PARSER_STATE_TEXT, ' ', 1},
	{ PARSER_STATE_VARNAME2, PARSER_STATE_TEXT, '}', 0},
	{ PARSER_STATE_QVAR, PARSER_STATE_QVARNAME2, '{', 0},
	{ PARSER_STATE_QVAR, PARSER_STATE_QVARNAME, 0, 1},
	{ PARSER_STATE_QVARNAME, PARSER_STATE_DQUOTE, ' ', 1},
	{ PARSER_STATE_QVARNAME, PARSER_STATE_TEXT, '\"', 0},
	{ PARSER_STATE_QVARNAME2, PARSER_STATE_DQUOTE, '}', 0},
	{ 0, 0, 0, 0}
};

static enum paser_state_t shell_parser_state(enum paser_state_t state, char c, char * result)
{
	struct parser_state_transition_t * transition;
	struct parser_state_transition_t * next_match = 0;
	struct parser_state_transition_t default_transition;
	int found = 0;

	default_transition.to_state = state;
	default_transition.keep_value = 1;
	for(transition = state_transitions; transition->from_state; transition++)
	{
		if((transition->from_state == state) && (transition->input == c))
		{
			found = 1;
			break;
		}
		if((transition->from_state == state) && (transition->input == 0))
			next_match = transition;
	}
	if(!found)
	{
		if(next_match)
			transition = next_match;
		else
			transition = &default_transition;
	}
	if(transition->keep_value)
		*result = c;
	else
		*result = 0;
	return transition->to_state;
}

static int is_varstate(enum paser_state_t s)
{
	if((s == PARSER_STATE_VARNAME) || (s == PARSER_STATE_VARNAME2) || (s == PARSER_STATE_QVARNAME) || (s == PARSER_STATE_QVARNAME2))
		return 1;
	else
		return 0;
}

static int shell_parser(const char * cmdline, int * argc, char *** argv, char ** pos)
{
	enum paser_state_t state = PARSER_STATE_TEXT;
	enum paser_state_t newstate;
	char *rd = (char *)cmdline;
	char c, *args, *val;
	char *buffer, *bp;
	char *varname, *vp;
	int i;

	*argc = 0;
	*pos = 0;
	bp = buffer = malloc(SZ_4K);
	if(!buffer)
	{
		*argc = 0;
		return 0;
	}
	vp = varname = malloc(SZ_256);
	if(!varname)
	{
		*argc = 0;
		return 0;
	}
	do {
		if(!(*rd))
			break;
		for(; *rd; rd++)
		{
			newstate = shell_parser_state(state, *rd, &c);
			if(is_varstate(state) && !is_varstate(newstate))
			{
				*(vp++) = '\0';
				val = getenv(varname);
				vp = varname;
				if(val)
				{
					for(; *val; val++)
						*(bp++) = *val;
				}
			}
			if(is_varstate(newstate))
			{
				if(c)
					*(vp++) = c;
			}
			else
			{
				if(newstate == PARSER_STATE_TEXT && state != PARSER_STATE_ESC && c == ' ')
				{
					if(bp != buffer && *(bp - 1))
					{
						*(bp++) = '\0';
						(*argc)++;
					}
				}
				else if(newstate == PARSER_STATE_TEXT && state != PARSER_STATE_ESC && c == ';')
				{
					if(bp != buffer && *(bp - 1))
					{
						*(bp++) = '\0';
						(*argc)++;
					}
					*pos = rd + 1;
					break;
				}
				else if(c)
				{
					*(bp++) = c;
				}
			}
			state = newstate;
		}
	} while((state != PARSER_STATE_TEXT) && (!is_varstate(state)));
	*(bp++) = '\0';
	if(is_varstate(state) && !is_varstate(PARSER_STATE_TEXT))
	{
		*(vp++) = '\0';
		val = getenv(varname);
		vp = varname;
		if(val)
		{
			for(; *val; val++)
				*(bp++) = *val;
		}
	}
	args = malloc(bp - buffer);
	if(!args)
	{
		*argc = 0;
		return 0;
	}
	memcpy(args, buffer, bp - buffer);
	*argv = malloc(sizeof(char *) * (*argc + 1));
	if(!*argv)
	{
		*argc = 0;
		free(args);
		return 0;
	}
	bp = args;
	for(i = 0; i < *argc; i++)
	{
		(*argv)[i] = bp;
		while(*bp)
			bp++;
		bp++;
	}
	(*argv)[i] = 0;
	free(buffer);
	free(varname);

	return 1;
}

static int __vmexec(const char * path, const char * fb, const char * input)
{
	return -1;
}
extern __typeof(__vmexec) vmexec __attribute__((weak, alias("__vmexec")));

int shell_system(const char * cmdline)
{
	struct command_t * cmd;
	char fpath[VFS_MAX_PATH];
	char ** args;
	char * p, * buf, * pos;
	size_t len;
	int n, ret;

	if(!cmdline)
		return 0;

	len = strlen(cmdline);
	buf = malloc(len + 2);
	if(!buf)
		return 0;
	memcpy(buf, cmdline, len);
	memcpy(buf + len, " ", 2);

	p = buf;
	while(*p)
	{
		if(shell_parser(p, &n, &args, &pos))
		{
			if(n > 0)
			{
				if((cmd = search_command(args[0])))
				{
					ret = cmd->exec(n, args);
				}
				else
				{
					if(vmexec == __vmexec)
					{
						printf(" could not found \'%s\' command\r\n", args[0]);
						ret = -1;
					}
					else
					{
						if(shell_realpath(args[0], fpath) >= 0)
							ret = vmexec(fpath, (n > 1) ? args[1] : NULL, (n > 2) ? args[2] : NULL);
						else
							ret = -1;
					}
				}
				if((ret < 0) && pos)
				{
			    	free(args[0]);
			    	free(args);
			    	break;
				}
    		}
			free(args[0]);
			free(args);
    	}
		if(!pos)
			*p = 0;
		else
			p = pos;
    }
	free(buf);
	return 1;
}

static void usage(void)
{
	printf("usage:\r\n");
	printf("    shell [args ...]\r\n");
}

static void shell_task(struct task_t * task, void * data)
{
	char prompt[VFS_MAX_PATH];
	char * p;

	while(1)
	{
		sprintf(prompt, "xboot: %s# ", shell_cwd);
		p = readline(prompt);
		shell_system(p);
		free(p);
	}
}

static int do_shell(int argc, char ** argv)
{
	task_create(scheduler_self(), "shell", NULL, NULL, shell_task, NULL, 0, 0);
	return 0;
}

static struct command_t cmd_shell = {
	.name	= "shell",
	.desc	= "shell command interpreter",
	.usage	= usage,
	.exec	= do_shell,
};

static __init void shell_cmd_init(void)
{
	register_command(&cmd_shell);
}

static __exit void shell_cmd_exit(void)
{
	unregister_command(&cmd_shell);
}

command_initcall(shell_cmd_init);
command_exitcall(shell_cmd_exit);
