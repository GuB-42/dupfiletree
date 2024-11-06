#include <archive.h>
#include <archive_entry.h>
#include <openssl/md5.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

/* #define DO_MTRACE */
/* #define DO_FORK */
/* #define NO_ARCHIVES */
#define BLOCK_SIZE 10240
#define UNKNOWN_SIZE 0xFFFFFFFFFFFFFFFFull
#define RETRY_ARCHIVE_COUNT 10

#ifdef DO_MTRACE
#include <mcheck.h>
#endif

static size_t grow_size(size_t size) {
	if (size < 1024) return 1024;
	if (size > 1048576) return size + 1048576;
	return size << 1;
}

const char *get_escape_pt(const char *filename_pt, const char **prepl)
{
	const char *pt;
	for (pt = filename_pt; *pt; ++pt) {
		if (*pt == '\n') {
			*prepl = "%lf;";
			return pt;
		} else if (*pt == '\r') {
			*prepl = "%cr;";
			return pt;
		} else if (*pt == '%') {
			if ((pt[1] == '%' && pt[2] == '%' && pt[3] == '%') ||
			    (pt[1] == '#' && pt[2] == '0' && pt[3] == ';') ||
			    (pt[1] == 'l' && pt[2] == 'f' && pt[3] == ';') ||
			    (pt[1] == 'c' && pt[2] == 'r' && pt[3] == ';') ||
			    (pt[1] == 'p' && pt[2] == 'c' && pt[3] == ';')) {
				*prepl = "%pc;";
				return pt;
			}
		}
	}
	return NULL;
}

char *escape_filename(const char *filename)
{
	const char *cp;
	char *output = (char *)filename;
	size_t output_size = 1;
	int need_replace = 0;

	for (cp = filename; *cp; ) {
		const char *repl = NULL;
		const char *nextp = get_escape_pt(cp, &repl);
		if (nextp) {
			output_size += (nextp - cp) + strlen(repl);
			need_replace = 1;
			cp = nextp + 1;
		} else {
			output_size += strlen(cp);
			break;
		}
	}

	if (need_replace) {
		output = (char *)malloc(output_size);
		if (output) {
			char *poutput = output;
			for (cp = filename; *cp; ) {
				const char *repl = NULL;
				const char *nextp = get_escape_pt(cp, &repl);
				if (nextp) {
					size_t repl_len = strlen(repl);
					memcpy(poutput, cp, nextp - cp);
					poutput += nextp - cp;
					memcpy(poutput, repl, repl_len + 1);
					poutput += repl_len;
					cp = nextp + 1;
				} else {
					strcpy(poutput, cp);
					break;
				}
			}
		}
	}

	return output;
}

void set_xerror(char *xerror, size_t xerror_size,
                const char *error, const char *error2)
{
	unsigned i;
	const char *errors[3];
	const char **pstr;
	const char *p;

	errors[0] = error ? error : error2;
	errors[1] = error ? error2 : NULL;
	errors[2] = NULL;
	pstr = errors;
	p = *pstr;
	for (i = 0; i < xerror_size - 3; ++i) {
		while (p &&
		       !(*p >= 'A' && *p <= 'Z') &&
		       !(*p >= 'a' && *p <= 'z')) {
			p = *p == '\0' ? *++pstr : p + 1;
		}
		if (p && *p >= 'A' && *p <= 'Z') {
			xerror[i] = *p++;
		} else if (p && *p >= 'a' && *p <= 'z') {
			xerror[i] = *p++ - 'a' + 'A';
		} else {
			xerror[i] = 'X';
		}
	}
	xerror[xerror_size - 3] = 'X';
	xerror[xerror_size - 2] = 'X';
	xerror[xerror_size - 1] = '\0';
}

void print_error_line(const char *error, const char *error2, const char *escaped_filename)
{
	char xerror[MD5_DIGEST_LENGTH * 2 + 1];

	set_xerror(xerror, sizeof(xerror), error, error2);
	printf("%s                X %s\n", xerror, escaped_filename);
}


