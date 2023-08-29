// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/uaccess.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

#include "hw_fence_drv_priv.h"
#include "hw_fence_drv_utils.h"
#include "hw_fence_drv_ipc.h"
#include "hw_fence_drv_debug.h"

/* Global atomic lock */
#define GLOBAL_ATOMIC_STORE(lock, val) global_atomic_store(lock, val)

inline u64 hw_fence_get_qtime(struct hw_fence_driver_data *drv_data)
{
	return readl_relaxed(drv_data->qtime_io_mem);
}

static int init_hw_fences_queues(struct hw_fence_driver_data *drv_data,
	enum hw_fence_mem_reserve mem_reserve_id,
	struct msm_hw_fence_mem_addr *mem_descriptor,
	struct msm_hw_fence_queue *queues, int queues_num,
	int client_id)
{
	struct msm_hw_fence_hfi_queue_table_header *hfi_table_header;
	struct msm_hw_fence_hfi_queue_header *hfi_queue_header;
	void *ptr, *qptr;
	phys_addr_t phys, qphys;
	u32 size, start_queue_offset;
	int headers_size, queue_size, payload_size;
	int i, ret = 0;

	HWFNC_DBG_INIT("mem_reserve_id:%d client_id:%d\n", mem_reserve_id, client_id);
	switch (mem_reserve_id) {
	case HW_FENCE_MEM_RESERVE_CTRL_QUEUE:
		headers_size = HW_FENCE_HFI_CTRL_HEADERS_SIZE;
		queue_size = drv_data->hw_fence_ctrl_queue_size;
		payload_size = HW_FENCE_CTRL_QUEUE_PAYLOAD;
		break;
	case HW_FENCE_MEM_RESERVE_CLIENT_QUEUE:
		headers_size = HW_FENCE_HFI_CLIENT_HEADERS_SIZE;
		queue_size = drv_data->hw_fence_client_queue_size;
		payload_size = HW_FENCE_CLIENT_QUEUE_PAYLOAD;
		break;
	default:
		HWFNC_ERR("Unexpected mem reserve id: %d\n", mem_reserve_id);
		return -EINVAL;
	}

	/* Reserve Virtual and Physical memory for HFI headers */
	ret = hw_fence_utils_reserve_mem(drv_data, mem_reserve_id, &phys, &ptr, &size, client_id);
	if (ret) {
		HWFNC_ERR("Failed to reserve id:%d client %d\n", mem_reserve_id, client_id);
		return -ENOMEM;
	}
	HWFNC_DBG_INIT("phys:0x%x ptr:0x%pK size:%d\n", phys, ptr, size);

	/* Populate Memory descriptor with address */
	mem_descriptor->virtual_addr = ptr;
	mem_descriptor->device_addr = phys;
	mem_descriptor->size = size; /* bytes */
	mem_descriptor->mem_data = NULL; /* Currently we don't need any special info */

	HWFNC_DBG_INIT("Initialize headers\n");
	/* Initialize headers info within hfi memory */
	hfi_table_header = (struct msm_hw_fence_hfi_queue_table_header *)ptr;
	hfi_table_header->version = 0;
	hfi_table_header->size = size; /* bytes */
	/* Offset, from the Base Address, where the first queue header starts */
	hfi_table_header->qhdr0_offset =
		sizeof(struct msm_hw_fence_hfi_queue_table_header);
	hfi_table_header->qhdr_size =
		sizeof(struct msm_hw_fence_hfi_queue_header);
	hfi_table_header->num_q = queues_num; /* number of queues */
	hfi_table_header->num_active_q = queues_num;

	/* Initialize Queues Info within HFI memory */

	/*
	 * Calculate offset where hfi queue header starts, which it is at the
	 * end of the hfi table header
	 */
	HWFNC_DBG_INIT("Initialize queues\n");
	hfi_queue_header = (struct msm_hw_fence_hfi_queue_header *)
					   ((char *)ptr + HW_FENCE_HFI_TABLE_HEADER_SIZE);
	for (i = 0; i < queues_num; i++) {
		HWFNC_DBG_INIT("init queue[%d]\n", i);

		/* Calculate the offset where the Queue starts */
		start_queue_offset = headers_size + (i * queue_size); /* Bytes */
		qphys = phys + start_queue_offset; /* start of the PA for the queue elems */
		qptr = (char *)ptr + start_queue_offset; /* start of the va for queue elems */

		/* Set the physical start address in the HFI queue header */
		hfi_queue_header->start_addr = qphys;

		/* Set the queue type (i.e. RX or TX queue) */
		hfi_queue_header->type = (i == 0) ? HW_FENCE_TX_QUEUE : HW_FENCE_RX_QUEUE;

		/* Set the size of this header */
		hfi_queue_header->queue_size = queue_size;

		/* Set the payload size */
		hfi_queue_header->pkt_size = payload_size;

		/* Store Memory info in the Client data */
		queues[i].va_queue = qptr;
		queues[i].pa_queue = qphys;
		queues[i].va_header = hfi_queue_header;
		queues[i].q_size_bytes = queue_size;
		HWFNC_DBG_INIT("init:%s client:%d q[%d] va=0x%pK pa=0x%x hd:0x%pK sz:%u pkt:%d\n",
			hfi_queue_header->type == HW_FENCE_TX_QUEUE ? "TX_QUEUE" : "RX_QUEUE",
			client_id, i, queues[i].va_queue, queues[i].pa_queue, queues[i].va_header,
			queues[i].q_size_bytes, payload_size);

		/* Next header */
		hfi_queue_header++;
	}

	return ret;
}

static inline _lock_client_queue(int queue_type)
{
	/* Only lock Rx Queue */
	return (queue_type == (HW_FENCE_RX_QUEUE - 1)) ? true : false;
}

char *_get_queue_type(int queue_type)
{
	return (queue_type == (HW_FENCE_RX_QUEUE - 1)) ? "RXQ" : "TXQ";
}

int hw_fence_read_queue(struct msm_hw_fence_client *hw_fence_client,
		 struct msm_hw_fence_queue_payload *payload, int queue_type)
{
	struct msm_hw_fence_hfi_queue_header *hfi_header;
	struct msm_hw_fence_queue *queue;
	u32 read_idx;
	u32 write_idx;
	u32 to_read_idx;
	u32 *read_ptr;
	u32 payload_size_u32;
	u32 q_size_u32;
	struct msm_hw_fence_queue_payload *read_ptr_payload;

	if (queue_type >= HW_FENCE_CLIENT_QUEUES || !hw_fence_client || !payload) {
		HWFNC_ERR("Invalid queue type:%s hw_fence_client:0x%pK payload:0x%pK\n", queue_type,
			hw_fence_client, payload);
		return -EINVAL;
	}

