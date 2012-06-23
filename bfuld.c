#include <procfs.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/varargs.h>
#include <sys/mman.h>
#include <thread.h>
#include <string.h>
#include <libelf.h>
#include <gelf.h>
#include <unistd.h>
#include <strings.h>

char *g_cmd = "bfuld";

static void
fatal(char *fmt, ...)
{
	va_list ap;
	int error = errno;
	int elferr = 0;

	va_start(ap, fmt);

	if (fmt[strlen(fmt) - 1] == '?') {
		elferr = 1;
		fmt[strlen(fmt) - 1] = '\0';
	}

	(void) fprintf(stderr, "%s: ", g_cmd);
	(void) vfprintf(stderr, fmt, ap);

	if (elferr) {
		(void) fprintf(stderr, ": %s\n", elf_errmsg(elf_errno()));
	} else if (fmt[strlen(fmt) - 1] != '\n') {
		(void) fprintf(stderr, ": %s\n", strerror(error));
	}

	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	int fd;
	Elf *elf;
	char *expected = "/usr/lib/ld.so.1";
	char *altered = "/var/tmp/ld.so.1";
	char *new;
	GElf_Ehdr hdr;
	int i;

	if (argc < 2)
		fatal("expected binary\n");

	if (elf_version(EV_CURRENT) == EV_NONE)
		fatal("out of date with respect to ELF version\n");

	if ((fd = open(argv[1], O_RDWR)) < 0)
		fatal("could not open %s", argv[1]);

	if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		fatal("could not open %s as an ELF file: %s\n", argv[1],
		    elf_errmsg(-1));
	}

	if (gelf_getehdr(elf, &hdr) == NULL)
		fatal("could not get header?", elf_errmsg(elf_errno()));
	
	for (i = 0; i < hdr.e_phnum; i++) {
		GElf_Phdr phdr;
		char buf[256];

		if (gelf_getphdr(elf, i, &phdr) == NULL)
			fatal("couldn't read program header %d?", i);

		if (phdr.p_type != PT_INTERP)
			continue;

		if (phdr.p_memsz != strlen(expected) + 1) {
			fatal("expected interp to be %d bytes; found %d\n",
			    strlen(expected) + 1, phdr.p_memsz);
		}

		if (pread(fd, buf, phdr.p_memsz, phdr.p_offset) != phdr.p_memsz)
			fatal("short read at offset %d", phdr.p_offset);

		if (strcmp(buf, expected) == 0) {
			new = altered;
		} else if (strcmp(buf, altered) == 0) {
			new = expected;
		} else {
			fatal("unexpected interpreter '%s'\n", buf);
		}

		printf("%s: interpreter is %s; setting to %s ... ",
		    g_cmd, buf, new);

		if (pwrite(fd, new,
		    phdr.p_memsz, phdr.p_offset) != phdr.p_memsz)
			fatal("short write at offset %d", phdr.p_offset);

		printf("done\n");
		return (0);
	}

	fatal("didn't find interp program header\n");
	return (0);
}
