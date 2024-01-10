#ifndef __KERNEL_PIPE_H
#define __KERNEL_PIPE_H

#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_dev.h"
#include "kernel_cc.h"

#define PIPE_BUFFER_SIZE 16500

typedef struct pipe_control_block{
	FCB* reader;
	FCB* writer;

	CondVar has_space;
	CondVar has_data;

	int w_position;
	int r_position;
	int current_size;

	char buffer[PIPE_BUFFER_SIZE];
}PIPE_CB;

int sys_Pipe(pipe_t* pipe);

int pipe_write(void* pipecb_t, const char* buf, unsigned int n);

int pipe_read(void* pipecb_t, char* buf, unsigned int n);

int pipe_writer_close(void* _pipecb);

int pipe_reader_close(void* _pipecb);


#endif