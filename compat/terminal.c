#include <inttypes.h>
#include "git-compat-util.h"
#include "run-command.h"
#include "compat/terminal.h"
#include "sigchain.h"
#include "strbuf.h"
#include "cache.h"

#ifdef HAVE_DEV_TTY

static int term_fd = -1;
static struct termios old_term;

static void restore_term(void)
{
	if (term_fd < 0)
		return;

	tcsetattr(term_fd, TCSAFLUSH, &old_term);
	close(term_fd);
	term_fd = -1;
}

static void restore_term_on_signal(int sig)
{
	restore_term();
	sigchain_pop(sig);
	raise(sig);
}

static int disable_echo(void)
{
	struct termios t;

	term_fd = open("/dev/tty", O_RDWR);
	if (tcgetattr(term_fd, &t) < 0)
		goto error;

	old_term = t;
	sigchain_push_common(restore_term_on_signal);

	t.c_lflag &= ~ECHO;
	if (!tcsetattr(term_fd, TCSAFLUSH, &t))
		return 0;

error:
	close(term_fd);
	term_fd = -1;
	return -1;
}

char *git_terminal_prompt(const char *prompt, int echo)
{
	static struct strbuf buf = STRBUF_INIT;
	int r;
	FILE *input_fh, *output_fh;

	input_fh = fopen("/dev/tty", "r");
	if (!input_fh)
		return NULL;

	output_fh = fopen("/dev/tty", "w");
	if (!output_fh) {
		fclose(input_fh);
		return NULL;
	}

	if (!echo && disable_echo()) {
		fclose(input_fh);
		fclose(output_fh);
		return NULL;
	}

	fputs(prompt, output_fh);
	fflush(output_fh);

	r = strbuf_getline(&buf, input_fh, '\n');
	if (!echo) {
		putc('\n', output_fh);
		fflush(output_fh);
	}

	restore_term();
	fclose(input_fh);
	fclose(output_fh);

	if (r == EOF)
		return NULL;
	return buf.buf;
}

#elif !defined(GIT_WINDOWS_NATIVE)

char *git_terminal_prompt(const char *prompt, int echo)
{
	return getpass(prompt);
}

#else

char *git_terminal_prompt(const char *prompt, int echo)
{
	const char *read_input[] = {
		"sh", "-c", echo ?
		"cat >/dev/tty && read -r line </dev/tty && echo \"$line\"" :
		"cat >/dev/tty && read -r -s line </dev/tty && echo \"$line\" && echo >/dev/tty",
		NULL
	};
	struct child_process child = CHILD_PROCESS_INIT;
	static struct strbuf buffer = STRBUF_INIT;
	int prompt_len = strlen(prompt), len = -1;

	child.argv = read_input;
	child.in = -1;
	child.out = -1;

	if (start_command(&child)) {
		error("Could not spawn shell");
		return NULL;
	}

	if (write_in_full(child.in, prompt, prompt_len) != prompt_len) {
		error("Could not write to terminal");
		close(child.in);
		goto ret;
	}
	close(child.in);

	strbuf_reset(&buffer);
	len = strbuf_read(&buffer, child.out, 1024);
	if (len < 0) {
		error("Could not read from terminal");
		goto ret;
	}

	strbuf_strip_suffix(&buffer, "\n");
	strbuf_strip_suffix(&buffer, "\r");

ret:
	close(child.out);
	finish_command(&child);

	return len < 0 ? NULL : buffer.buf;
}

#endif
