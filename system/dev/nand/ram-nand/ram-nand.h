// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <threads.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/nand.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <lib/zx/vmo.h>
#include <sync/completion.h>
#include <zircon/device/ram-nand.h>
#include <zircon/listnode.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

// Wrapper for nand_info_t. It simplifies initialization of NandDevice.
struct NandParams : public nand_info_t {
    NandParams() : NandParams(0, 0, 0, 0) {}

    NandParams(uint32_t page_size, uint32_t pages_per_block, uint32_t num_blocks, uint32_t ecc_bits)
        : NandParams(nand_info_t {page_size, pages_per_block, num_blocks, ecc_bits}) {}

    NandParams(const nand_info_t& base) {
        // NandParams has no data members.
        *this = *reinterpret_cast<const NandParams*>(&base);
    }

    uint64_t GetSize() const {
        return static_cast<uint64_t>(page_size) * pages_per_block * num_blocks;
    }
};

// Callback to remove a NandDevice (from the Device Manager). Basically an
// embedder provided pointer to device_remove().
typedef void (*RemoveCallback)(zx_device_t* device);

// Provides the bulk of the functionality for a ram-backed NAND device.
class NandDevice {
  public:
    explicit NandDevice(const NandParams& params);
    ~NandDevice();

    // Performs the object initialization, returning the required data to create
    // an actual device (to call device_add()). The provided callback will be
    // called when this device must be removed from the system.
    zx_status_t Init(RemoveCallback remove_callback, device_add_args_t* device_args);

    // Stores the device pointer that represents this object (as returned by
    // device_add()).
    void SetDevice(zx_device_t* device) { zx_device_ = device; }

    // Device protocol implementation.
    uint64_t GetSize() const { return params_.GetSize(); }
    void Unbind();
    zx_status_t IoCtl(uint32_t op, const void* in_buf, size_t in_len,
                      void* out_buf, size_t out_len, size_t* out_actual);

    // NAND protocol implementation.
    void Query(nand_info_t* info_out, size_t* nand_op_size_out);
    void Queue(nand_op_t* operation);
    void GetBadBlockList(uint32_t* bad_blocks, uint32_t bad_block_len,
                         uint32_t* num_bad_blocks);

  private:
    void Kill();
    bool AddToList(nand_op_t* operation);
    bool RemoveFromList(nand_op_t** operation);
    int WorkerThread();
    static int WorkerThreadStub(void* arg);

    zx_device_t* zx_device_ = nullptr;
    RemoveCallback remove_callback_ = nullptr;
    uintptr_t mapped_addr_ = 0;
    zx::vmo vmo_;

    NandParams params_;

    fbl::Mutex lock_;
    list_node_t txn_list_ TA_GUARDED(lock_) = {};
    bool dead_ TA_GUARDED(lock_) = false;

    bool thread_created_ = false;

    completion_t wake_signal_;
    thrd_t worker_;
    char name_[NAME_MAX];

    DISALLOW_COPY_ASSIGN_AND_MOVE(NandDevice);
};