	queue = &hw_fence_client->queues[queue_type];
	hfi_header = queue->va_header;

	q_size_u32 = (queue->q_size_bytes / sizeof(u32));
	payload_size_u32 = (sizeof(struct msm_hw_fence_queue_payload) / sizeof(u32));
	HWFNC_DBG_Q("sizeof payload:%d\n", sizeof(struct msm_hw_fence_queue_payload));

	if (!hfi_header || !payload) {
		HWFNC_ERR("Invalid queue\n");
		return -EINVAL;
	}

	/* Make sure data is ready before read */
	mb();

	/* Get read and write index */
	read_idx = readl_relaxed(&hfi_header->read_index);
	write_idx = readl_relaxed(&hfi_header->write_index);

	HWFNC_DBG_Q("read client:%d rd_ptr:0x%pK wr_ptr:0x%pK rd_idx:%d wr_idx:%d queue:0x%pK\n",
		hw_fence_client->client_id, &hfi_header->read_index, &hfi_header->write_index,
		read_idx, write_idx, queue);

	if (read_idx == write_idx) {
		HWFNC_DBG_Q("Nothing to read!\n");
		return 0;
	}

	/* Move the pointer where we need to read and cast it */
	read_ptr = ((u32 *)queue->va_queue + read_idx);
	read_ptr_payload = (struct msm_hw_fence_queue_payload *)read_ptr;
	HWFNC_DBG_Q("read_ptr:0x%pK queue: va=0x%pK pa=0x%pK read_ptr_payload:0x%pK\n", read_ptr,
		queue->va_queue, queue->pa_queue, read_ptr_payload);

	/* Calculate the index after the read */
	to_read_idx = read_idx + payload_size_u32;

	/*
	 * wrap-around case, here we are reading the last element of the queue, therefore set
	 * to_read_idx, which is the index after the read, to the beginning of the
	 * queue
	 */
	if (to_read_idx >= q_size_u32)
		to_read_idx = 0;

	/* Read the Client Queue */
	payload->ctxt_id = readq_relaxed(&read_ptr_payload->ctxt_id);
	payload->seqno = readq_relaxed(&read_ptr_payload->seqno);
	payload->hash = readq_relaxed(&read_ptr_payload->hash);
	payload->flags = readq_relaxed(&read_ptr_payload->flags);
	payload->error = readl_relaxed(&read_ptr_payload->error);

	/* update the read index */
	writel_relaxed(to_read_idx, &hfi_header->read_index);

	/* update memory for the index */
	wmb();

	/* Return one if queue still has contents after read */
	return to_read_idx == write_idx ? 0 : 1;
}

/*
 * This function writes to the queue of the client. The 'queue_type' determines
 * if this function is writing to the rx or tx queue
 */
int hw_fence_update_queue(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client, u64 ctxt_id, u64 seqno, u64 hash,
	u64 flags, u32 error, int queue_type)
{
	struct msm_hw_fence_hfi_queue_header *hfi_header;
	struct msm_hw_fence_queue *queue;
	u32 read_idx;
	u32 write_idx;
	u32 to_write_idx;
	u32 q_size_u32;
	u32 q_free_u32;
	u32 *q_payload_write_ptr;
	u32 payload_size, payload_size_u32;
	struct msm_hw_fence_queue_payload *write_ptr_payload;
	bool lock_client = false;
	u32 lock_idx;
	u64 timestamp;
	int ret = 0;

	if (queue_type >= HW_FENCE_CLIENT_QUEUES) {
		HWFNC_ERR("Invalid queue type:%s\n", queue_type);
		return -EINVAL;
	}

	queue = &hw_fence_client->queues[queue_type];
	hfi_header = queue->va_header;

	q_size_u32 = (queue->q_size_bytes / sizeof(u32));
	payload_size = sizeof(struct msm_hw_fence_queue_payload);
	payload_size_u32 = (payload_size / sizeof(u32));

	if (!hfi_header) {
		HWFNC_ERR("Invalid queue\n");
		return -EINVAL;
	}

	/*
	 * We need to lock the client if there is an Rx Queue update, since that
	 * is the only time when HW Fence driver can have a race condition updating
	 * the Rx Queue, which also could be getting updated by the Fence CTL
	 */
	lock_client = _lock_client_queue(queue_type);
	if (lock_client) {
		lock_idx = hw_fence_client->client_id - 1;

		if (lock_idx >= drv_data->client_lock_tbl_cnt) {
			HWFNC_ERR("lock for client id:%d exceed max:%d\n",
				hw_fence_client->client_id, drv_data->client_lock_tbl_cnt);
			return -EINVAL;
		}
		HWFNC_DBG_Q("Locking client id:%d: idx:%d\n", hw_fence_client->client_id, lock_idx);

		/* lock the client rx queue to update */
		GLOBAL_ATOMIC_STORE(&drv_data->client_lock_tbl[lock_idx], 1); /* lock */
	}

	/* Make sure data is ready before read */
	mb();

	/* Get read and write index */
	read_idx = readl_relaxed(&hfi_header->read_index);
	write_idx = readl_relaxed(&hfi_header->write_index);

	HWFNC_DBG_Q("wr client:%d rd_ptr:0x%pK wr_ptr:0x%pK rd_idx:%d wr_idx:%d q:0x%pK type:%d\n",
		hw_fence_client->client_id, &hfi_header->read_index, &hfi_header->write_index,
		read_idx, write_idx, queue, queue_type);

	/* Check queue to make sure message will fit */
	q_free_u32 = read_idx <= write_idx ? (q_size_u32 - (write_idx - read_idx)) :
		(read_idx - write_idx);
	if (q_free_u32 <= payload_size_u32) {
		HWFNC_ERR("cannot fit the message size:%d\n", payload_size_u32);
		ret = -EINVAL;
		goto exit;
	}
	HWFNC_DBG_Q("q_free_u32:%d payload_size_u32:%d\n", q_free_u32, payload_size_u32);

	/* Move the pointer where we need to write and cast it */
	q_payload_write_ptr = ((u32 *)queue->va_queue + write_idx);
	write_ptr_payload = (struct msm_hw_fence_queue_payload *)q_payload_write_ptr;
	HWFNC_DBG_Q("q_payload_write_ptr:0x%pK queue: va=0x%pK pa=0x%pK write_ptr_payload:0x%pK\n",
		q_payload_write_ptr, queue->va_queue, queue->pa_queue, write_ptr_payload);

	/* calculate the index after the write */
	to_write_idx = write_idx + payload_size_u32;

	HWFNC_DBG_Q("to_write_idx:%d write_idx:%d payload_size\n", to_write_idx, write_idx,
		payload_size_u32);
	HWFNC_DBG_L("client_id:%d update %s hash:%llu ctx_id:%llu seqno:%llu flags:%llu error:%u\n",
		hw_fence_client->client_id, _get_queue_type(queue_type),
		hash, ctxt_id, seqno, flags, error);

	/*
	 * wrap-around case, here we are writing to the last element of the queue, therefore
	 * set to_write_idx, which is the index after the write, to the beginning of the
	 * queue
	 */
	if (to_write_idx >= q_size_u32)
		to_write_idx = 0;

	/* Update Client Queue */
	writeq_relaxed(payload_size, &write_ptr_payload->size);
	writew_relaxed(HW_FENCE_PAYLOAD_TYPE_1, &write_ptr_payload->type);
	writew_relaxed(HW_FENCE_PAYLOAD_REV(1, 0), &write_ptr_payload->version);
	writeq_relaxed(ctxt_id, &write_ptr_payload->ctxt_id);
	writeq_relaxed(seqno, &write_ptr_payload->seqno);
	writeq_relaxed(hash, &write_ptr_payload->hash);
	writeq_relaxed(flags, &write_ptr_payload->flags);
	writel_relaxed(error, &write_ptr_payload->error);
	timestamp = hw_fence_get_qtime(drv_data);
	writel_relaxed(timestamp, &write_ptr_payload->timestamp_lo);
	writel_relaxed(timestamp >> 32, &write_ptr_payload->timestamp_hi);

	/* update memory for the message */
	wmb();

	/* update the write index */
	writel_relaxed(to_write_idx, &hfi_header->write_index);

	/* update memory for the index */
	wmb();

exit:
	if (lock_client)
		GLOBAL_ATOMIC_STORE(&drv_data->client_lock_tbl[lock_idx], 0); /* unlock */

	return ret;
}

