// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright (C) 2023 The Falco Authors.
 *
 * This file is dual licensed under either the MIT or GPL 2. See MIT.txt
 * or GPL2.txt for full copies of the license.
 */

#include <helpers/interfaces/fixed_size_event.h>
#include <helpers/interfaces/variable_size_event.h>

/*=============================== ENTER EVENT ===========================*/

SEC("tp_btf/sys_enter")
int BPF_PROG(recvfrom_e, struct pt_regs *regs, long id) {
	/* We need to keep this at the beginning of the program because otherwise we alter the state of
	 * the ebpf registers causing a verifier issue.
	 */
	unsigned long args[3] = {0};
	extract__network_args(args, 3, regs);

	struct ringbuf_struct ringbuf;
	if(!ringbuf__reserve_space(&ringbuf, RECVFROM_E_SIZE, PPME_SOCKET_RECVFROM_E)) {
		return 0;
	}

	ringbuf__store_event_header(&ringbuf);

	/*=============================== COLLECT PARAMETERS  ===========================*/

	/* Parameter 1: fd (type: PT_FD) */
	int64_t socket_fd = (int32_t)args[0];
	ringbuf__store_s64(&ringbuf, socket_fd);

	/* Parameter 2: size (type: PT_UINT32) */
	uint32_t size = (uint32_t)args[2];
	ringbuf__store_u32(&ringbuf, size);

	/*=============================== COLLECT PARAMETERS  ===========================*/

	ringbuf__submit_event(&ringbuf);

	return 0;
}

/*=============================== ENTER EVENT ===========================*/

/*=============================== EXIT EVENT ===========================*/

SEC("tp_btf/sys_exit")
int BPF_PROG(recvfrom_x, struct pt_regs *regs, long ret) {
	struct auxiliary_map *auxmap = auxmap__get();
	if(!auxmap) {
		return 0;
	}

	auxmap__preload_event_header(auxmap, PPME_SOCKET_RECVFROM_X);

	/*=============================== COLLECT PARAMETERS  ===========================*/

	/* Parameter 1: res (type: PT_ERRNO) */
	auxmap__store_s64_param(auxmap, ret);

	/* Collect parameters at the beginning to manage socketcalls */
	unsigned long args[5] = {0};
	extract__network_args(args, 5, regs);

	int64_t socket_fd = (int32_t)args[0];

	if(ret >= 0) {
		/* We read the minimum between `snaplen` and what we really
		 * have in the buffer.
		 */
		dynamic_snaplen_args snaplen_args = {
		        .only_port_range = false,
		        .evt_type = PPME_SOCKET_RECVFROM_X,
		};
		uint16_t snaplen = maps__get_snaplen();
		apply_dynamic_snaplen(regs, &snaplen, &snaplen_args);
		if(snaplen > ret) {
			snaplen = ret;
		}

		/* Parameter 2: data (type: PT_BYTEBUF) */
		unsigned long received_data_pointer = args[1];
		auxmap__store_bytebuf_param(auxmap, received_data_pointer, snaplen, USER);

		/* Parameter 3: tuple (type: PT_SOCKTUPLE) */
		struct sockaddr *usrsockaddr = (struct sockaddr *)args[4];
		/* Notice: the following will push an empty parameter if something goes wrong (e.g.: fd not
		 * valid) */
		auxmap__store_socktuple_param(auxmap, socket_fd, INBOUND, usrsockaddr);
	} else {
		/* Parameter 2: data (type: PT_BYTEBUF) */
		auxmap__store_empty_param(auxmap);

		/* Parameter 3: tuple (type: PT_SOCKTUPLE) */
		auxmap__store_empty_param(auxmap);
	}

	/* Parameter 4: fd (type: PT_FD) */
	auxmap__store_s64_param(auxmap, socket_fd);

	/* Parameter 5: size (type: PT_UINT32) */
	uint32_t size = (uint32_t)args[2];
	auxmap__store_u32_param(auxmap, size);

	/*=============================== COLLECT PARAMETERS  ===========================*/

	auxmap__finalize_event_header(auxmap);

	auxmap__submit_event(auxmap);

	return 0;
}

/*=============================== EXIT EVENT ===========================*/