int sort_str_with_xmd5(const void *a, const void *b)
{
	const char *a_str = *(char * const *)a;
	const char *b_str = *(char * const *)b;
	while (*a_str != ' ' && *a_str != '\0') ++a_str;
	while (*a_str == ' ') ++a_str;
	while (*a_str != ' ' && *a_str != '\0') ++a_str;
	while (*a_str == ' ') ++a_str;
	while (*b_str != ' ' && *b_str != '\0') ++b_str;
	while (*b_str == ' ') ++b_str;
	while (*b_str != ' ' && *b_str != '\0') ++b_str;
	while (*b_str == ' ') ++b_str;
	return strcmp(a_str, b_str);
}

int do_xmd5_archive(const char *filename, const char *escaped_filename)
{
	char **flist = NULL;
	char **flist_p = NULL;
	size_t flist_alloc_size = 0;
	char **p;
	struct archive *a;
	struct archive_entry *entry;
	void *buf = NULL;
	int rh_ret, open_ret;
	int retry, j;
	unsigned long long name_error_counter = 0;

	if (!(a = archive_read_new())) goto bad_archive;
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	for (retry = 0; retry <= RETRY_ARCHIVE_COUNT; ++retry) {
		open_ret = archive_read_open_filename(a, filename, BLOCK_SIZE);
		if (open_ret != ARCHIVE_RETRY) break;
	}
	if (open_ret != ARCHIVE_OK && open_ret != ARCHIVE_WARN) goto bad_archive2;
	if (!(buf = malloc(BLOCK_SIZE))) goto bad_archive2;
	for (;;) {
		MD5_CTX c;
		unsigned long long fsize = 0;
		unsigned char md5_out[MD5_DIGEST_LENGTH];
		const char *entry_pathname;
		char *escaped_entry_pathname;
		char xerror[MD5_DIGEST_LENGTH * 2 + 1];
		int md5_error = 0;

		for (retry = 0; retry <= RETRY_ARCHIVE_COUNT; ++retry) {
			rh_ret = archive_read_next_header(a, &entry);
			if (rh_ret != ARCHIVE_RETRY) break;
		}
		if (rh_ret == ARCHIVE_FATAL) goto bad_archive2;
		if (rh_ret != ARCHIVE_OK && rh_ret != ARCHIVE_WARN) break;
		fsize = 0;
		if (MD5_Init(&c) != 1) goto bad_archive2;
		while (1) {
			int size;
			for (retry = 0; retry <= RETRY_ARCHIVE_COUNT; ++retry) {
				size = archive_read_data(a, buf, BLOCK_SIZE);
				if (size != ARCHIVE_RETRY) break;
			}
			if (size < 0) {
				if (size == ARCHIVE_FATAL) {
					MD5_Final(md5_out, &c);
					goto bad_archive2;
				}
				md5_error = 1;
				set_xerror(xerror, sizeof(xerror),
				           "bad afile", archive_error_string(a));
				break;
			} else if (size == 0) {
				break;
			} else {
				if (MD5_Update(&c, buf, size) != 1) {
					md5_error = 1;
					set_xerror(xerror, sizeof(xerror),
					           "bad afile", archive_error_string(a));
					break;
				}
				fsize += size;
			}
		}
		if (MD5_Final(md5_out, &c) != 1) goto bad_archive2;

		if ((size_t)(flist_p - flist) >= flist_alloc_size) {
			char **t;
			flist_alloc_size = grow_size(flist_alloc_size);
			t = (char **)realloc(flist, flist_alloc_size * sizeof (char *));
			if (!t) goto bad_archive2;
			flist_p = t + (flist_p - flist);
			flist = t;
		}
		entry_pathname = archive_entry_pathname(entry);
		if (!entry_pathname) {
			++name_error_counter;
			escaped_entry_pathname = malloc(256);
			if (escaped_entry_pathname) {
				sprintf(escaped_entry_pathname, "%%#0;BAD_NAME_%llu", name_error_counter);
			} else {
				entry_pathname = "%#0;";
				escaped_entry_pathname = (char *)entry_pathname;
			}
		} else {
			escaped_entry_pathname = escape_filename(entry_pathname);
		}
		*flist_p = (char *)
			malloc(MD5_DIGEST_LENGTH * 2 + 2 +
			       33 + strlen(escaped_entry_pathname) +
			       5 + strlen(escaped_filename) + 1);
		if (!*flist_p) goto bad_archive2;
		if (md5_error) {
			sprintf(*flist_p, "%s  %15llu %s%%%%%%%%/%s",
			        xerror, 0ull, escaped_filename, escaped_entry_pathname);
		} else {
			for (j = 0; j < MD5_DIGEST_LENGTH; ++j) {
				sprintf(*flist_p + 2 * j, "%02x", md5_out[j]);
			}
			sprintf(*flist_p + 2 * MD5_DIGEST_LENGTH, "  %15llu %s%%%%%%%%/%s",
			        fsize, escaped_filename, escaped_entry_pathname);
		}
		++flist_p;
		if (entry_pathname != escaped_entry_pathname) {
			free(escaped_entry_pathname);
		}
	}
	free(buf);
	buf = NULL;

	if (rh_ret != ARCHIVE_EOF) goto bad_archive2;
	if (archive_read_free(a) != ARCHIVE_OK) goto bad_archive;

	if (flist) {
		qsort(flist, flist_p - flist, sizeof (char **), sort_str_with_xmd5);
		for (p = flist; p < flist_p; ++p) {
			printf("%s\n", *p);
			free(*p);
		}
		free(flist);
	}

	return 0;
bad_archive2:
	archive_read_free(a);
bad_archive:
	for (p = flist; p < flist_p; ++p) free(*p);
	free(flist);
	free(buf);
	return -1;
}

