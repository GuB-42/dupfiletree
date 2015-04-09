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

//#define DO_MTRACE
//#define DO_FORK
//#define NO_ARCHIVES
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

int do_xmd5_archive(const char *filename)
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
			       5 + strlen(filename) + 1);
		if (!*flist_last)	goto bad_archive2;
		(*flist_last)->next = NULL;
		for (j = 0; j < MD5_DIGEST_LENGTH; ++j) {
			sprintf((*flist_last)->line + 2 * j, "%02x", md5_out[j]);
		}
		sprintf((*flist_last)->line + 2 * MD5_DIGEST_LENGTH,
		        "  %15llu %s%%%%%%%%/%s",
		        fsize, filename, archive_entry_pathname(entry));
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
	if (!f) goto bad_file;
	if (MD5_Init(&c) != 1) goto bad_file2;
	if (!(buf = malloc(BLOCK_SIZE))) goto bad_file2;
	while ((rsize = fread(buf, 1, BLOCK_SIZE, f))) {
		if (MD5_Update(&c, buf, rsize) != 1) {
			MD5_Final(md5_out, &c);
			free(buf);
			goto bad_file2;
		}
		fsize += rsize;
	}
	free(buf);
	if (MD5_Final(md5_out, &c) != 1) goto bad_file2;
	if (ferror(f)) goto bad_file2;

	fclose(f);
	if (stat_size != UNKNOWN_SIZE && stat_size != fsize) goto bad_file;
	for (j = 0; j < MD5_DIGEST_LENGTH; ++j) {
		printf("%02x", md5_out[j]);
	}
	printf("  %15llu %s\n", fsize, filename);

	return 0;
bad_file2:
	fclose(f);
bad_file:
	printf("BADFILEXXXXXXXXXXXXXXXXXXXXXXXXX                X %s\n", filename);
	return -1;
}

int do_xmd5(const char *filename);

int do_xmd5_dir(const char *filename)
{
	DIR *dir;
	long pathlen;
	size_t dir_entry_len;
	size_t filename_len = strlen(filename);
	struct dirent *dir_entry_buf = NULL;
	struct dirent *dir_entry;
	struct FileList *flist = NULL;
	struct FileList **flist_last = &flist;

	if (!(dir = opendir(filename))) goto bad_dir;
	if ((pathlen = pathconf(filename, _PC_NAME_MAX)) == -1) goto bad_dir2;
	dir_entry_len = offsetof(struct dirent, d_name) + pathlen + 1;
	if (!(dir_entry_buf = (struct dirent *)malloc(dir_entry_len))) goto bad_dir2;
	while (1) {
		if (readdir_r(dir, dir_entry_buf, &dir_entry) != 0) goto bad_dir2;
		if (!dir_entry) break;
		if (strcmp(dir_entry->d_name, ".") != 0 &&
		    strcmp(dir_entry->d_name, "..") != 0) {
			size_t fname_len = strlen(dir_entry->d_name);
			*flist_last = (struct FileList *)malloc(offsetof(struct FileList, line) +
			                                        filename_len + 1 + fname_len + 1);
			if (!*flist_last) goto bad_dir2;
			(*flist_last)->next = NULL;
			memcpy((*flist_last)->line, filename, filename_len);
			(*flist_last)->line[filename_len] = '/';
			memcpy((*flist_last)->line + filename_len + 1,
			       dir_entry->d_name, fname_len + 1);
			flist_last = &(*flist_last)->next;
		}
	}
	free(dir_entry_buf);
	closedir(dir);

	while (flist) {
		struct FileList *p = flist;
		do_xmd5(p->line);
		flist = p->next;
		free(p);
	}

	return 0;
bad_dir2:
	free_flist(flist);
	free(dir_entry_buf);
	closedir(dir);
bad_dir:
	printf("BADDIRXXXXXXXXXXXXXXXXXXXXXXXXXX                X %s\n", filename);
	return -1;
}

int do_xmd5(const char *filename)
{
	struct stat st;

	if (lstat(filename, &st) != 0) {
		printf("STATFAILXXXXXXXXXXXXXXXXXXXXXXXX                X %s\n", filename);
		return -1;
	}

	if (S_ISREG(st.st_mode)) {
		if (do_xmd5_file(filename, st.st_size) == 0) {
#ifndef NO_ARCHIVES
#ifdef DO_FORK
			pid_t pid;
         fflush(stdout);
         fflush(stderr);
			pid = fork();
			if (pid == 0) {
				do_xmd5_archive(filename);
				exit(0);
			} else if (pid != -1) {
				waitpid(pid, NULL, 0);
			}
#else
			do_xmd5_archive(filename);
#endif
#endif
			return 0;
		} else {
			return -1;
		}
	} else if (S_ISDIR(st.st_mode)) {
		return do_xmd5_dir(filename);
	} else {
		printf("FILETYPEXXXXXXXXXXXXXXXXXXXXXXXX                X %s\n", filename);
		return -1;
	}
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
