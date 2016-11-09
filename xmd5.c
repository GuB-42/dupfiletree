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

#ifdef DO_MTRACE
#include <mcheck.h>
#endif

struct FileList {
	struct FileList *next;
	char line[];
};

static struct FileList *free_flist(struct FileList *flist)
{
	while (flist) {
		struct FileList *p = flist;
		flist = p->next;
		free(p);
	}
	return 0;
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

void print_error_line(const char *error, const char *error2, const char *escaped_filename)
{
	char xerror[MD5_DIGEST_LENGTH * 2 + 1];
	unsigned i;
	const char *errors[3];
	const char **pstr;
	const char *p;

	errors[0] = error ? error : error2;
	errors[1] = error ? error2 : NULL;
	errors[2] = NULL;
	pstr = errors;
	p = *pstr;
	for (i = 0; i < sizeof (xerror) - 3; ++i) {
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
	xerror[sizeof (xerror) - 3] = 'X';
	xerror[sizeof (xerror) - 2] = 'X';
	xerror[sizeof (xerror) - 1] = '\0';

	printf("%s                X %s\n", xerror, escaped_filename);
}

int do_xmd5_archive(const char *filename, const char *escaped_filename)
{
	struct FileList *flist = NULL;
	struct FileList **flist_last = &flist;
	struct archive *a;
	struct archive_entry *entry;
	void *buf = NULL;
	int rh_ret;

	if (!(a = archive_read_new())) goto bad_archive;
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	if (archive_read_open_filename(a, filename, BLOCK_SIZE) != ARCHIVE_OK) {
		goto bad_archive2;
	}
	if (!(buf = malloc(BLOCK_SIZE))) goto bad_archive2;
	while ((rh_ret = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
		int j;
		MD5_CTX c;
		unsigned long long fsize = 0;
		unsigned char md5_out[MD5_DIGEST_LENGTH];

		fsize = 0;
		if (MD5_Init(&c) != 1) goto bad_archive2;
		while (1) {
			int size = archive_read_data(a, buf, BLOCK_SIZE);
			if (size < 0) {
				MD5_Final(md5_out, &c);
				goto bad_archive2;
			} else if (size == 0) {
				break;
			} else {
				if (MD5_Update(&c, buf, size) != 1) {
					MD5_Final(md5_out, &c);
					goto bad_archive2;
				}
				fsize += size;
			}
		}
		if (MD5_Final(md5_out, &c) != 1) goto bad_archive2;

		*flist_last = (struct FileList *)
			malloc(offsetof(struct FileList, line) + MD5_DIGEST_LENGTH * 2 + 2 +
			       33 + strlen(archive_entry_pathname(entry)) +
			       5 + strlen(escaped_filename) + 1);
		if (!*flist_last) goto bad_archive2;
		(*flist_last)->next = NULL;
		for (j = 0; j < MD5_DIGEST_LENGTH; ++j) {
			sprintf((*flist_last)->line + 2 * j, "%02x", md5_out[j]);
		}
		sprintf((*flist_last)->line + 2 * MD5_DIGEST_LENGTH,
		        "  %15llu %s%%%%%%%%/%s",
		        fsize, escaped_filename, archive_entry_pathname(entry));
		flist_last = &(*flist_last)->next;
	}
	free(buf);
	buf = NULL;

	if (rh_ret != ARCHIVE_EOF) goto bad_archive2;
	if (archive_read_free(a) != ARCHIVE_OK) goto bad_archive;

	while (flist) {
		struct FileList *p = flist;
		printf("%s\n", p->line);
		flist = p->next;
		free(p);
	}

	return 0;
bad_archive2:
	archive_read_free(a);
bad_archive:
	free_flist(flist);
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
	if (!f)	goto bad_file_errno;
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

int do_xmd5_dir(const char *filename, const char *escaped_filename)
{
	DIR *dir;
	long pathlen;
	size_t dir_entry_len;
	size_t filename_len = strlen(filename);
	struct dirent *dir_entry_buf = NULL;
	struct dirent *dir_entry;
	struct FileList *flist = NULL;
	struct FileList **flist_last = &flist;

	if (!(dir = opendir(filename))) goto bad_dir_errno;
	if ((pathlen = pathconf(filename, _PC_NAME_MAX)) == -1) goto bad_dir2_pathconv;
	dir_entry_len = offsetof(struct dirent, d_name) + pathlen + 1;
	if (!(dir_entry_buf = (struct dirent *)malloc(dir_entry_len))) goto bad_dir2_errno;
	while (1) {
		if ((errno = readdir_r(dir, dir_entry_buf, &dir_entry)) != 0) goto bad_dir2_errno;
		if (!dir_entry) break;
		if (strcmp(dir_entry->d_name, ".") != 0 &&
		    strcmp(dir_entry->d_name, "..") != 0) {
			size_t fname_len = strlen(dir_entry->d_name);
			*flist_last = (struct FileList *)malloc(offsetof(struct FileList, line) +
			                                        filename_len + 1 + fname_len + 1);
			if (!*flist_last) goto bad_dir2_errno;
			(*flist_last)->next = NULL;
			memcpy((*flist_last)->line, filename, filename_len);
			(*flist_last)->line[filename_len] = '/';
			memcpy((*flist_last)->line + filename_len + 1,
			       dir_entry->d_name, fname_len + 1);
			flist_last = &(*flist_last)->next;
		}
	}
	free(dir_entry_buf);
	if (closedir(dir) != 0) goto bad_dir_errno;

	while (flist) {
		struct FileList *p = flist;
		do_xmd5(p->line);
		flist = p->next;
		free(p);
	}

	return 0;
bad_dir2_pathconv:
	print_error_line("bad dir", "pathconf", escaped_filename);
	goto dir_error2;
bad_dir2_errno:
	print_error_line("bad dir", strerror(errno), escaped_filename);
	goto dir_error2;
bad_dir_errno:
	print_error_line("bad dir", strerror(errno), escaped_filename);
	goto dir_error;
dir_error2:
	free_flist(flist);
	free(dir_entry_buf);
	closedir(dir);
dir_error:
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
