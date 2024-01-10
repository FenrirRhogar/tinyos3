#include "tinyos.h"
#include "kernel_pipe.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_sched.h"

static file_ops pipe_read_file_ops = {
	.Open = NULL,
	.Read = pipe_read,
	.Write = NULL,
	.Close = pipe_reader_close};

static file_ops pipe_write_file_ops = {
	.Open = NULL,
	.Read = NULL,
	.Write = pipe_write,
	.Close = pipe_writer_close};

int sys_Pipe(pipe_t *pipe)
{
	Fid_t fid[2];
	FCB *fcb[2];

	if (FCB_reserve(2, fid, fcb) == 0)
	{
		return -1;
	}
	PIPE_CB *pipe_cb = (PIPE_CB *)xmalloc(sizeof(PIPE_CB));

	pipe->read = fid[0];
	pipe->write = fid[1];

	pipe_cb->reader = fcb[0];
	pipe_cb->writer = fcb[1];
	pipe_cb->has_space = COND_INIT;
	pipe_cb->has_data = COND_INIT;
	pipe_cb->w_position = 0;
	pipe_cb->r_position = 0;
	pipe_cb->current_size = 0;

	fcb[0]->streamobj = pipe_cb;
	fcb[1]->streamobj = pipe_cb;
	fcb[0]->streamfunc = &pipe_read_file_ops;
	fcb[1]->streamfunc = &pipe_write_file_ops;

	return 0;
}

int pipe_write(void *pipecb_t, const char *buf, unsigned int size)
{
	PIPE_CB *pipe_cb = (PIPE_CB *)pipecb_t;

	int *w_position = &pipe_cb->w_position;
	int bytes_written = 0;

	if (pipe_cb->reader == NULL || pipe_cb->writer == NULL)
	{
		return -1;
	}

	while (bytes_written < size)
	{
		while (pipe_cb->current_size == PIPE_BUFFER_SIZE && pipe_cb->reader != NULL)
		{
			kernel_broadcast(&pipe_cb->has_data);
			kernel_wait(&pipe_cb->has_space, SCHED_PIPE);
		}

		if (pipe_cb->reader == NULL || pipe_cb->writer == NULL)
		{
			return bytes_written;
		}

		int empty_space = PIPE_BUFFER_SIZE - pipe_cb->current_size;
		int chunk_size;
		int copy_size;
		if (size - bytes_written < empty_space)
		{
			chunk_size = size - bytes_written;
		}
		else
		{
			chunk_size = empty_space;
		}
		if (chunk_size < PIPE_BUFFER_SIZE - *w_position)
		{
			copy_size = chunk_size;
		}
		else
		{
			copy_size = PIPE_BUFFER_SIZE - *w_position;
		}

		memcpy(&pipe_cb->buffer[*w_position], &buf[bytes_written], copy_size);

		bytes_written += copy_size;
		pipe_cb->current_size += copy_size;
		*w_position = (*w_position + copy_size) % PIPE_BUFFER_SIZE;
	}

	kernel_broadcast(&pipe_cb->has_data);

	return bytes_written;
}

int pipe_read(void *pipecb_t, char *buf, unsigned int size)
{
	PIPE_CB *pipe_cb = (PIPE_CB *)pipecb_t;

	int *r_position = &pipe_cb->r_position;
	int bytes_read = 0;

	if (pipe_cb->reader == NULL)
	{
		return -1;
	}

	while (bytes_read < size)
	{
		while (pipe_cb->current_size == 0 && pipe_cb->writer != NULL)
		{
			kernel_broadcast(&pipe_cb->has_space);
			kernel_wait(&pipe_cb->has_data, SCHED_PIPE);
		}

		if (pipe_cb->current_size == 0 && pipe_cb->writer == NULL)
		{
			return bytes_read;
		}

		int empty_space = pipe_cb->current_size;
		int chunk_size;
		int copy_size;
		if (size - bytes_read < empty_space)
		{
			chunk_size = size - bytes_read;
		}
		else
		{
			chunk_size = empty_space;
		}
		if (chunk_size < PIPE_BUFFER_SIZE - *r_position)
		{
			copy_size = chunk_size;
		}
		else
		{
			copy_size = PIPE_BUFFER_SIZE - *r_position;
		}

		memcpy(&buf[bytes_read], &pipe_cb->buffer[*r_position], copy_size);

		bytes_read += copy_size;
		pipe_cb->current_size -= copy_size;
		*r_position = (*r_position + copy_size) % PIPE_BUFFER_SIZE;
	}

	kernel_broadcast(&pipe_cb->has_space);

	return bytes_read;
}

int pipe_writer_close(void *_pipecb)
{
	if (_pipecb == NULL)
	{
		return -1;
	}

	PIPE_CB *pipe_cb = (PIPE_CB *)_pipecb;

	pipe_cb->writer = NULL;
	kernel_broadcast(&pipe_cb->has_data);

	if (pipe_cb->reader == NULL)
	{
		free(pipe_cb);
	}

	return 0;
}

int pipe_reader_close(void *_pipecb)
{
	if (_pipecb == NULL)
	{
		return -1;
	}

	PIPE_CB *pipe_cb = (PIPE_CB *)_pipecb;

	pipe_cb->reader = NULL;
	kernel_broadcast(&pipe_cb->has_space);

	if (pipe_cb->writer == NULL)
	{
		free(pipe_cb);
	}

	return 0;
}