static int init_global_locks(struct hw_fence_driver_data *drv_data)
{
	struct msm_hw_fence_mem_addr *mem_descriptor;
	phys_addr_t phys;
	void *ptr;
	u32 size;
	int ret;

	ret = hw_fence_utils_reserve_mem(drv_data, HW_FENCE_MEM_RESERVE_LOCKS_REGION, &phys, &ptr,
		&size, 0);
	if (ret) {
		HWFNC_ERR("Failed to reserve clients locks mem %d\n", ret);
		return -ENOMEM;
	}
	HWFNC_DBG_INIT("phys:0x%x ptr:0x%pK size:%d\n", phys, ptr, size);

	/* Populate Memory descriptor with address */
	mem_descriptor = &drv_data->clients_locks_mem_desc;
	mem_descriptor->virtual_addr = ptr;
	mem_descriptor->device_addr = phys;
	mem_descriptor->size = size;
	mem_descriptor->mem_data = NULL; /* not storing special info for now */

	/* Initialize internal pointers for managing the tables */
	drv_data->client_lock_tbl = (u64 *)drv_data->clients_locks_mem_desc.virtual_addr;
	drv_data->client_lock_tbl_cnt = drv_data->clients_locks_mem_desc.size / sizeof(u64);

	return 0;
}

static int init_hw_fences_table(struct hw_fence_driver_data *drv_data)
{
	struct msm_hw_fence_mem_addr *mem_descriptor;
	phys_addr_t phys;
	void *ptr;
	u32 size;
	int ret;

	ret = hw_fence_utils_reserve_mem(drv_data, HW_FENCE_MEM_RESERVE_TABLE, &phys, &ptr,
		&size, 0);
	if (ret) {
		HWFNC_ERR("Failed to reserve table mem %d\n", ret);
		return -ENOMEM;
	}
	HWFNC_DBG_INIT("phys:0x%x ptr:0x%pK size:%d\n", phys, ptr, size);

	/* Populate Memory descriptor with address */
	mem_descriptor = &drv_data->hw_fences_mem_desc;
	mem_descriptor->virtual_addr = ptr;
	mem_descriptor->device_addr = phys;
	mem_descriptor->size = size;
	mem_descriptor->mem_data = NULL; /* not storing special info for now */

	/* Initialize internal pointers for managing the tables */
	drv_data->hw_fences_tbl = (struct msm_hw_fence *)drv_data->hw_fences_mem_desc.virtual_addr;
	drv_data->hw_fences_tbl_cnt = drv_data->hw_fences_mem_desc.size /
		sizeof(struct msm_hw_fence);

	HWFNC_DBG_INIT("hw_fences_table:0x%pK cnt:%u\n", drv_data->hw_fences_tbl,
		drv_data->hw_fences_tbl_cnt);

	return 0;
}

static int init_ctrl_queue(struct hw_fence_driver_data *drv_data)
{
	struct msm_hw_fence_mem_addr *mem_descriptor;
	int ret;

	mem_descriptor = &drv_data->ctrl_queue_mem_desc;

	/* Init ctrl queue */
	ret = init_hw_fences_queues(drv_data, HW_FENCE_MEM_RESERVE_CTRL_QUEUE,
		mem_descriptor, drv_data->ctrl_queues,
		HW_FENCE_CTRL_QUEUES, 0);
	if (ret)
		HWFNC_ERR("Failure to init ctrl queue\n");

	return ret;
}

int hw_fence_init(struct hw_fence_driver_data *drv_data)
{
	int ret;
	__le32 *mem;

	ret = hw_fence_utils_parse_dt_props(drv_data);
	if (ret) {
		HWFNC_ERR("failed to set dt properties\n");
		goto exit;
	}

	/* Allocate hw fence driver mem pool and share it with HYP */
	ret = hw_fence_utils_alloc_mem(drv_data);
	if (ret) {
		HWFNC_ERR("failed to alloc base memory\n");
		goto exit;
	}

	/* Initialize ctrl queue */
	ret = init_ctrl_queue(drv_data);
	if (ret)
		goto exit;

	ret = init_global_locks(drv_data);
	if (ret)
		goto exit;
	HWFNC_DBG_INIT("Locks allocated at 0x%pK total locks:%d\n", drv_data->client_lock_tbl,
		drv_data->client_lock_tbl_cnt);

	/* Initialize hw fences table */
	ret = init_hw_fences_table(drv_data);
	if (ret)
		goto exit;

	/* Map ipcc registers */
	ret = hw_fence_utils_map_ipcc(drv_data);
	if (ret) {
		HWFNC_ERR("ipcc regs mapping failed\n");
		goto exit;
	}

	/* Map time register */
	ret = hw_fence_utils_map_qtime(drv_data);
	if (ret) {
		HWFNC_ERR("qtime reg mapping failed\n");
		goto exit;
	}

	/* Map ctl_start registers */
	ret = hw_fence_utils_map_ctl_start(drv_data);
	if (ret) {
		/* This is not fatal error, since platfoms with dpu-ipc
		 * won't use this option
		 */
		HWFNC_WARN("no ctl_start regs, won't trigger the frame\n");
	}

	/* Init debugfs */
	ret = hw_fence_debug_debugfs_register(drv_data);
	if (ret) {
		HWFNC_ERR("debugfs init failed\n");
		goto exit;
	}

	/* Init vIRQ from VM */
	ret = hw_fence_utils_init_virq(drv_data);
	if (ret) {
		HWFNC_ERR("failed to init virq\n");
		goto exit;
	}

	mem = drv_data->io_mem_base;
	HWFNC_DBG_H("memory ptr:0x%pK val:0x%x\n", mem, *mem);

	HWFNC_DBG_INIT("HW Fences Table Initialized: 0x%pK cnt:%d\n",
		drv_data->hw_fences_tbl, drv_data->hw_fences_tbl_cnt);

exit:
	return ret;
}