int do_xmd5_file(const char *filename,
                 const char *escaped_filename,
                 unsigned long long stat_size)
{
	FILE    *f;
	size_t  rsize;
	MD5_CTX c;
	unsigned long long fsize = 0;
	unsigned char md5_out[MD5_DIGEST_LENGTH];
	void *buf = NULL;
	int j;

	f = fopen(filename, "rb");
	if (!f) goto bad_file_errno;
	if (MD5_Init(&c) != 1) goto bad_file2_md5;
	if (!(buf = malloc(BLOCK_SIZE))) goto bad_file2_errno;
	while ((rsize = fread(buf, 1, BLOCK_SIZE, f))) {
		if (MD5_Update(&c, buf, rsize) != 1) {
			MD5_Final(md5_out, &c);
			free(buf);
			goto bad_file2_md5;
		}
		fsize += rsize;
	}
	free(buf);
	if (MD5_Final(md5_out, &c) != 1) goto bad_file2_md5;
	if (ferror(f)) goto bad_file2_ferror;

	if (fclose(f) != 0) goto bad_file_errno;
	if (stat_size != UNKNOWN_SIZE && stat_size != fsize) goto bad_file_size;
	for (j = 0; j < MD5_DIGEST_LENGTH; ++j) {
		printf("%02x", md5_out[j]);
	}
	printf("  %15llu %s\n", fsize, escaped_filename);

	return 0;
bad_file2_errno:
	print_error_line("bad file", strerror(errno), escaped_filename);
	goto file_error2;
bad_file2_md5:
	print_error_line("bad file", "checksum", escaped_filename);
	goto file_error2;
bad_file2_ferror:
	print_error_line("bad file", "ferror", escaped_filename);
	goto file_error2;
bad_file_errno:
	print_error_line("bad file", strerror(errno), escaped_filename);
	goto file_error;
bad_file_size:
	print_error_line("bad file", "size", escaped_filename);
	goto file_error;
file_error2:
	fclose(f);
file_error:
	return -1;
}

int do_xmd5(const char *filename);

int sort_str(const void *a, const void *b)
{
	return strcmp(*(char * const *)a, *(char * const *)b);
}

