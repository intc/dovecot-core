#ifndef MAIL_TRANSACTION_LOG_VIEW_H
#define MAIL_TRANSACTION_LOG_VIEW_H

#include "buffer.h"
#include "mail-transaction-log.h"

struct dotlock_settings;

/* Synchronization can take a while sometimes, especially when copying lots of
   mails. */
#define MAIL_TRANSACTION_LOG_LOCK_TIMEOUT (3*60)
#define MAIL_TRANSACTION_LOG_LOCK_CHANGE_TIMEOUT (3*60)

#define MAIL_TRANSACTION_LOG_FILE_IN_MEMORY(file) ((file)->fd == -1)

#define LOG_FILE_MODSEQ_CACHE_SIZE 10

struct modseq_cache {
	uoff_t offset;
	uint64_t highest_modseq;
};

struct mail_transaction_log_file {
	struct mail_transaction_log *log;
        struct mail_transaction_log_file *next;

	/* refcount=0 is a valid state. files start that way, and they're
	   freed only when mail_transaction_logs_clean() is called. */
	int refcount;

	char *filepath;
	int fd;

	ino_t st_ino;
	dev_t st_dev;
	time_t last_mtime;
	uoff_t last_size;

	time_t last_mmap_error_time;
	char *need_rotate;

	struct mail_transaction_log_header hdr;
	buffer_t mmap_buffer;
	buffer_t *buffer;
	uoff_t buffer_offset;
	void *mmap_base;
	size_t mmap_size;

	/* points to the next uncommitted transaction. usually same as EOF. */
	uoff_t sync_offset;
	/* highest modseq at sync_offset */
	uint64_t sync_highest_modseq;
	/* last_read_hdr_tail_offset is the offset that was last written to transaction
	   log. max_tail_offset is what should be written to the log the next
	   time a transaction is written. transaction log handling may update
	   max_tail_offset automatically by making it skip external transactions
	   after the last saved offset (to avoid re-reading them needlessly). */
	uoff_t last_read_hdr_tail_offset, max_tail_offset;

	/* if we've seen _INDEX_[UN9DELETED transaction in this file,
	   this is the offset. otherwise UOFF_T_MAX */
	uoff_t index_deleted_offset, index_undeleted_offset;

	struct modseq_cache modseq_cache[LOG_FILE_MODSEQ_CACHE_SIZE];

	struct file_lock *file_lock;
	time_t lock_created;

	bool locked:1;
	bool locked_sync_offset_updated:1;
	bool corrupted:1;
};

struct mail_transaction_log {
	struct mail_index *index;
        struct mail_transaction_log_view *views;
	char *filepath, *filepath2;

	/* files is a linked list of all the opened log files. the list is
	   sorted by the log file sequence, so that transaction views can use
	   them easily. head contains a pointer to the newest log file. */
	struct mail_transaction_log_file *files, *head;
	/* open_file is used temporarily while opening the log file.
	   if _open() failed, it's left there for _create(). */
	struct mail_transaction_log_file *open_file;

	int dotlock_refcount;
	struct dotlock *dotlock;

	bool log_2_unlink_checked:1;
};

void
mail_transaction_log_file_set_corrupted(struct mail_transaction_log_file *file,
					const char *fmt, ...)
	ATTR_FORMAT(2, 3);

void mail_transaction_log_get_dotlock_set(struct mail_transaction_log *log,
					  struct dotlock_settings *set_r);

struct mail_transaction_log_file *
mail_transaction_log_file_alloc_in_memory(struct mail_transaction_log *log);
struct mail_transaction_log_file *
mail_transaction_log_file_alloc(struct mail_transaction_log *log,
				const char *path);
void mail_transaction_log_file_free(struct mail_transaction_log_file **file);

/* Returns 1 if log was opened, 0 if it didn't exist or was already open,
   -1 if error. */
int mail_transaction_log_file_open(struct mail_transaction_log_file *file,
				   const char **reason_r);
int mail_transaction_log_file_create(struct mail_transaction_log_file *file,
				     bool reset);
int mail_transaction_log_file_lock(struct mail_transaction_log_file *file);

int mail_transaction_log_find_file(struct mail_transaction_log *log,
				   uint32_t file_seq, bool nfs_flush,
				   struct mail_transaction_log_file **file_r,
				   const char **reason_r);

/* Returns 1 if ok, 0 if file is corrupted or offset range is invalid,
   -1 if I/O error */
int mail_transaction_log_file_map(struct mail_transaction_log_file *file,
				  uoff_t start_offset, uoff_t end_offset,
				  const char **reason_r);
int mail_transaction_log_file_move_to_memory(struct mail_transaction_log_file *file);

void mail_transaction_logs_clean(struct mail_transaction_log *log);

bool mail_transaction_log_want_rotate(struct mail_transaction_log *log,
				      const char **reason_r);
int mail_transaction_log_rotate(struct mail_transaction_log *log, bool reset);
int mail_transaction_log_lock_head(struct mail_transaction_log *log,
				   const char *lock_reason);
void mail_transaction_log_file_unlock(struct mail_transaction_log_file *file,
				      const char *lock_reason);

void mail_transaction_update_modseq(const struct mail_transaction_header *hdr,
				    const void *data, uint64_t *cur_modseq,
				    unsigned int version);
/* Returns 1 if ok, 0 if file is corrupted or offset range is invalid,
   -1 if I/O error */
int mail_transaction_log_file_get_highest_modseq_at(
		struct mail_transaction_log_file *file,
		uoff_t offset, uint64_t *highest_modseq_r,
		const char **error_r);
int mail_transaction_log_file_get_modseq_next_offset(
		struct mail_transaction_log_file *file,
		uint64_t modseq, uoff_t *next_offset_r);

#endif