int hw_fence_alloc_client_resources(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client,
	struct msm_hw_fence_mem_addr *mem_descriptor)
{
	int ret;

	/* Init client queues */
	ret = init_hw_fences_queues(drv_data, HW_FENCE_MEM_RESERVE_CLIENT_QUEUE,
		&hw_fence_client->mem_descriptor, hw_fence_client->queues,
		HW_FENCE_CLIENT_QUEUES, hw_fence_client->client_id);
	if (ret) {
		HWFNC_ERR("Failure to init the queue for client:%d\n",
			hw_fence_client->client_id);
		goto exit;
	}

	/* Init client memory descriptor */
	memcpy(mem_descriptor, &hw_fence_client->mem_descriptor,
		sizeof(struct msm_hw_fence_mem_addr));

exit:
	return ret;
}

int hw_fence_init_controller_signal(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client)
{
	int ret = 0;

	/*
	 * Initialize IPCC Signals for this client
	 *
	 * NOTE: Fore each Client HW-Core, the client drivers might be the ones making
	 * it's own initialization (in case that any hw-sequence must be enforced),
	 * however, if that is  not the case, any per-client ipcc init to enable the
	 * signaling, can go here.
	 */
	switch (hw_fence_client->client_id) {
	case HW_FENCE_CLIENT_ID_CTX0:
		/* nothing to initialize for gpu client */
		break;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	case HW_FENCE_CLIENT_ID_VAL0:
	case HW_FENCE_CLIENT_ID_VAL1:
	case HW_FENCE_CLIENT_ID_VAL2:
	case HW_FENCE_CLIENT_ID_VAL3:
	case HW_FENCE_CLIENT_ID_VAL4:
	case HW_FENCE_CLIENT_ID_VAL5:
	case HW_FENCE_CLIENT_ID_VAL6:
		/* nothing to initialize for validation clients */
		break;
#endif /* CONFIG_DEBUG_FS */
	case HW_FENCE_CLIENT_ID_CTL0:
	case HW_FENCE_CLIENT_ID_CTL1:
	case HW_FENCE_CLIENT_ID_CTL2:
	case HW_FENCE_CLIENT_ID_CTL3:
	case HW_FENCE_CLIENT_ID_CTL4:
	case HW_FENCE_CLIENT_ID_CTL5:
#ifdef HW_DPU_IPCC
		/* initialize ipcc signals for dpu clients */
		HWFNC_DBG_H("init_controller_signal: DPU client:%d initialized:%d\n",
			hw_fence_client->client_id, drv_data->ipcc_dpu_initialized);
		if (!drv_data->ipcc_dpu_initialized) {
			drv_data->ipcc_dpu_initialized = true;

			/* Init dpu client ipcc signal */
			hw_fence_ipcc_enable_dpu_signaling(drv_data);
		}
#endif /* HW_DPU_IPCC */
		break;
	default:
		HWFNC_ERR("Unexpected client:%d\n", hw_fence_client->client_id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

int hw_fence_init_controller_resources(struct msm_hw_fence_client *hw_fence_client)
{

	/*
	 * Initialize Fence Controller resources for this Client,
	 *  here we need to use the CTRL queue to communicate to the Fence
	 *  Controller the shared memory for the Rx/Tx queue for this client
	 *  as well as any information that Fence Controller might need to
	 *  know for this client.
	 *
	 * NOTE: For now, we are doing a static allocation of the
	 *  client's queues, so currently we don't need any notification
	 *  to the Fence CTL here through the CTRL queue.
	 *  Later-on we might need it, once the PVM to SVM (and vice versa)
	 *  communication for initialization is supported.
	 */

	return 0;
}

void hw_fence_cleanup_client(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client)
{
	/*
	 * Deallocate any resource allocated for this client.
	 *  If fence controller was notified about existence of this client,
	 *  we will need to notify fence controller that this client is gone
	 *
	 * NOTE: Since currently we are doing a 'fixed' memory for the clients queues,
	 *  we don't need any notification to the Fence Controller, yet..
	 *  however, if the memory allocation is removed from 'fixed' to a dynamic
	 *  allocation, then we will need to notify FenceCTL about the client that is
	 *  going-away here.
	 */
	mutex_lock(&drv_data->clients_mask_lock);
	drv_data->client_id_mask &= ~BIT(hw_fence_client->client_id);
	drv_data->clients[hw_fence_client->client_id] = NULL;
	mutex_unlock(&drv_data->clients_mask_lock);

	/* Deallocate client's object */
	HWFNC_DBG_LUT("freeing client_id:%d\n", hw_fence_client->client_id);
	kfree(hw_fence_client);
}

static inline int _calculate_hash(u32 table_total_entries, u64 context, u64 seqno,
	u64 step, u64 *hash)
{
	u64 m_size = table_total_entries;
	int val = 0;

	if (step == 0) {
		u64 a_multiplier = HW_FENCE_HASH_A_MULT;
		u64 c_multiplier = HW_FENCE_HASH_C_MULT;
		u64 b_multiplier = context + (context - 1); /* odd multiplier */

		/*
		 * if m, is power of 2, we can optimize with right shift,
		 * for now we don't do it, to avoid assuming a power of two
		 */
		*hash = (a_multiplier * seqno * b_multiplier + (c_multiplier * context)) % m_size;
	} else {
		if (step >= m_size) {
			/*
			 * If we already traversed the whole table, return failure since this means
			 * there are not available spots, table is either full or full-enough
			 * that we couldn't find an available spot after traverse the whole table.
			 * Ideally table shouldn't be so full that we cannot find a value after some
			 * iterations, so this maximum step size could be optimized to fail earlier.
			 */
			HWFNC_ERR("Fence Table tranversed and no available space!\n");
			val = -EINVAL;
		} else {
			/*
			 * Linearly increment the hash value to find next element in the table
			 * note that this relies in the 'scrambled' data from the original hash
			 * Also, add a mod division to wrap-around in case that we reached the
			 * end of the table
			 */
			*hash = (*hash + 1) % m_size;
		}
	}

	return val;
}

static inline struct msm_hw_fence *_get_hw_fence(u32 table_total_entries,
	struct msm_hw_fence *hw_fences_tbl,
	u64 hash)
{
	if (hash >= table_total_entries) {
		HWFNC_ERR("hash:%llu out of max range:%llu\n",
			hash, table_total_entries);
		return NULL;
	}

	return &hw_fences_tbl[hash];
}

static bool _is_hw_fence_free(struct msm_hw_fence *hw_fence, u64 context, u64 seqno)
{
	/* If valid is set, the hw fence is not free */
	return hw_fence->valid ? false : true;
}

static bool _hw_fence_match(struct msm_hw_fence *hw_fence, u64 context, u64 seqno)
{
	return ((hw_fence->ctx_id == context && hw_fence->seq_id == seqno) ? true : false);
}

/* clears everything but the 'valid' field */
static void _cleanup_hw_fence(struct msm_hw_fence *hw_fence)
{
	int i;

	hw_fence->error = 0;
	wmb(); /* update memory to avoid mem-abort */
	hw_fence->ctx_id = 0;
	hw_fence->seq_id = 0;
	hw_fence->wait_client_mask = 0;
	hw_fence->fence_allocator = 0;
	hw_fence->fence_signal_client = 0;

	hw_fence->flags = 0;

	hw_fence->fence_create_time = 0;
	hw_fence->fence_trigger_time = 0;
	hw_fence->fence_wait_time = 0;
	hw_fence->debug_refcount = 0;
	hw_fence->parents_cnt = 0;
	hw_fence->pending_child_cnt = 0;

	for (i = 0; i < MSM_HW_FENCE_MAX_JOIN_PARENTS; i++)
		hw_fence->parent_list[i] = HW_FENCE_INVALID_PARENT_FENCE;
}

/* This function must be called with the hw fence lock */
static void  _reserve_hw_fence(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence *hw_fence, u32 client_id,
	u64 context, u64 seqno, u32 hash, u32 pending_child_cnt)
{
	_cleanup_hw_fence(hw_fence);

	/* reserve this HW fence */
	hw_fence->valid = 1;

	hw_fence->ctx_id = context;
	hw_fence->seq_id = seqno;
	hw_fence->flags = 0; /* fence just reserved, there shouldn't be any flags set */
	hw_fence->fence_allocator = client_id;
	hw_fence->fence_create_time = hw_fence_get_qtime(drv_data);
	hw_fence->debug_refcount++;

	HWFNC_DBG_LUT("Reserved fence client:%d ctx:%llu seq:%llu hash:%llu\n",
		client_id, context, seqno, hash);
}

/* This function must be called with the hw fence lock */
static void  _unreserve_hw_fence(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence *hw_fence, u32 client_id,
	u64 context, u64 seqno, u32 hash, u32 pending_child_cnt)
{
	_cleanup_hw_fence(hw_fence);

	/* unreserve this HW fence */
	hw_fence->valid = 0;

	HWFNC_DBG_LUT("Unreserved fence client:%d ctx:%llu seq:%llu hash:%llu\n",
		client_id, context, seqno, hash);
}

/* This function must be called with the hw fence lock */
static void  _reserve_join_fence(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence *hw_fence, u32 client_id, u64 context,
	u64 seqno, u32 hash, u32 pending_child_cnt)
{
	_cleanup_hw_fence(hw_fence);

	/* reserve this HW fence */
	hw_fence->valid = true;

	hw_fence->ctx_id = context;
	hw_fence->seq_id = seqno;
	hw_fence->fence_allocator = client_id;
	hw_fence->fence_create_time = hw_fence_get_qtime(drv_data);
	hw_fence->debug_refcount++;

	hw_fence->pending_child_cnt = pending_child_cnt;

	HWFNC_DBG_LUT("Reserved join fence client:%d ctx:%llu seq:%llu hash:%llu\n",
		client_id, context, seqno, hash);
}

/* This function must be called with the hw fence lock */
static void  _fence_found(struct hw_fence_driver_data *drv_data,
	 struct msm_hw_fence *hw_fence, u32 client_id,
	u64 context, u64 seqno, u32 hash, u32 pending_child_cnt)
{
	/*
	 * Do nothing, when this find fence fn is invoked, all processing is done outside.
	 * Currently just keeping this function for debugging purposes, can be removed
	 * in final versions
	 */
	HWFNC_DBG_LUT("Found fence client:%d ctx:%llu seq:%llu hash:%llu\n",
		client_id, context, seqno, hash);
}

char *_get_op_mode(enum hw_fence_lookup_ops op_code)
{
	switch (op_code) {
	case HW_FENCE_LOOKUP_OP_CREATE:
		return "CREATE";
	case HW_FENCE_LOOKUP_OP_DESTROY:
		return "DESTROY";
	case HW_FENCE_LOOKUP_OP_CREATE_JOIN:
		return "CREATE_JOIN";
	case HW_FENCE_LOOKUP_OP_FIND_FENCE:
		return "FIND_FENCE";
	default:
		return "UNKNOWN";
	}

	return "UNKNOWN";
}

struct msm_hw_fence *_hw_fence_lookup_and_process(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence *hw_fences_tbl, u64 context, u64 seqno, u32 client_id,
	u32 pending_child_cnt, enum hw_fence_lookup_ops op_code, u64 *hash)
{
	bool (*compare_fnc)(struct msm_hw_fence *hfence, u64 context, u64 seqno);
	void (*process_fnc)(struct hw_fence_driver_data *drv_data, struct msm_hw_fence *hfence,
			u32 client_id, u64 context, u64 seqno, u32 hash, u32 pending);
	struct msm_hw_fence *hw_fence = NULL;
	u64 step = 0;
	int ret = 0;
	bool hw_fence_found = false;

	if (!hash | !drv_data | !hw_fences_tbl) {
		HWFNC_ERR("Invalid input for hw_fence_lookup\n");
		return NULL;
	}

	*hash = ~0;

	HWFNC_DBG_LUT("hw_fence_lookup: %d\n", op_code);

	switch (op_code) {
	case HW_FENCE_LOOKUP_OP_CREATE:
		compare_fnc = &_is_hw_fence_free;
		process_fnc = &_reserve_hw_fence;
		break;
	case HW_FENCE_LOOKUP_OP_DESTROY:
		compare_fnc = &_hw_fence_match;
		process_fnc = &_unreserve_hw_fence;
		break;
	case HW_FENCE_LOOKUP_OP_CREATE_JOIN:
		compare_fnc = &_is_hw_fence_free;
		process_fnc = &_reserve_join_fence;
		break;
	case HW_FENCE_LOOKUP_OP_FIND_FENCE:
		compare_fnc = &_hw_fence_match;
		process_fnc = &_fence_found;
		break;
	default:
		HWFNC_ERR("Unknown op code:%d\n", op_code);
		return NULL;
	}

	while (!hw_fence_found && (step < drv_data->hw_fence_table_entries)) {

		/* Calculate the Hash for the Fence */
		ret = _calculate_hash(drv_data->hw_fence_table_entries, context, seqno, step, hash);
		if (ret) {
			HWFNC_ERR("error calculating hash ctx:%llu seqno:%llu hash:%llu\n",
				context, seqno, *hash);
			break;
		}
		HWFNC_DBG_LUT("calculated hash:%llu [ctx:%llu seqno:%llu]\n", *hash, context,
			seqno);

		/* Get element from the table using the hash */
		hw_fence = _get_hw_fence(drv_data->hw_fence_table_entries, hw_fences_tbl, *hash);
		HWFNC_DBG_LUT("hw_fence_tbl:0x%pK hw_fence:0x%pK, hash:%llu valid:0x%x\n",
			hw_fences_tbl, hw_fence, *hash, hw_fence ? hw_fence->valid : 0xbad);
		if (!hw_fence) {
			HWFNC_ERR("bad hw fence ctx:%llu seqno:%llu hash:%llu\n",
				context, seqno, *hash);
			break;
		}

		GLOBAL_ATOMIC_STORE(&hw_fence->lock, 1);

		/* compare to either find a free fence or find an allocated fence */
		if (compare_fnc(hw_fence, context, seqno)) {

			/* Process the hw fence found by the algorithm */
			if (process_fnc) {
				process_fnc(drv_data, hw_fence, client_id, context, seqno, *hash,
					pending_child_cnt);

				/* update memory table with processing */
				wmb();
			}

			HWFNC_DBG_L("client_id:%lu op:%s ctx:%llu seqno:%llu hash:%llu step:%llu\n",
				client_id, _get_op_mode(op_code), context, seqno, *hash, step);

			hw_fence_found = true;
		} else {
			if ((op_code == HW_FENCE_LOOKUP_OP_CREATE ||
				op_code == HW_FENCE_LOOKUP_OP_CREATE_JOIN) &&
				seqno == hw_fence->seq_id && context == hw_fence->ctx_id) {
				/* ctx & seqno must be unique creating a hw-fence */
				HWFNC_ERR("cannot create hw fence with same ctx:%llu seqno:%llu\n",
					context, seqno);
				GLOBAL_ATOMIC_STORE(&hw_fence->lock, 0);
				break;
			}
			/* compare can fail if we have a collision, we will linearly resolve it */
			HWFNC_DBG_H("compare failed for hash:%llu [ctx:%llu seqno:%llu]\n", *hash,
				context, seqno);
		}

		GLOBAL_ATOMIC_STORE(&hw_fence->lock, 0);

		/* Increment step for the next loop */
		step++;
	}

	/* If we iterated through the whole list and didn't find the fence, return null */
	if (!hw_fence_found) {
		HWFNC_ERR("fail to create hw-fence step:%llu\n", step);
		hw_fence = NULL;
	}

	HWFNC_DBG_LUT("lookup:%d hw_fence:%pK ctx:%llu seqno:%llu hash:%llu flags:0x%llx\n",
		op_code, hw_fence, context, seqno, *hash, hw_fence ? hw_fence->flags : -1);

	return hw_fence;
}

int hw_fence_create(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client,
	u64 context, u64 seqno, u64 *hash)
{
	u32 client_id = hw_fence_client->client_id;
	struct msm_hw_fence *hw_fences_tbl = drv_data->hw_fences_tbl;

	int ret = 0;

	/* allocate hw fence in table */
	if (!_hw_fence_lookup_and_process(drv_data, hw_fences_tbl,
		context, seqno, client_id, 0, HW_FENCE_LOOKUP_OP_CREATE, hash)) {
		HWFNC_ERR("Fail to create fence client:%lu ctx:%llu seqno:%llu\n",
			client_id, context, seqno);
		ret = -EINVAL;
	}

	return ret;
}

static  inline int _hw_fence_cleanup(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence *hw_fences_tbl, u32 client_id, u64 context, u64 seqno) {
	u64 hash;

	if (!_hw_fence_lookup_and_process(drv_data, hw_fences_tbl,
			context, seqno, client_id, 0, HW_FENCE_LOOKUP_OP_DESTROY, &hash))
		return -EINVAL;

	return 0;
}

int hw_fence_destroy(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client,
	u64 context, u64 seqno)
{
	u32 client_id = hw_fence_client->client_id;
	struct msm_hw_fence *hw_fences_tbl = drv_data->hw_fences_tbl;
	int ret = 0;

	/* remove hw fence from table*/
	if (_hw_fence_cleanup(drv_data, hw_fences_tbl, client_id, context, seqno)) {
		HWFNC_ERR("Fail destroying fence client:%lu ctx:%llu seqno:%llu\n",
			client_id, context, seqno);
		ret = -EINVAL;
	}

	return ret;
}

static struct msm_hw_fence *_hw_fence_process_join_fence(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client,
	struct dma_fence_array *array, u64 *hash, bool create)
{
	struct msm_hw_fence *hw_fences_tbl;
	struct msm_hw_fence *join_fence = NULL;
	u64 context, seqno;
	u32 client_id, pending_child_cnt;

	/*
	 * NOTE: For now we are allocating the join fences from the same table as all
	 * the other fences (i.e. drv_data->hw_fences_tbl), functionally this will work, however,
	 * this might impact the lookup algorithm, since the "join-fences" are created with the
	 * context and seqno of a fence-array, and those might not be changing by the client,
	 * so this will linearly increment the look-up and very likely impact the other fences if
	 * these join-fences start to fill-up a particular region of the fences global table.
	 * So we might have to allocate a different table altogether for these join fences.
	 * However, to do this, just alloc another table and change it here:
	 */
	hw_fences_tbl = drv_data->hw_fences_tbl;

	context = array->base.context;
	seqno = array->base.seqno;
	pending_child_cnt = array->num_fences;
	client_id = HW_FENCE_JOIN_FENCE_CLIENT_ID;

	if (create) {
		/* allocate the fence */
		join_fence = _hw_fence_lookup_and_process(drv_data, hw_fences_tbl, context,
			seqno, client_id, pending_child_cnt, HW_FENCE_LOOKUP_OP_CREATE_JOIN, hash);
		if (!join_fence)
			HWFNC_ERR("Fail to create join fence client:%lu ctx:%llu seqno:%llu\n",
				client_id, context, seqno);
	} else {
		/* destroy the fence */
		if (_hw_fence_cleanup(drv_data, hw_fences_tbl, client_id, context, seqno))
			HWFNC_ERR("Fail destroying join fence client:%lu ctx:%llu seqno:%llu\n",
				client_id, context, seqno);
	}

	return join_fence;
}

struct msm_hw_fence *msm_hw_fence_find(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client,
	u64 context, u64 seqno, u64 *hash)
{
	struct msm_hw_fence *hw_fences_tbl = drv_data->hw_fences_tbl;
	struct msm_hw_fence *hw_fence;
	u32 client_id = hw_fence_client ? hw_fence_client->client_id : 0xff;

	/* find the hw fence */
	hw_fence = _hw_fence_lookup_and_process(drv_data, hw_fences_tbl, context,
		seqno, client_id, 0, HW_FENCE_LOOKUP_OP_FIND_FENCE, hash);
	if (!hw_fence)
		HWFNC_ERR("Fail to find hw fence client:%lu ctx:%llu seqno:%llu\n",
			client_id, context, seqno);

	return hw_fence;
}

static void _fence_ctl_signal(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client, struct msm_hw_fence *hw_fence, u64 hash,
	u64 flags, u32 error)
{
	u32 tx_client_id = drv_data->ipcc_client_id;
	u32 rx_client_id = hw_fence_client->ipc_client_id;

	HWFNC_DBG_H("We must signal the client now! hfence hash:%llu\n", hash);

	/* Write to Rx queue */
	if (hw_fence_client->update_rxq)
		hw_fence_update_queue(drv_data, hw_fence_client, hw_fence->ctx_id,
			hw_fence->seq_id, hash, flags, error, HW_FENCE_RX_QUEUE - 1);

	/* Signal the hw fence now */
	hw_fence_ipcc_trigger_signal(drv_data, tx_client_id, rx_client_id,
		hw_fence_client->ipc_signal_id);
}

static void _cleanup_join_and_child_fences(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client, int iteration, struct dma_fence_array *array,
	struct msm_hw_fence *join_fence, u64 hash_join_fence)
{
	struct dma_fence *child_fence;
	struct msm_hw_fence *hw_fence_child;
	int idx, j;
	u64 hash = 0;

	/* cleanup the child-fences from the parent join-fence */
	for (idx = iteration; idx >= 0; idx--) {
		child_fence = array->fences[idx];

		hw_fence_child = msm_hw_fence_find(drv_data, hw_fence_client, child_fence->context,
			child_fence->seqno, &hash);
		if (!hw_fence_child) {
			HWFNC_ERR("Cannot cleanup child fence context:%lu seqno:%lu hash:%lu\n",
				child_fence->context, child_fence->seqno, hash);

			/*
			 * ideally this should not have happened, but if it did, try to keep
			 * cleaning-up other fences after printing the error
			 */
			continue;
		}

		/* lock the child while we clean it up from the parent join-fence */
		GLOBAL_ATOMIC_STORE(&hw_fence_child->lock, 1); /* lock */
		for (j = hw_fence_child->parents_cnt; j > 0; j--) {

			if (j > MSM_HW_FENCE_MAX_JOIN_PARENTS) {
				HWFNC_ERR("Invalid max parents_cnt:%d, will reset to max:%d\n",
					hw_fence_child->parents_cnt, MSM_HW_FENCE_MAX_JOIN_PARENTS);

				j = MSM_HW_FENCE_MAX_JOIN_PARENTS;
			}

			if (hw_fence_child->parent_list[j - 1] == hash_join_fence) {
				hw_fence_child->parent_list[j - 1] = HW_FENCE_INVALID_PARENT_FENCE;

				if (hw_fence_child->parents_cnt)
					hw_fence_child->parents_cnt--;

				/* update memory for the table update */
				wmb();
			}
		}
		GLOBAL_ATOMIC_STORE(&hw_fence_child->lock, 0); /* unlock */
	}

	/* destroy join fence */
	_hw_fence_process_join_fence(drv_data, hw_fence_client, array, &hash_join_fence,
		false);
}

int hw_fence_process_fence_array(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client, struct dma_fence_array *array)
{
	struct msm_hw_fence *join_fence;
	struct msm_hw_fence *hw_fence_child;
	struct dma_fence *child_fence;
	u32 signaled_fences = 0;
	u64 hash_join_fence, hash;
	int i, ret = 0;

	/*
	 * Create join fence from the join-fences table,
	 * This function initializes:
	 * join_fence->pending_child_count = array->num_fences
	 */
	join_fence = _hw_fence_process_join_fence(drv_data, hw_fence_client, array,
		&hash_join_fence, true);
	if (!join_fence) {
		HWFNC_ERR("cannot alloc hw fence for join fence array\n");
		return -EINVAL;
	}

	/* update this as waiting client of the join-fence */
	GLOBAL_ATOMIC_STORE(&join_fence->lock, 1); /* lock */
	join_fence->wait_client_mask |= BIT(hw_fence_client->client_id);
	GLOBAL_ATOMIC_STORE(&join_fence->lock, 0); /* unlock */

	/* Iterate through fences of the array */
	for (i = 0; i < array->num_fences; i++) {
		child_fence = array->fences[i];

		/* Nested fence-arrays are not supported */
		if (to_dma_fence_array(child_fence)) {
			HWFNC_ERR("This is a nested fence, fail!\n");
			ret = -EINVAL;
			goto error_array;
		}

		/* All elements in the fence-array must be hw-fences */
		if (!test_bit(MSM_HW_FENCE_FLAG_ENABLED_BIT, &child_fence->flags)) {
			HWFNC_ERR("DMA Fence in FenceArray is not a HW Fence\n");
			ret = -EINVAL;
			goto error_array;
		}

		/* Find the HW Fence in the Global Table */
		hw_fence_child = msm_hw_fence_find(drv_data, hw_fence_client, child_fence->context,
			child_fence->seqno, &hash);
		if (!hw_fence_child) {
			HWFNC_ERR("Cannot find child fence context:%lu seqno:%lu hash:%lu\n",
				child_fence->context, child_fence->seqno, hash);
			ret = -EINVAL;
			goto error_array;
		}

		GLOBAL_ATOMIC_STORE(&hw_fence_child->lock, 1); /* lock */
		if (hw_fence_child->flags & MSM_HW_FENCE_FLAG_SIGNAL) {

			/* child fence is already signaled */
			GLOBAL_ATOMIC_STORE(&join_fence->lock, 1); /* lock */
			join_fence->pending_child_cnt--;

			/* update memory for the table update */
			wmb();

			GLOBAL_ATOMIC_STORE(&join_fence->lock, 0); /* unlock */
			signaled_fences++;
		} else {

			/* child fence is not signaled */
			hw_fence_child->parents_cnt++;

			if (hw_fence_child->parents_cnt >= MSM_HW_FENCE_MAX_JOIN_PARENTS
					|| hw_fence_child->parents_cnt < 1) {

				/* Max number of parents for a fence is exceeded */
				HWFNC_ERR("DMA Fence in FenceArray exceeds parents:%d\n",
					hw_fence_child->parents_cnt);
				hw_fence_child->parents_cnt--;

				/* update memory for the table update */
				wmb();

				GLOBAL_ATOMIC_STORE(&hw_fence_child->lock, 0); /* unlock */
				ret = -EINVAL;
				goto error_array;
			}

			hw_fence_child->parent_list[hw_fence_child->parents_cnt - 1] =
				hash_join_fence;

			/* update memory for the table update */
			wmb();
		}
		GLOBAL_ATOMIC_STORE(&hw_fence_child->lock, 0); /* unlock */
	}

	/* all fences were signaled, signal client now */
	if (signaled_fences == array->num_fences) {

		/* signal the join hw fence */
		_fence_ctl_signal(drv_data, hw_fence_client, join_fence, hash_join_fence, 0, 0);

		/*
		 * job of the join-fence is finished since we already signaled,
		 * we can delete it now. This can happen when all the fences that
		 * are part of the join-fence are already signaled.
		 */
		_hw_fence_process_join_fence(drv_data, hw_fence_client, array, &hash_join_fence,
			false);
	}

	return ret;

error_array:
	_cleanup_join_and_child_fences(drv_data, hw_fence_client, i, array, join_fence,
		hash_join_fence);

	return -EINVAL;
}

int hw_fence_register_wait_client(struct hw_fence_driver_data *drv_data,
		struct msm_hw_fence_client *hw_fence_client, u64 context, u64 seqno)
{
	struct msm_hw_fence *hw_fence;
	u64 hash;

	/* find the hw fence within the table */
	hw_fence = msm_hw_fence_find(drv_data, hw_fence_client, context, seqno, &hash);
	if (!hw_fence) {
		HWFNC_ERR("Cannot find fence!\n");
		return -EINVAL;
	}

	GLOBAL_ATOMIC_STORE(&hw_fence->lock, 1); /* lock */

	/* register client in the hw fence */
	hw_fence->wait_client_mask |= BIT(hw_fence_client->client_id);
	hw_fence->fence_wait_time = hw_fence_get_qtime(drv_data);
	hw_fence->debug_refcount++;

	/* update memory for the table update */
	wmb();

	/* if hw fence already signaled, signal the client */
	if (hw_fence->flags & MSM_HW_FENCE_FLAG_SIGNAL)
		_fence_ctl_signal(drv_data, hw_fence_client, hw_fence, hash, 0, 0);

	GLOBAL_ATOMIC_STORE(&hw_fence->lock, 0); /* unlock */

	return 0;
}

int hw_fence_process_fence(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client,
	struct dma_fence *fence)
{
	int ret = 0;

	if (!drv_data | !hw_fence_client | !fence) {
		HWFNC_ERR("Invalid Input!\n");
		return -EINVAL;
	}
	/* fence must be hw-fence */
	if (!test_bit(MSM_HW_FENCE_FLAG_ENABLED_BIT, &fence->flags)) {
		HWFNC_ERR("DMA Fence in is not a HW Fence flags:0x%llx\n", fence->flags);
		return -EINVAL;
	}

	ret = hw_fence_register_wait_client(drv_data, hw_fence_client, fence->context,
		fence->seqno);
	if (ret)
		HWFNC_ERR("Error registering for wait client:%d\n", hw_fence_client->client_id);

	return ret;
}

static void _signal_all_wait_clients(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence *hw_fence, u64 hash, int error)
{
	enum hw_fence_client_id wait_client_id;
	struct msm_hw_fence_client *hw_fence_wait_client;

	/* signal with an error all the waiting clients for this fence */
	for (wait_client_id = 0; wait_client_id < HW_FENCE_CLIENT_MAX; wait_client_id++) {
		if (hw_fence->wait_client_mask & BIT(wait_client_id)) {
			hw_fence_wait_client = drv_data->clients[wait_client_id];

			if (hw_fence_wait_client)
				_fence_ctl_signal(drv_data, hw_fence_wait_client, hw_fence,
					hash, 0, error);
		}
	}
}

int hw_fence_utils_cleanup_fence(struct hw_fence_driver_data *drv_data,
	struct msm_hw_fence_client *hw_fence_client, struct msm_hw_fence *hw_fence, u64 hash,
	u32 reset_flags)
{
	int ret = 0;
	int error = (reset_flags & MSM_HW_FENCE_RESET_WITHOUT_ERROR) ? 0 : MSM_HW_FENCE_ERROR_RESET;

	GLOBAL_ATOMIC_STORE(&hw_fence->lock, 1); /* lock */
	if (hw_fence->wait_client_mask & BIT(hw_fence_client->client_id)) {
		HWFNC_DBG_H("clearing client:%d wait bit for fence: ctx:%d seqno:%d\n",
			hw_fence_client->client_id, hw_fence->ctx_id,
			hw_fence->seq_id);
		hw_fence->wait_client_mask &= ~BIT(hw_fence_client->client_id);

		/* update memory for the table update */
		wmb();
	}
	GLOBAL_ATOMIC_STORE(&hw_fence->lock, 0); /* unlock */

	if (hw_fence->fence_allocator == hw_fence_client->client_id) {

		/* if fence is not signaled, signal with error all the waiting clients */
		if (!(hw_fence->flags & MSM_HW_FENCE_FLAG_SIGNAL))
			_signal_all_wait_clients(drv_data, hw_fence, hash, error);

		if (reset_flags & MSM_HW_FENCE_RESET_WITHOUT_DESTROY)
			goto skip_destroy;

		ret = hw_fence_destroy(drv_data, hw_fence_client,
			hw_fence->ctx_id, hw_fence->seq_id);
		if (ret) {
			HWFNC_ERR("Error destroying HW fence: ctx:%d seqno:%d\n",
				hw_fence->ctx_id, hw_fence->seq_id);
		}
	}

skip_destroy:
	return ret;
}