int do_xmd5_dir(const char *filename, const char *escaped_filename)
{
	DIR *dir;
	size_t filename_len = strlen(filename);
	struct dirent *dir_entry;
	char **flist = NULL;
	char **flist_p = NULL;
	size_t flist_alloc_size = 0;
	char **p;

	if (!(dir = opendir(filename))) goto bad_dir_errno;
	while (1) {
		errno = 0;
		dir_entry = readdir(dir);
		if (errno) goto bad_dir2_errno;
		if (!dir_entry) break;
		if (strcmp(dir_entry->d_name, ".") != 0 &&
		    strcmp(dir_entry->d_name, "..") != 0) {
			size_t fname_len = strlen(dir_entry->d_name);
			if ((size_t)(flist_p - flist) >= flist_alloc_size) {
				char **t;
				flist_alloc_size = grow_size(flist_alloc_size);
				t = (char **)realloc(flist, flist_alloc_size * sizeof (char *));
				if (!t) goto bad_dir2_errno;
				flist_p = t + (flist_p - flist);
				flist = t;
			}
			*flist_p = (char *)malloc(filename_len + 1 + fname_len + 1);
			if (!*flist_p) goto bad_dir2_errno;
			memcpy(*flist_p, filename, filename_len);
			(*flist_p)[filename_len] = '/';
			memcpy(*flist_p + filename_len + 1,
			       dir_entry->d_name, fname_len + 1);
			++flist_p;
		}
	}
	if (closedir(dir) != 0) goto bad_dir_errno;

	if (flist) {
		qsort(flist, flist_p - flist, sizeof (char **), sort_str);
		for (p = flist; p < flist_p; ++p) {
			do_xmd5(*p);
			free(*p);
		}
		free(flist);
	}

	return 0;
bad_dir2_errno:
	print_error_line("bad dir", strerror(errno), escaped_filename);
	goto dir_error2;
bad_dir_errno:
	print_error_line("bad dir", strerror(errno), escaped_filename);
	goto dir_error;
dir_error2:
	for (p = flist; p < flist_p; ++p) free(*p);
	closedir(dir);
dir_error:
	free(flist);
	return -1;
}

int do_xmd5(const char *filename)
{
	struct stat st;
	char *escaped_filename = escape_filename(filename);
	int ret = -1;

	if (!escaped_filename) {
		print_error_line("escape name fail", strerror(errno), "???");
		return -1;
	}

	if (lstat(filename, &st) != 0) {
		print_error_line("stat fail", strerror(errno), escaped_filename);
		if (filename != escaped_filename) free(escaped_filename);
		return -1;
	}

	if (S_ISREG(st.st_mode)) {
		if (do_xmd5_file(filename, escaped_filename, st.st_size) == 0) {
#ifndef NO_ARCHIVES
#ifdef DO_FORK
			pid_t pid;
			fflush(stdout);
			fflush(stderr);
			pid = fork();
			if (pid == 0) {
				do_xmd5_archive(filename, escaped_filename);
				exit(0);
			} else if (pid != -1) {
				waitpid(pid, NULL, 0);
			}
#else
			do_xmd5_archive(filename, escaped_filename);
#endif
#endif
			ret = 0;
		} else {
			ret = -1;
		}
	} else if (S_ISDIR(st.st_mode)) {
		ret = do_xmd5_dir(filename, escaped_filename);
	} else if (S_ISLNK(st.st_mode)) {
		print_error_line("file type", "link", escaped_filename);
		ret = -1;
	} else if (S_ISCHR(st.st_mode)) {
		print_error_line("file type", "char device", escaped_filename);
		ret = -1;
	} else if (S_ISBLK(st.st_mode)) {
		print_error_line("file type", "block device", escaped_filename);
		ret = -1;
	} else if (S_ISFIFO(st.st_mode)) {
		print_error_line("file type", "fifo", escaped_filename);
		ret = -1;
	} else if (S_ISSOCK(st.st_mode)) {
		print_error_line("file type", "socket", escaped_filename);
		ret = -1;
	} else {
		print_error_line("file type", "unknown", escaped_filename);
		ret = -1;
	}

	if (filename != escaped_filename) free(escaped_filename);
	return ret;
}

int main(int argc, char* argv[])
{
	int i;

#ifdef DO_MTRACE
	mtrace();
#endif
	for (i = 1; i < argc; ++i) {
		do_xmd5(argv[i]);
	}
#ifdef DO_MTRACE
	muntrace();
#endif
	return 0;
}
