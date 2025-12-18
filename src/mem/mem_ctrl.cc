/*
 * Copyright (c) 2010-2020 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2013 Amin Farmahini-Farahani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/mem_ctrl.hh"

#include "base/trace.hh"
#include "debug/DRAM.hh"
#include "debug/Drain.hh"
#include "debug/MemCtrl.hh"
#include "debug/NVM.hh"
#include "debug/QOS.hh"
#include "mem/dram_interface.hh"
#include "mem/mem_interface.hh"
#include "mem/nvm_interface.hh"
#include "sim/system.hh"
#include "sim/serialize.hh"
#include "base/types.hh"

#include <random>
#include <chrono>

#define ALIGN(x) (((x + 4095) >> 12) << 12)
namespace gem5
{

    unsigned long long counter = 0;

    unsigned long long access_cnt = 0;

    bool isAddressCovered(uintptr_t start_addr, size_t pkt_size, int type) {
        // uintptr_t target_addr = 0x131898;
        // pkt_size = 4096;
        // start_addr = (start_addr >> 12) << 12;
        // return (target_addr >= start_addr) && (target_addr < start_addr + pkt_size);
        // if (type == 0) {
        //     return true;
        // } else {
        //     return false;
        // }
        return false;
        // return true;

        // if (access_cnt < 700000000) {
        //     return false;
        // } else if (access_cnt < 1350000000) {
        //     // return true;
        //     uintptr_t target_addr = 0x2183020;
        //     pkt_size = 4096;
        //     start_addr = (start_addr >> 12) << 12;
        //     return (target_addr >= start_addr) && (target_addr < start_addr + pkt_size);
        // } else {
        //     exit(1);
        // }
    }

    bool coverageTestMC(Addr start_addr, Addr target_addr, size_t pkt_size) {
        // target_addr = 0x25cc000;
        // return (target_addr >= start_addr) && (target_addr < start_addr + pkt_size);
        return false;
    }

    bool onePercentChance() {
        static std::mt19937 rng(std::random_device{}());
        static std::uniform_int_distribution<int> dist(1, 100);
        return dist(rng) <= 1;
    }

    bool fakeOnePercentChance() {
        if (counter == 20) {
            counter = 0;
            return true;
        }
        counter++;
        return false;
    }


namespace memory
{

MemCtrl::MemCtrl(const MemCtrlParams &p) :
    qos::MemCtrl(p),
    operationMode(p.operation_mode),
    recencyListSize(p.recency_list_size),
    port(name() + ".port", *this), isTimingMode(false),
    retryRdReq(false), retryWrReq(false),
    nextReqEvent([this] {processNextReqEvent(dram, respQueue,
                         respondEvent, nextReqEvent, retryWrReq);}, name()),
    respondEvent([this] {processRespondEvent(dram, respQueue,
                         respondEvent, retryRdReq); }, name()),
    dram(p.dram),
    globalPredictor(0), mcache(MetaCache(32768)),
    curReadNum(0), curWriteNum(0), blockedNum(0),
    readBufferSizeForCompr(dram->readBufferSize),
    writeBufferSizeForCompr(dram->writeBufferSize),
    blockedForCompr(false),
    typeOneBlock(0),
    typeTwoBlock(0),
    recordForCheckReady(false),
    blockedForDyL(false),
    functionalBlockedForDyL(false),
    blockedNumForDyL(0),
    pktInProcess(0),
    accessCnt(0),
    potentialRecycle(0),
    startAddrForCTE(0),
    startAddrForPreGather(0),
    startAddrForCnt(0),
    pageBufferForDyL(std::vector<uint8_t>(64)),
    decompress_latency(277000), compress_latency(662000),
    expectReadQueueSize(0),
    expectWriteQueueSize(0),
    blockedForNew(false),
    lastRecomprTick(0),
    recomprInterval(10000000),
    readBufferSizeForNew(dram->readBufferSize),
    writeBufferSizeForNew(dram->writeBufferSize),
    blockedForSecure(false),
    pendingPktForSecure(nullptr),
    blockedNumForSecure(0),
    lastRecordTick(0), passedInterval(0),
    recordInterval(0),
    stat_used_bytes(0), stat_max_used_bytes(0),
    readBufferSize(dram->readBufferSize),
    writeBufferSize(dram->writeBufferSize),
    writeHighThreshold(writeBufferSize * p.write_high_thresh_perc / 100.0),
    writeLowThreshold(writeBufferSize * p.write_low_thresh_perc / 100.0),
    minWritesPerSwitch(p.min_writes_per_switch),
    minReadsPerSwitch(p.min_reads_per_switch),
    memSchedPolicy(p.mem_sched_policy),
    frontendLatency(p.static_frontend_latency),
    backendLatency(p.static_backend_latency),
    commandWindow(p.command_window),
    prevArrival(0),
    stats(*this)
{
    DPRINTF(MemCtrl, "Setting up controller\n");

    readQueue.resize(p.qos_priorities);
    writeQueue.resize(p.qos_priorities);

    dram->setCtrl(this, commandWindow);

    recordInterval = static_cast<uint64_t>(p.tick_interval) * 10000000LL;

    printf("the record interval is setting to %lld\n", recordInterval);

    // perform a basic check of the write thresholds
    if (p.write_low_thresh_perc >= p.write_high_thresh_perc)
        fatal("Write buffer low threshold %d must be smaller than the "
              "high threshold %d\n", p.write_low_thresh_perc,
              p.write_high_thresh_perc);
    if (p.disable_sanity_check) {
        port.disableSanityCheck();
    }
}

void
MemCtrl::init()
{
    if (!port.isConnected()) {
        fatal("MemCtrl %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }

    if (operationMode == "normal") {
        printf("enter the normal mode\n");
    } else if (operationMode == "compresso") {
        printf("enter the compresso mode\n");
    } else if (operationMode == "DyLeCT") {
        printf("enter the DyLeCT mode\n");
    } else if (operationMode == "new") {
        printf("enter the new mode\n");
    } else if (operationMode == "secure") {
        printf("enter the secure mode\n");
    } else {
        panic("unknown mode for memory controller");
    }

    /* initialize the state for memory compression */

    /* get the size of OSPA space */
    uint64_t OSPACapacity = 1ULL << ceilLog2(dram->AbstractMemory::size());
    uint32_t numPages = OSPACapacity / (4 * 1024);

    if (operationMode == "compresso") {
        /* calculate the real start address */
        pageBufferAddr = ALIGN(64 * numPages); // 64B metadata per OSPA page (4KB)

        realStartAddr = pageBufferAddr + 4096;

        /* push the available free chunks into the freeList */
        uint64_t dramCapacity = (dram->capacity() * (1024 * 1024));
        assert(realStartAddr < dramCapacity);
        for (Addr addr = realStartAddr; (addr + 512) <= dramCapacity; addr += 512) {
            freeList.emplace_back(addr);
        }
        sizeMap = {0, 8, 32, 64};
        hasBuffered = false;
    } else if (operationMode == "DyLeCT") {
        startAddrForPreGather = 0;
        startAddrForCTE = ALIGN(numPages * 2 / 8);
        startAddrForCnt = ALIGN(startAddrForCTE + (numPages * 8));
        realStartAddr = ALIGN(startAddrForCnt + numPages);

        printf("realStartAddr: 0x%lx\n", realStartAddr);
        printf("dram capacity: %d\n", dram->capacity());
        printf("dram->abstractMemory::size(): %d\n", dram->AbstractMemory::size());

        uint64_t OSPACapacity = 1ULL << ceilLog2(dram->AbstractMemory::size());
        printf("OSPACapacity: %ld\n", OSPACapacity);

        uint64_t dramCapacity = (dram->capacity() * (1024 * 1024));

        Addr addr = realStartAddr;
        for (; addr < OSPACapacity; addr += 4096) {
            freeList.emplace_back(addr);
        }
        printf("last addr: 0x%lx\n", addr - 4096);
        fflush(stdout);
        std::vector<uint8_t> to_test(1);
        dram->atomicRead(to_test.data(), addr, 1);


        freeListThreshold = (16 * 1024) / 4;  // set the threshold to be 16 MiB  <=>  the freelist should at least be (16 * 1024) / 4 length
        recencyListThreshold = recencyListSize;
        memoryUsageThreshold = recencyListSize * 100;

        
        printf("Initial: the size of freeList is %ld\n", freeList.size());
        printf("Initial: the threshold of recency list is %lld\n", recencyListThreshold);
        printf("Initial: the threshold of memory usage is %lld\n", memoryUsageThreshold);

    } else if (operationMode == "new") {
        zeroAddr = 64 * numPages;
        realStartAddr = ((zeroAddr + 64 + 1023) >> 10) << 10;
        uint64_t dramCapacity = (dram->capacity() * (1024 * 1024));
        assert(realStartAddr < dramCapacity);
        for (Addr addr = realStartAddr; (addr + 1024) <= dramCapacity; addr += 1024) {
            freeList.emplace_back(addr);
        }
        sizeMap = {1, 22, 44, 64};
        pageSizeMap = {0, 512, 1024, 2048, 3072, 4096, 4608, 5120, 6144, 7168};

        originMetaData.resize(64);
        memset(originMetaData.data(), 0, 64);
        originMetaData[0] = 0x80;
        originMetaData[1] = 1;
    } else if (operationMode == "secure") {
        startAddrForSecureMetaData = 0;
        Addr realStartAddr = ALIGN(startAddrForSecureMetaData + numPages * 8);
        uint64_t dramCapacity = ALIGN(dram->capacity() * (1024 * 1024) - 4095);
        printf("=========\n\nthe realStartAddr is %d\n\n", realStartAddr);
        
        /* initially, all space is divided by large chunks */
        for (uint64_t addr = realStartAddr; addr < (dramCapacity - 4096); addr += 4096) {
            largeChunkList.emplace_back(addr);
        }
        blockedQueueForSecure.clear();
    }
}

void
MemCtrl::startup()
{
    // remember the memory system mode of operation
    isTimingMode = system()->isTimingMode();

    if (isTimingMode) {
        // shift the bus busy time sufficiently far ahead that we never
        // have to worry about negative values when computing the time for
        // the next request, this will add an insignificant bubble at the
        // start of simulation
        dram->nextBurstAt = curTick() + dram->commandOffset();
    }
}

void
MemCtrl::serialize(gem5::CheckpointOut &cp) const {
    if(operationMode == "secure"){
        printf("when serialize \n");
        printf("the size of largeChunkList is %d\n", largeChunkList.size());
        printf("the size of smallChunkList is %d\n", smallChunkList.size());
        SERIALIZE_CONTAINER(largeChunkList);
        SERIALIZE_CONTAINER(smallChunkList);
    } else {
        SERIALIZE_CONTAINER(freeList);
    }

    // printf("when serialize, the pageNum is %ld\n", pageNum);
    SERIALIZE_SCALAR(pageNum);
    SERIALIZE_SCALAR(hasBuffered);
    SERIALIZE_SCALAR(stat_used_bytes);
    SERIALIZE_SCALAR(passedInterval);
    SERIALIZE_SCALAR(lastRecordTick);
}

void
MemCtrl::unserialize(gem5::CheckpointIn &cp) {

    if(operationMode == "secure"){
        UNSERIALIZE_CONTAINER(largeChunkList);
        UNSERIALIZE_CONTAINER(smallChunkList);
        printf("after unserialize \n");
        printf("the size of largeChunkList is %d\n", largeChunkList.size());
        printf("the size of smallChunkList is %d\n", smallChunkList.size());
    } else {
        UNSERIALIZE_CONTAINER(freeList);
    }

    UNSERIALIZE_SCALAR(pageNum);
    UNSERIALIZE_SCALAR(hasBuffered);
    UNSERIALIZE_SCALAR(stat_used_bytes);
    printf("when unserialize, the stat used bytes are %ld\n", stat_used_bytes);
    UNSERIALIZE_SCALAR(passedInterval);
    UNSERIALIZE_SCALAR(lastRecordTick);
}

void
MemCtrl::recordMemConsumption() {
    if (passedInterval >= 10) {
        return;
    }

    if (curTick() - lastRecordTick < recordInterval) {
        return;
    }

    lastRecordTick = curTick();

    if (operationMode == "normal") {
        stats.usedMemoryByte[passedInterval] = stat_page_used.size() * 4096;
    } else {
        stats.usedMemoryByte[passedInterval] = stat_used_bytes;
    }

    passedInterval++;
}

Tick
MemCtrl::recvAtomic(PacketPtr pkt)
{
    if (!dram->getAddrRange().contains(pkt->getAddr())) {
        panic("Can't handle address range for packet %s\n", pkt->print());
    }

    recordMemConsumption();
    // access_cnt += 1;

    Tick res = 0;
    if (operationMode == "normal") {
        res = recvAtomicLogic(pkt, dram);
    } else if (operationMode == "compresso") {
        res = recvAtomicLogicForCompr(pkt, dram);
    } else if (operationMode == "DyLeCT") {
        res = recvAtomicLogicForDyL(pkt, dram);
    } else if (operationMode == "new") {
        res = recvAtomicLogicForNew(pkt, dram);
    } else if (operationMode == "secure") {
        res = recvAtomicLogicForSecure(pkt, dram);
    } else {
        panic("unknown operation mode for Memory Controller");
    }

    return res;
}

Tick
MemCtrl::recvAtomicLogic(PacketPtr pkt, MemInterface* mem_intr)
{
    DPRINTF(MemCtrl, "recvAtomic: %s 0x%x\n",
                     pkt->cmdString(), pkt->getAddr());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
        printf("recv Atomic: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
    }

    PPN page_num = (pkt->getAddr() >> 12);
    stat_page_used.insert(page_num);

    // do the actual memory access and turn the packet into a response
    mem_intr->access(pkt, access_cnt);

    if (pkt->hasData()) {
        // this value is not supposed to be accurate, just enough to
        // keep things going, mimic a closed page
        // also this latency can't be 0
        return mem_intr->accessLatency();
    }

    return 0;
}

Tick
MemCtrl::recvAtomicLogicForCompr(PacketPtr pkt, MemInterface* mem_intr) {
    DPRINTF(MemCtrl, "recvAtomic: %s 0x%x\n",
        pkt->cmdString(), pkt->getAddr());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
                "is responding");

    /* step 0: create an auxiliary packet for processing the pkt */
    PacketPtr auxPkt = new Packet(pkt);
    auxPkt->allocateForMC();
    memcpy(auxPkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

    unsigned size = pkt->getSize();
    uint32_t burst_size = mem_intr->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    // DPRINTF(MemCtrl, "Line %d: finish step 0\n", __LINE__);

    /* Step 1: atomic read metadata from memory or mcache */
    Addr base_addr = auxPkt->getAddr();
    Addr addr = base_addr;

    for (unsigned int i = 0; i < pkt_count; i++) {
        // DPRINTF(MemCtrl, "Line %d: cur addr: %lld\n", __LINE__, addr);
        PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

        if (auxPkt->comprMetaDataMap.find(ppn) == auxPkt->comprMetaDataMap.end()) {
            /* step 1.1: calculate the MPA for metadata */
            Addr memory_addr = ppn * 64;
            std::vector<uint8_t> metaData(64, 0);
            if (mcache.isExist(memory_addr)) {
                metaData = mcache.find(memory_addr);
            } else {
                mem_intr->atomicRead(metaData.data(), memory_addr, 64);
            }
            DPRINTF(MemCtrl, "Line %d: finish reading the metaData\n", __LINE__);

            /* step 1.2 determine if it is an unallocated page */
            if (!isValidMetaData(metaData)) {
                DPRINTF(MemCtrl, "Line %d: it is a unallocated page\n", __LINE__);
                /* make room in the pagebuffer for this new page */
                if (!hasBuffered) {
                    initialPageBuffer(ppn);
                }
                /*
                    if the pageBuffer already have data of another page,
                        write back to memory, update the metadata
                */
                if (pageNum != ppn) {
                    DPRINTF(MemCtrl, "Line %d: we have to flush the original data in page buffer to memory\n", __LINE__);
                    std::vector<uint8_t> compressedPage;
                    std::vector<uint8_t> mPageBuffer(64, 0);
                    mem_intr->atomicRead(mPageBuffer.data(), pageNum * 64, 64);

                    for (uint8_t u = 0; u < 64; u++) {
                        uint8_t type = getType(mPageBuffer, u);
                        uint8_t dataLen = sizeMap[type];
                        std::vector<uint8_t> compressedCL(dataLen, 0);
                        mem_intr->atomicRead(compressedCL.data(), pageBufferAddr + 64 * u, dataLen);
                        for (uint8_t v = 0; v < dataLen; v++) {
                            compressedPage.emplace_back(compressedCL[v]);
                        }
                    }

                    uint64_t cur = 0;
                    while (cur < compressedPage.size()) {
                        assert(freeList.size() > 0);
                        Addr chunkAddr = freeList.front();
                        DPRINTF(MemCtrl, "Line %d, freeList pop\n", __LINE__);
                        freeList.pop_front();

                        int chunkIdx = cur / 512;
                        uint64_t MPFN = (chunkAddr >> 9);
                        for (int u = 3; u >= 0; u--) {   // 4B per chunk
                            uint8_t val = MPFN & 0xFF;
                            MPFN >>= 8;
                            mPageBuffer[2 + chunkIdx * 4 + u] = val;
                        }
                        if (cur + 512 < compressedPage.size()) {
                            mem_intr->atomicWrite(compressedPage, chunkAddr, 512, cur);
                            cur += 512;
                        } else {
                            mem_intr->atomicWrite(compressedPage, chunkAddr, compressedPage.size() - cur, cur);
                            break;
                        }
                    }
                    // store the size of compressedPage into the control block (using 12 bit)
                    uint64_t compressedSize = compressedPage.size();

                    mPageBuffer[1] = compressedSize & (0xFF);
                    mPageBuffer[0] = mPageBuffer[0] | ((compressedSize >> 8) & 0xF);

                    /* write the mPageBuffer to mcache and memory */
                    Addr mPageBufferAddr = pageNum * 64;
                    // printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, pageNum, mPageBufferAddr);
                    mcache.add(mPageBufferAddr, mPageBuffer);
                    mem_intr->atomicWrite(mPageBuffer, mPageBufferAddr, 64, 0);
                    if (auxPkt->comprMetaDataMap.find(pageNum) != auxPkt->comprMetaDataMap.end()) {
                        auxPkt->comprMetaDataMap[pageNum] = mPageBuffer;
                    }

                    initialPageBuffer(ppn);
                }

                /* now the page buffer is ready to serve the incoming write request */
            }
            /* step 1.2: update the pkt's metaDataMap */
            if (ppn == pageNum) {
                // auxPkt->comprMetaDataMap[ppn] = mPageBuffer;
                mem_intr->atomicRead(metaData.data(), ppn * 64, 64);
                auxPkt->comprMetaDataMap[ppn] = metaData;
            } else {
                auxPkt->comprMetaDataMap[ppn] = metaData;
            }
        }
        // Starting address of next memory pkt (aligned to burst boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }
    DPRINTF(MemCtrl, "Line %d: finish step 1\n", __LINE__);

    /* step 2: process the pkt based on isWrite or isRead*/

    /* step 2.1: if the pkt is write */
    DPRINTF(MemCtrl, "Line %d: actually process the pkt \n", __LINE__);
    if (auxPkt->isWrite()) {

        Addr addrAligned = (base_addr >> 6) << 6;
        uint64_t new_size = ((((base_addr + size) + (burst_size - 1)) >> 6) << 6) - addrAligned;

        assert(burst_size == 64);
        assert(new_size == (addr - addrAligned));

        if (auxPkt->cmd == MemCmd::SwapReq) {
            panic("not support yet");
            // DPRINTF(MemCtrl, "Line %d: req's cmd is swapReq \n", __LINE__);
            // /* step 2.1.1 assert(pkt.size <= 64 && pkt is not cross the boundary)*/
            // assert(size <= 64 && (burst_size - (base_addr | (burst_size - 1)) <= size));

            // PPN ppn = (base_addr >> 12 & ((1ULL << 52) - 1));
            // std::vector<uint8_t> cacheLine(64, 0);

            // assert(auxPkt->comprMetaDataMap.find(ppn) != auxPkt->comprMetaDataMap.end());
            // std::vector<uint8_t> metaData = auxPkt->comprMetaDataMap[ppn];
            // uint8_t cacheLineIdx = (base_addr >> 6) & 0x3F;
            // uint8_t type = getType(metaData, cacheLineIdx);
            // bool inInflate = false;
            // Addr real_addr = 0;

            // if (ppn == pageNum) {
            //     type = getType(mPageBuffer, cacheLineIdx);
            //     for (unsigned int u = 0; u < sizeMap[type]; u++) {
            //         cacheLine[u] = pageBuffer[cacheLineIdx * 64 + u];
            //     }
            //     restoreData(cacheLine, type);
            // } else {
            //     std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);
            //     inInflate = cLStatus.first;
            //     real_addr = cLStatus.second;

            //     if (inInflate) {
            //         type = 0b11;
            //     }

            //     if (type != 0) {
            //         /* step 2.1.2 read the cacheLine from memory */
            //         mem_intr->atomicRead(cacheLine.data(), real_addr, sizeMap[type]);
            //     }

            //     /* step 2.1.3 decompress */
            //     restoreData(cacheLine, type);
            // }

            // /* step 2.1.4 the same as before, host_addr = cacheLine.data() + ofs */
            // assert(cacheLine.size() == burst_size);
            // uint8_t* uPtr = cacheLine.data() + offset;
            // if (pkt->isAtomicOp()) {
            //     if (mem_intr->hasValidHostMem()) {
            //         pkt->setData(uPtr);
            //         (*(pkt->getAtomicOp()))(uPtr);
            //     }
            // } else {
            //     std::vector<uint8_t> overwrite_val(pkt->getSize());
            //     uint64_t condition_val64;
            //     uint32_t condition_val32;

            //     panic_if(!mem_intr->hasValidHostMem(), "Swap only works if there is real memory " \
            //             "(i.e. null=False)");

            //     bool overwrite_mem = true;
            //     // keep a copy of our possible write value, and copy what is at the
            //     // memory address into the packet
            //     pkt->writeData(&overwrite_val[0]);
            //     pkt->setData(uPtr);

            //     if (pkt->req->isCondSwap()) {
            //         if (pkt->getSize() == sizeof(uint64_t)) {
            //             assert(uPtr == cacheLine.data());
            //             condition_val64 = pkt->req->getExtraData();
            //             overwrite_mem = !std::memcmp(&condition_val64, uPtr,
            //                                         sizeof(uint64_t));
            //         } else if (pkt->getSize() == sizeof(uint32_t)) {
            //             condition_val32 = (uint32_t)pkt->req->getExtraData();
            //             overwrite_mem = !std::memcmp(&condition_val32, uPtr,
            //                                         sizeof(uint32_t));
            //         } else
            //             panic("Invalid size for conditional read/write\n");
            //     }

            //     if (overwrite_mem) {
            //         std::memcpy(uPtr, &overwrite_val[0], pkt->getSize());
            //     }
            // }

            // /*step 2.1.5 recompress the cacheline */
            // std::vector<uint8_t> compressed = compressForCompr(cacheLine);

            // if (ppn == pageNum) {
            //     /*write back to pageBuffer and update the mPageBuffer if necessary */
            //     if (isAllZero(cacheLine)) {
            //         /* set the mPageBuffer entry to be 0 */
            //         setType(mPageBuffer, cacheLineIdx, 0);
            //     } else {
            //         if (compressed.size() <= 8) {
            //             /* set the mPageBuffer entry to be 0b1*/
            //             setType(mPageBuffer, cacheLineIdx, 0b01);
            //         } else if (compressed.size() <= 32) {
            //             setType(mPageBuffer, cacheLineIdx, 0b10);
            //         } else {
            //             /* set to be 0b11 */
            //             setType(mPageBuffer, cacheLineIdx, 0b11);
            //         }
            //     }
            //     auxPkt->comprMetaDataMap[ppn] = mPageBuffer;
            // } else {
            //     /* step 2.1.6 deal with potential overflow/underflow */
            //     updateMetaData(compressed, metaData, cacheLineIdx, inInflate, mem_intr);
            //     auxPkt->comprMetaDataMap[ppn] = metaData;
            //     Addr metadata_addr = ppn * 64;
            //     mcache.add(metadata_addr, metaData);
            //     mem_intr->atomicWrite(metaData, metadata_addr, 64, 0);
            // }

            // assert(new_size == 64);
            // auxPkt->setAddr(addrAligned);
            // auxPkt->setSizeForMC(new_size);
            // auxPkt->allocateForMC();
            // auxPkt->setDataForMC(cacheLine.data(), 0, new_size);
        } else {
            addr = base_addr;
            std::vector<uint8_t> newData(new_size, 0);

            DPRINTF(MemCtrl, "Line %d: start process the pkt, the pkt size is %lld, the pkt's address is 0x%llx \n", __LINE__, size, base_addr);
            /* process the pkt one by one */
            for (unsigned int i = 0; i < pkt_count; i++) {
                PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

                Addr mAddr = ppn * 64;
                uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

                assert(auxPkt->comprMetaDataMap.find(ppn) != auxPkt->comprMetaDataMap.end());
                std::vector<uint8_t> metaData = auxPkt->comprMetaDataMap[ppn];

                std::vector<uint8_t> cacheLine(64, 0);

                // printf("the pageNum is %d, the metadata is :\n", ppn);
                // for (int k = 0; k < 64; k++) {
                //     printf("%02x",static_cast<unsigned>(metaData[k]));

                // }
                // printf("\n");


                if (pageNum == ppn) {
                    DPRINTF(MemCtrl, "Line %d: the pageNum == ppn \n", __LINE__);
                    uint8_t type = getType(metaData, cacheLineIdx);

                    // for (unsigned int j = 0; j < sizeMap[type]; j++) {
                    //     cacheLine[j] = pageBuffer[64 * cacheLineIdx + j];
                    // }
                    mem_intr->atomicRead(cacheLine.data(), pageBufferAddr + 64 * cacheLineIdx, sizeMap[type]);

                    restoreData(cacheLine, type);
                    assert(cacheLine.size() == 64);

                    /* write the data */
                    uint64_t ofs = addr - base_addr;
                    uint8_t loc = addr & 0x3F;
                    size_t writeSize = std::min(64UL - loc, size - ofs);

                    auxPkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

                    std::vector<uint8_t> compressed = compressForCompr(cacheLine);

                    DPRINTF(MemCtrl, "Line %d: finish recompress the data \n", __LINE__);

                    /* update the metadata if necessary */
                    if (isAllZero(cacheLine)) {
                        /* set the metadata entry to be 0 */
                        setType(metaData, cacheLineIdx, 0);
                    } else {
                        if (compressed.size() <= 8) {
                            /* set the metadata entry to be 0b1*/
                            setType(metaData, cacheLineIdx, 0b01);
                        } else if (compressed.size() <= 32) {
                            setType(metaData, cacheLineIdx, 0b10);
                        } else {
                            /* set to be 0b11 */
                            setType(metaData, cacheLineIdx, 0b11);
                        }
                    }
                    auxPkt->comprMetaDataMap[ppn] = metaData;
                    mem_intr->atomicWrite(metaData, ppn * 64, 64);

                } else {
                    DPRINTF(MemCtrl, "Line %d: need to modify the memory \n", __LINE__);

                    uint8_t type = getType(metaData, cacheLineIdx);
                    std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);
                    bool inInflate = cLStatus.first;
                    Addr real_addr = cLStatus.second;
                    // printf("is in inflate: %d\n", inInflate);

                    DPRINTF(MemCtrl, "Line %d: the type is %d \n", __LINE__, static_cast<unsigned int>(type));
                    DPRINTF(MemCtrl, "Line %d: the real address is 0x%llx \n", __LINE__, real_addr);

                    if (inInflate) {
                        mem_intr->atomicRead(cacheLine.data(), real_addr, 64);
                        type = 0b11;
                    } else {
                        if (type != 0) {
                            mem_intr->atomicRead(cacheLine.data(), real_addr, sizeMap[type]);
                        }
                    }

                    DPRINTF(MemCtrl, "Line %d: finish read the cacheline from memory \n", __LINE__);

                    // for (int u = 0; u < 64; u++) {
                    //     if (u % 8 == 0) {
                    //         printf("\n");
                    //     }
                    //     printf("0x%02x, ", static_cast<uint8_t>(cacheLine[u]));
                    // }
                    // printf("\n");

                    restoreData(cacheLine, type);

                    DPRINTF(MemCtrl, "Line %d: finish restore the cacheline \n", __LINE__);
                    // printf("the restore the data is :\n");
                    // for (int as = 0; as < cacheLine.size(); as++) {
                    //     if (as % 8 == 0) {
                    //         printf("\n");
                    //     }
                    //     printf("%02x, ", static_cast<unsigned int>(cacheLine[as]));
                    // }
                    // printf("\n");

                    /* write the data */
                    uint64_t ofs = addr - base_addr;
                    uint8_t loc = addr & 0x3F;
                    size_t writeSize = std::min(64UL - loc, size - ofs);
                    DPRINTF(MemCtrl, "Line %d: start to write dat, the ofs is %lld, the loc is %d, the writeSize is %ld\n", __LINE__, ofs, static_cast<unsigned int>(loc), writeSize);
                    auxPkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

                    DPRINTF(MemCtrl, "Line %d: finish write the data \n", __LINE__);

                    // printf("after write the data is :\n");
                    // for (int as = 0; as < cacheLine.size(); as++) {
                    //     if (as % 8 == 0) {
                    //         printf("\n");
                    //     }
                    //     printf("%02x, ", static_cast<unsigned int>(cacheLine[as]));
                    // }
                    // printf("\n");


                    std::vector<uint8_t> compressed = compressForCompr(cacheLine);
                    if (compressed.size() > 32) {
                        assert(compressed.size() == 64);
                    }

                    DPRINTF(MemCtrl, "Line %d: finish compress, the size of compressed is %d\n", __LINE__, compressed.size());
                    // for (int as = 0; as < compressed.size(); as++) {
                    //     if (as % 8 == 0) {
                    //         printf("\n");
                    //     }
                    //     printf("%02x, ", static_cast<unsigned int>(compressed[as]));
                    // }
                    // printf("\n");

                    /* deal with potential overflow/underflow */
                    bool success = updateMetaData(compressed, metaData, cacheLineIdx, inInflate, mem_intr);

                    if (!success) {
                        recompressAtomic(cacheLine, ppn, cacheLineIdx, metaData, mem_intr);
                        /* update the metadataSet in auxPkt */
                        assert(mcache.isExist(ppn * 64));
                        auxPkt->comprMetaDataMap[ppn] = mcache.find(ppn * 64);
                    } else {
                        DPRINTF(MemCtrl, "Line %d: finish update the metadata \n", __LINE__);

                        auxPkt->comprMetaDataMap[ppn] = metaData;
                        Addr metadata_addr = ppn * 64;
                        mcache.add(metadata_addr, metaData);
                        mem_intr->atomicWrite(metaData, metadata_addr, 64, 0);
                    }
                }

                for (unsigned int j = 0; j < 64; j++) {
                    newData[i * 64 + j] = cacheLine[j];
                }

                // Starting address of next memory pkt (aligned to burst boundary)
                addr = (addr | (burst_size - 1)) + 1;
            }

            assert(new_size % 64 == 0);
            auxPkt->setAddr(addrAligned);
            auxPkt->setSizeForMC(new_size);
            auxPkt->allocateForMC();
            auxPkt->setDataForMC(newData.data(), 0, new_size);
        }
    } else {
        /* step 2.2: if the pkt is read, access the auxPkt directly */
        assert(pkt->isRead());
    }
    // do the actual memory access and turn the packet into a response
    // mem_intr->access(pkt);
    assert(burst_size == 64);
    // mem_intr->accessForCompr(auxPkt, burst_size, pageNum, pageBuffer, mPageBuffer);
    mem_intr->accessForCompr(auxPkt, burst_size, pageNum, pageBufferAddr);

    if (pkt->isRead()) {
        assert(auxPkt->getSize() == pkt->getSize());
        memcpy(pkt->getPtr<uint8_t>(), auxPkt->getPtr<uint8_t>(), pkt->getSize());
    }

    delete auxPkt;

    if (pkt->hasData()) {
        // DPRINTF(MemCtrl, "pkt has data\n");
        // this value is not supposed to be accurate, just enough to
        // keep things going, mimic a closed page
        // also this latency can't be 0
        return mem_intr->accessLatency();
    }
    return 0;
}

Tick
MemCtrl::recvAtomicLogicForDyL(PacketPtr pkt, MemInterface* mem_intr) {
    DPRINTF(MemCtrl, "recvAtomic: %s 0x%x\n",
        pkt->cmdString(), pkt->getAddr());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
    "is responding");

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)){
        printf("\n****************\n\n");
        printf("recv Atomic: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
    }

    /*
        stage 2:
            first attempt to read metadata from mcache
            if not hit:
                then read the metadata from memory
    */
    PPN ppn = ((pkt->getAddr() >> 12) & ((1ULL << 52) - 1));
    Addr cteAddr = startAddrForCTE + ppn * 8;
    Addr cteAddrAligned = (cteAddr >> 6) << 6;

    uint8_t loc = (cteAddr >> 3) & ((1 << 3) - 1);
    assert (loc < 8);

    pkt->DyLBackup = pkt->getAddr();

    // check if hit in the metadata cache. Read the CTE from cache or memory
    std::vector<uint8_t> cacheLine = mcache.find(cteAddrAligned);
    assert(cacheLine.size() == 64);
    uint64_t cte = 0;
    uint64_t cteCandi = 0;

    for (unsigned int i = loc * 8; i < (loc + 1) * 8; i++) {
        cteCandi = (cteCandi << 8) | cacheLine[i];
    }
    if ((cteCandi >> 63) != 0) { // cacheline hit and CTE is valid
        cte = cteCandi;
    } else {
        mem_intr->atomicRead(cacheLine.data(), cteAddrAligned, 64);
        for (int i = loc * 8; i < (loc + 1) * 8; i++) {
            cte = (cte << 8) | cacheLine[i];
        }
    }

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)){
        printf("cte is 0x%lx\n", cte);
    }

    // printf("atomic stage 2: cte is 0x%lx\n", cte);
    // fflush(stdout);

    // printf("finish stage 2\n");
    // fflush(stdout);

    /*
        stage 3: update the receny List
            if the page is marked incompressible, add it back to the recencyList with 1% (from TMCC p5)
    */

    auto it = recencyMap.find(ppn);
    if (it != recencyMap.end()) {
        recencyList.erase(it->second);
    }

    if (incompressiblePages.find(ppn) != incompressiblePages.end()) {
        // if(onePercentChance()) {
        // printf("the page is incompressible\n");
        // fflush(stdout);

        if (fakeOnePercentChance()) {
            // printf("enter one percent chance\n");
            // fflush(stdout);

            recencyList.push_front(ppn);
            recencyMap[ppn] = recencyList.begin();
            incompressiblePages.erase(ppn);
        }
    } else {

        // printf("the page is compressible\n");
        // fflush(stdout);

        recencyList.push_front(ppn);
        recencyMap[ppn] = recencyList.begin();
    }

    // printf("finish stage 3\n");
    // fflush(stdout);

    /*
        stage 4: interpret the CTE
            if the cte is invalid <=> the page is visited for the first time
                allocate the memory
            else if the page is compressed:
                decompress the page
            else:
                parse the cte for the dram address
    */
    Addr addr = 0;
    if (((cte >> 63) & 0x1) == 0) {   // the cte is invalid
        addr = freeList.front();
        freeList.pop_front();
        std::vector<uint8_t> zeroPage(4096, 0);
        mem_intr->atomicWrite(zeroPage, addr, 4096);
        stat_used_bytes += 4096;

        // update the CTE
        cte = (1ULL << 63) | (0ULL << 62) | (((addr >> 12) & ((1ULL << 30) - 1)) << 32);

        /* update the metacache entry */
        uint64_t cteVal = cte;
        for (int i = (loc * 8 + 7); i >= loc * 8; i--) {
            cacheLine[i] =  cteVal & ((1 << 8) - 1);
            cteVal = cteVal >> 8;
        }

    } else {
        if ((cte & (1ULL << 62)) == 0){   // the OS physical page is uncompressed
            addr = ((cte >> 32) & ((1ULL << 30) - 1)) << 12;
        } else {
            DPRINTF(MemCtrl, "Opps, we read/write physical page which is compressed\n");
            uint64_t compressedSize = ((cte >> 50) & ((1ULL << 12) - 1)) + 1;   // size range [1, 2kiB]

            assert (freeList.size() > 0);
            std::vector<uint8_t> pageBuffer(compressedSize, 0);
            Addr oldAddr = ((cte >> 10) & ((1ULL << 40) - 1)) << 8;

            addr = freeList.front();
            freeList.pop_front();
            stat_used_bytes += 4096;

            // printf("freelist front addr is 0x%lx\n", addr);
            // fflush(stdout);

            // read the data into the pageBuffer
            mem_intr->atomicRead(pageBuffer.data(), oldAddr, compressedSize);

            // Decompress and copy the data to the new page
            std::vector<uint8_t> decompressedPage = decompressPage(pageBuffer.data(), compressedSize);

            mem_intr->atomicWrite(decompressedPage, addr, decompressedPage.size());
            if (compressedSize <= 256) {
                smallFreeList.push_back(oldAddr);
                stat_used_bytes -= 256;
            } else if (compressedSize <= 1024) {
                moderateFreeList.push_back(oldAddr);
                stat_used_bytes -= 1024;
            } else {
                assert(compressedSize <= 2048);
                largeFreeList.push_back(oldAddr);
                stat_used_bytes -= 2048;
            }

           /* update the metacache entry */
            cte = (1ULL << 63) | (0ULL << 62) | (((addr >> 12) & ((1ULL << 30) - 1)) << 32);
            uint64_t cteVal = cte;
            for (int i = (loc * 8 + 7); i >= loc * 8; i--) {
                cacheLine[i] = cteVal & ((1 << 8) - 1);
                cteVal = cteVal >> 8;
            }

            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)){
                printf("the page is currently compressed\n");
                printf("after decompress, the new cte is 0x%lx\n", cte);
            }
        }
    }

    // update the metadata cache and update the CTE
    mcache.add(cteAddrAligned, cacheLine);

    if (coverageTestMC(cteAddrAligned, 0x198662, cacheLine.size())) {
        printf("Line %d: the addr is 0x%lx, size is %d\n", __LINE__, cteAddrAligned, cacheLine.size());
    }

    mem_intr->atomicWrite(cacheLine, cteAddrAligned, cacheLine.size());

    /*
        stage 5:
            try to compress the cold page if the current memory usage exceeds the threshold.
            will stop when the memory usage fall back or recency list becomes empty
    */

    while (stat_used_bytes > memoryUsageThreshold && recencyList.size() > 1) {
        PPN coldPageId = recencyList.back();
        recencyList.pop_back();
        recencyMap.erase(coldPageId);
        
        /* read the cold page from memory*/
        std::vector<uint8_t> pageForCompress(4096, 0);

        Addr coldCteAddr = startAddrForCTE + coldPageId * 8;
        Addr coldCteAddrAligned = (coldCteAddr >> 6) << 6;
        int8_t coldLoc = (coldCteAddr >> 3) & ((1 << 3) - 1);

        std::vector<uint8_t> cteCL(64, 0);
        mem_intr->atomicRead(cteCL.data(), coldCteAddrAligned, 64);

        uint64_t oldCTE = 0;
        for (unsigned int i = coldLoc * 8; i < (coldLoc + 1) * 8; i++) {
            oldCTE = (oldCTE << 8) | cteCL[i];
        }
        uint64_t pagePtr = ((oldCTE >> 32) & ((1ULL << 30) - 1)) << 12;
        uint64_t newCTE = 0;



        assert(((oldCTE >> 62) & 0x1) == 0);
        mem_intr->atomicRead(pageForCompress.data(), pagePtr, 4096);
        std::vector<uint8_t> compressedPage = compressPage(pageForCompress.data(), 4096);

        uint64_t cSize = compressedPage.size();

        if (isAddressCovered(coldPageId * 4096, pkt->getSize(), 1)){
            printf("the page is being compressed\n");
            printf("the cSize is %d\n", cSize);
            printf("the old cte is 0x%lx\n");
        }

        if (cSize > 2048) {
            incompressiblePages.emplace(coldPageId);
            continue;
        }

        freeList.push_back(pagePtr);
        // printf("Line %d, freeList push back 0x%lx\n", __LINE__, pagePtr);
        stat_used_bytes -= 4096;
            
        Addr newAddr = 0;
        if (cSize <= 256) {
            if (smallFreeList.size() > 0) {
                newAddr = smallFreeList.front();
                smallFreeList.pop_front();
            } else {
                newAddr = freeList.front();
                freeList.pop_front();
                for (int i = 1; i < 16; i++) {
                    smallFreeList.push_back(newAddr | (i << 8));
                }
            }
            stat_used_bytes += 256;
        } else if (cSize <= 1024) {
            if (moderateFreeList.size() > 0) {
                newAddr = moderateFreeList.front();
                moderateFreeList.pop_front();
            } else {
                newAddr = freeList.front();
                freeList.pop_front();
                for (int i = 1; i < 4; i++) {
                    moderateFreeList.push_back(newAddr | (i << 10));
                }
            }
            stat_used_bytes += 1024;
        } else {
            if (largeFreeList.size() > 0) {
                newAddr = largeFreeList.front();
                largeFreeList.pop_front();
            } else {
                newAddr = freeList.front();
                freeList.pop_front();
                largeFreeList.push_back(newAddr | (1 << 11));
            }
            stat_used_bytes += 2048;
        }

        // copy the compressed data into the space at newAddr
        mem_intr->atomicWrite(compressedPage, newAddr, compressedPage.size());
    
        // update the CTE (uncompressed to compressed)
        newCTE = (1ULL << 63) | (1ULL << 62) | (((cSize - 1) & ((1ULL << 12) - 1)) << 50) | ((newAddr >> 8) << 10);

        if (isAddressCovered(coldPageId * 4096, pkt->getSize(), 1)){
            printf("after being compressed, the new cte is 0x%lx\n", newCTE);
        }

        // update CTE in memory
        for (int i = 8 * coldLoc + 7; i >= 8 * coldLoc; i--) {
            cteCL[i] = newCTE & ((1 << 8) - 1);
            newCTE = newCTE >> 8;
        }
        mem_intr->atomicWrite(cteCL, coldCteAddrAligned, cteCL.size());

        if (coldCteAddrAligned == cteAddrAligned) {
            memcpy(cacheLine.data(), cteCL.data(), 64);
            for (int i = loc * 8; i < (loc + 1) * 8; i++) {
                cte = (cte << 8) | cacheLine[i];
            }
        }

        mcache.updateIfExist(coldCteAddrAligned, cteCL);
    }

    /*
        stage 6: process the pkt
            translate the physical address to dram address
            read/write the data from/to memory
    */

    // address translation
    Addr realAddr = addr | (pkt->getAddr() & ((1ULL << 12) - 1));
    pkt->DyLBackup = pkt->getAddr();
    pkt->setAddr(realAddr);

    // do the actual memory access and turn the packet into a response
    mem_intr->accessForDyL(pkt, pkt);

    if (isAddressCovered(pkt->DyLBackup, pkt->getSize(), 0)){
        printf("\n******* finish *********\n\n");
    }

    if (pkt->hasData()) {
        // this value is not supposed to be accurate, just enough to
        // keep things going, mimic a closed page
        // also this latency can't be 0
        return mem_intr->accessLatency();
    }
    return 0;
}

Tick
MemCtrl::recvAtomicLogicForNew(PacketPtr pkt, MemInterface* mem_intr) {
    DPRINTF(MemCtrl, "recvAtomic: %s 0x%x\n",
                     pkt->cmdString(), pkt->getAddr());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)){
        printf("recv Atomic: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
    }

    if (pkt->isWrite()) {
        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
            printf("Atomic write marker: ");
            // printf("marker:%lx\n", pkt);
            // printf("Timing read marker: ");
            uint8_t* start = pkt->getPtr<uint8_t>();
            for (int ts = 0; ts < pkt->getSize(); ts++) {
                printf("%02x ", static_cast<unsigned int>(start[ts]));
            }
            printf("\n");
            fflush(stdout);
        }
    }

    if (curTick() - lastRecomprTick >= recomprInterval) {
        lastRecomprTick = curTick();
        /* recompress a page */
        PPN target_page = 0;
        target_page = mcache.chooseTarget() / 64;
        // printf("recompress: the target page num is %d\n", target_page);

        std::vector<uint8_t> metaData(64, 0);
        Addr memory_addr = target_page * 64;
        mem_intr->atomicRead(metaData.data(), memory_addr, 64);

        // printf("[recompr]: the old metaData is \n");
        // for (int k = 0; k < 64; k++) {
        //     printf("%02x",static_cast<unsigned>(metaData[k]));
        // }
        // printf("\n");

        uint8_t num_blocks = metaData[1];
        std::vector<uint8_t> page(pageSizeMap[num_blocks]);

        /* read the old page */
        for (int i = 0; i < num_blocks; i++) {
            Addr block_addr = 0;
            for (int j = 0; j < 4; j++) {
                block_addr = (block_addr << 8) | (metaData[4 * i + 4 + j]);
            }
            block_addr = block_addr << 9;
            Addr block_size = pageSizeMap[i + 1] - pageSizeMap[i];
            mem_intr->atomicRead(page.data() + pageSizeMap[i], block_addr, block_size);
        }
        atomicRecompressForNew(page, metaData, mem_intr);

        // printf("[recompr]: the new metaData is: \n");
        // for (int k = 0; k < 64; k++) {
        //     printf("%02x",static_cast<unsigned>(metaData[k]));
        // }
        // printf("\n");

        if (mcache.isExist(memory_addr)){
            mcache.updateIfExist(memory_addr, metaData);
        }
        mem_intr->atomicWrite(metaData, memory_addr, 64);
    }

    /* real process the incoming pkt */

    unsigned size = pkt->getSize();
    uint32_t burst_size = mem_intr->bytesPerBurst();
    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    /* Step 0: create an auxPkt for write */
    PacketPtr aux_pkt = new Packet(pkt);
    aux_pkt->new_subPktCnt = pkt_count;
    aux_pkt->allocateForMC();
    memcpy(aux_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

    for (unsigned int i = 0; i < pkt_count; i++) {
        /* for each sub-block, create a sub-pkt to process */
        PacketPtr sub_pkt = new Packet(aux_pkt);
        sub_pkt->configAsSubPkt(aux_pkt, i);

        /* prepare metadata */
        PPN ppn = (sub_pkt->getAddr() >> 12 & ((1ULL << 52) - 1));

        /* step 1.1: calculate the MPA for metadata */
        Addr memory_addr = ppn * 64;
        std::vector<uint8_t> metaData(64, 0);

        if (mcache.isExist(memory_addr)) {
            sub_pkt->newMetaData = mcache.find(memory_addr);
        } else {
            mem_intr->atomicRead(metaData.data(), memory_addr, 64);
            if (!isValidMetaData(metaData)) {
                metaData = originMetaData;
                new_allocateBlock(metaData, 1);
            }
            sub_pkt->newMetaData = metaData;
        }

        /*
            update the metadata when necessary, processing the sub-pkt so that
            pkt->getAddr() return the mpa address,
            the size also should be in alignment with the compressed form
        */
        updateMetaDataForNew(sub_pkt, mem_intr);

        // printf("ppn is %d, the metadata is \n", ppn);
        // for (int k = 0; k < 64; k++) {
        //     printf("%02x",static_cast<unsigned>(sub_pkt->newMetaData[k]));
        // }
        // printf("\n");


        /* update the metadata in mcache & memory */
        metaData = sub_pkt->newMetaData;
        mcache.add(memory_addr, metaData);
        mem_intr->atomicWrite(metaData, memory_addr, 64);

        Addr origin_addr = sub_pkt->new_origin;  /* the origin address in OSPA space */

        if (sub_pkt->isRead()) {

            uint8_t cacheLineIdx = (origin_addr >> 6) & 0x3F;
            uint8_t type = new_getType(metaData, cacheLineIdx);
            Addr new_addr = 0;
            uint8_t coverage = new_getCoverage(metaData);


            if (coverage > cacheLineIdx && (type >= 0b100)) {
                uint64_t backup_size = sub_pkt->getSize();
                sub_pkt->setSizeForMC(1);
                // printf("[READ]: first access, the addr is 0x%lx, the size is 0x%d\n", sub_pkt->getAddr(), sub_pkt->getSize());
                mem_intr->accessForNew(sub_pkt, 1);
                sub_pkt->setSizeForMC(backup_size);
            } else {
                // printf("[READ]: first access, the addr is 0x%lx, the size is 0x%d\n", sub_pkt->getAddr(), sub_pkt->getSize());
                mem_intr->accessForNew(sub_pkt, 1);
            }

            if (coverage <= cacheLineIdx) {
                assert(type == 0);
                assert(sub_pkt->suffixLen == 0);
                new_addr = zeroAddr;
            } else {
                if (type >= 0b100) {
                    assert(sub_pkt->suffixLen == 0);
                    uint8_t overflowIdx = *(sub_pkt->getPtr<uint8_t>());
                    new_addr = calOverflowAddr(metaData, overflowIdx);
                } else {
                    new_addr = burstAlign(sub_pkt->getAddr(), mem_intr) + burst_size;
                    if (sub_pkt->suffixLen != 0) {
                        new_addr = sub_pkt->newBlockAddr;
                    }
                }

            }
            Addr start_addr = (type >= 0b100)?(new_addr):(sub_pkt->getAddr());
            PacketPtr readTwice = new Packet(sub_pkt);
            readTwice->configAsReadTwice(sub_pkt, new_addr, start_addr);

            readTwice->setAddr(readTwice->new_origin);

            mem_intr->accessForNew(readTwice, 1);
            // printf("[read] twice, the address is 0x%lx, the size is %d\n", readTwice->getAddr(), readTwice->getSize());

            std::vector<uint8_t> cacheLine(64, 0);

            cacheLineIdx = (origin_addr >> 6) & 0x3F;
            type = new_getType(sub_pkt->newMetaData, cacheLineIdx);

            memcpy(cacheLine.data(), readTwice->getPtr<uint8_t>(), readTwice->getSize());

            // if (isAddressCovered(aux_pkt->getAddr(), aux_pkt->getSize(), 1)) {
            //     printf("before restore , the data is\n");
            //     for (int i = 0; i < pkt->getSize(); i++) {
            //         if (i % 8 == 0) {
            //             printf("\n");
            //         }
            //         printf("%02x ", static_cast<unsigned int>(cacheLine[i]));
            //     }
            //     printf("\n");
            // }

            new_restoreData(cacheLine, type);

            // if (isAddressCovered(aux_pkt->getAddr(), aux_pkt->getSize(), 1)) {
            //     printf("the original address of sub-pkt is 0x%lx\n", origin_addr);
            //     printf("ppn is %d, the metadata is:\n", (origin_addr >> 12));
            //     for (int k = 0; k < 64; k++) {
            //         printf("%02x",static_cast<unsigned>(sub_pkt->newMetaData[k]));
            //     }
            //     printf("\n");
            //     printf("the readed cacheline is:");

            //     for (int i = 0; i < cacheLine.size(); i++) {
            //         if (i % 8 == 0) {
            //             printf("\n");
            //         }
            //         printf("%02x ", static_cast<unsigned int>(cacheLine[i]));
            //     }
            //     printf("\n");
            // }

            uint64_t cur_loc = origin_addr & 0x3F;
            uint64_t cur_size = std::min(64UL - cur_loc, aux_pkt->getAddr() + aux_pkt->getSize() - origin_addr);

            assert(aux_pkt->getAddr() <= origin_addr);

            uint64_t offset = origin_addr - aux_pkt->getAddr();
            if (offset != 0) {
                assert(cur_loc == 0);
            }
            memcpy(aux_pkt->getPtr<uint8_t>() + offset, cacheLine.data() + cur_loc, cur_size);

            aux_pkt->new_subPktCnt--;
            delete readTwice;
            delete sub_pkt;

        } else {
            assert(pkt->isWrite());
            uint8_t cacheLineIdx = (origin_addr >> 6) & 0x3F;
            uint8_t type = new_getType(metaData, cacheLineIdx);
            if (type >= 0b100) {
                /* the real data is in the overflow region */

                assert(sub_pkt->getSize() == 64);
                uint8_t overflowIdx = 0;

                mem_intr->atomicRead(&overflowIdx, sub_pkt->getAddr(), 1);

                Addr real_addr = calOverflowAddr(metaData, overflowIdx);
                sub_pkt->setAddr(real_addr);
            }
            mem_intr->accessForNew(sub_pkt, 1);

            aux_pkt->new_subPktCnt--;
            delete sub_pkt;
        }
    }


    /* aux pkt */
    assert(aux_pkt->new_backup);

    bool needsResponse = pkt->needsResponse();

    panic_if(!mem_intr->getAddrRange().contains(pkt->getAddr()),
                "Can't handle address range for packet %s\n", pkt->print());

    if (pkt->isRead()) {
        assert(pkt->getSize() == aux_pkt->getSize());
        memcpy(pkt->getPtr<uint8_t>(), aux_pkt->getPtr<uint8_t>(), pkt->getSize());
        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
            printf("Atomic read marker: ");
            // printf("marker:%lx\n", pkt);
            // printf("Timing read marker: ");
            uint8_t* start = pkt->getPtr<uint8_t>();
            for (int ts = 0; ts < pkt->getSize(); ts++) {
                printf("%02x ", static_cast<unsigned int>(start[ts]));
            }
            printf("\n");
            fflush(stdout);
        }

    }

    mem_intr->accessForNew(aux_pkt, 0);

    delete aux_pkt;

    if (pkt->hasData()) {
        // this value is not supposed to be accurate, just enough to
        // keep things going, mimic a closed page
        // also this latency can't be 0
        return mem_intr->accessLatency();
    }

    return 0;
}

Tick
MemCtrl::recvAtomicLogicForSecure(PacketPtr pkt, MemInterface* mem_intr) {
    DPRINTF(MemCtrl, "recvAtomic: %s 0x%x\n",
                     pkt->cmdString(), pkt->getAddr());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
        printf("\n\n=====================\n\n");
        printf("recv Atomic: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
    }

    // calculate the dram address of the metaDatas
    Addr phyAddr = pkt->getAddr();
    PPN ppn = ((phyAddr >> 12) & ((1ULL << 52) - 1));
    Addr mAddr = startAddrForSecureMetaData + ppn * 8;

    std::vector<uint8_t> metaDataEntry = mcache.find(mAddr);

    Addr dram_addr = 0;

    PacketPtr auxPkt = new Packet(pkt);
    auxPkt->backupForSecure = phyAddr;

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
        printf("the first elem of metadata is %lx\n", metaDataEntry[0]);
    }

    if (metaDataEntry[0] >= 0x80) {
        /* metadata cache hit */

        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
            printf("the metadata cache hit\n");
        }

        assert(((metaDataEntry[0] >> 6) & 0x1) == 0);
        /* the page should be uncompressed */

        Addr page_dram_addr = parseMetaDataForSecure(metaDataEntry, 0);
        dram_addr = page_dram_addr | (phyAddr & ((0x1 << 12) - 1));

    } else {
        /* metaData cache miss */
        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
            printf("the metadata cache miss\n");
        }

        if (mcache.isFull()) {
            // printf("the cache is full, evict a page first\n");
            /* evict an entry first */
            Addr evicted_page_mAddr = mcache.lastElemAddr();
            std::vector<uint8_t> evicted_matedata_entry = mcache.find(evicted_page_mAddr, false);
            mcache.pop();

            PPN evicted_ppn = (evicted_page_mAddr - startAddrForSecureMetaData) / 8;
            Addr evicted_phy_addr = evicted_ppn * 4096;

            if (isAddressCovered(evicted_phy_addr, 4096, 1)) {
                printf("the cache is full, evict a page first, ppn is %ld\n", evicted_ppn);

                printf("the old metadata is \n");

                for (int i = 0; i < 8; i++) {
                    printf("%lx ", evicted_matedata_entry[i]);
                }
                printf("\n");

                printf("the maddr is 0x%lx\n", evicted_page_mAddr);

                std::vector<uint8_t> mytest = mcache.find(evicted_page_mAddr);
                printf("now read from metadata, should be invalid\n");
                for (int i = 0; i < 8; i++) {
                    printf("%02x ", mytest[i]);
                }
            }

            /* read the evicted page */
            std::vector<uint8_t> origin_page(4096);
            Addr evicted_page_dramAddr = parseMetaDataForSecure(evicted_matedata_entry, 0);

            mem_intr->atomicRead(origin_page.data(), evicted_page_dramAddr, 4096);

            /* try to compress the page */
            std::vector<uint8_t> cPage = compressPage(origin_page.data(), 4096);

            if (cPage.size() <= 2048) {
                /* the page is compressible */
                Addr new_chunk_addr = allocateChunkForSecure(0);

                // if (new_chunk_addr <= 0x1378d0 && new_chunk_addr + cPage.size() > 0x1378d0) {
                //     printf("the page is writting to a new space: address is 0x%lx\n", new_chunk_addr);
                //     for (int i = 0; i < cPage.size(); i++) {
                //         if (i % 8 == 0) {
                //             printf("\n");
                //         }
                //         printf("%lx ", cPage[i]);
                //     }
                // }

                if (isAddressCovered(evicted_phy_addr, 4096, 1)) {
                    printf("the page is compressible\n");
                    printf("allocate a new chunk is 0x%lx\n", new_chunk_addr);
                }

                mem_intr->atomicWrite(cPage, new_chunk_addr, cPage.size());

                recycleChunkForSecure(evicted_page_dramAddr, 1);

                /* update the metaData */

                std::vector<uint8_t> evicted_metaData(8);
                memcpy(evicted_metaData.data(), evicted_matedata_entry.data(), 8);

                evicted_metaData[0] = 0x80 | 0x40;  // set the compressed bit to one

                uint64_t cSize = cPage.size();

                for (int i = 2; i >= 1; i--) {
                    evicted_metaData[i] = cSize & 0xFF;
                    cSize = cSize >> 8;
                }
                assert(cSize == 0);
                new_chunk_addr >>= 11;

                for (int i = 6; i >= 3; i--) {
                    evicted_metaData[i] = new_chunk_addr & 0xFF;
                    new_chunk_addr >>= 8;
                }

                mem_intr->atomicWrite(evicted_metaData, evicted_page_mAddr, 8);

                if (isAddressCovered(evicted_phy_addr, 4096, 1)) {
                    printf("the metadata is updated, the new metadata is \n");

                    for (int i = 0; i < 8; i++) {
                        printf("%lx ", evicted_metaData[i]);
                    }
                    printf("\n");
                }

            } else {
                /* keep as is */
            }
        }

        /* read the metadata for the current pkt */
        std::vector<uint8_t> metaData(8);
        mem_intr->atomicRead(metaData.data(), mAddr, 8);

        if(metaData[0] < (0x1 << 7)) {
            /* the metaData is invalid now */
            initialMetaDataForSecure(metaData);

            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                printf("atomic: the metadata is invalid now\n");
                printf("the metadata is \n");

                for (int i = 0; i < metaData.size(); i++) {
                    printf("%lx ", metaData[i]);
                }
            }

        }

        if (((metaData[0] >> 6) & 0x1) == 0x1) {
            /* if the page is currently compressed */
            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                printf("the page is currently compressed\n");
            }

            uint64_t cSize = parseMetaDataForSecure(metaData, 1);
            Addr old_chunk_addr = parseMetaDataForSecure(metaData, 0);
            std::vector<uint8_t> cPage(cSize);
            mem_intr->atomicRead(cPage.data(), old_chunk_addr, cSize);

            std::vector<uint8_t> origin_page = decompressPage(cPage.data(), cSize);
            assert(origin_page.size() == 4096);

            /* write the uncompressed page to a new place */
            Addr new_chunk_addr = allocateChunkForSecure(1);
            recycleChunkForSecure(old_chunk_addr, 0);

            mem_intr->atomicWrite(origin_page, new_chunk_addr, 4096);

            /* update the corresponding metadata */
            metaData[0] = 0x80;
            new_chunk_addr >>= 11;
            for (int i = 6; i >= 3; i--) {
                metaData[i] = new_chunk_addr & 0xFF;
                new_chunk_addr >>= 8;
            }
        }

        mem_intr->atomicWrite(metaData, mAddr, 8);
        assert(!mcache.isFull());
        memcpy(metaDataEntry.data(), metaData.data(), 8);

        mcache.add(mAddr, metaDataEntry);

        Addr page_dram_addr = parseMetaDataForSecure(metaData, 0);
        dram_addr = page_dram_addr | (phyAddr & ((0x1 << 12) - 1));

    }

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
        printf("atomic: the dram_addr is 0x%lx\n", dram_addr);
    }

    auxPkt->configAsSecureAuxPkt(pkt, dram_addr, pkt->getSize());

    // do the actual memory access and turn the packet into a response
    mem_intr->accessForSecure(auxPkt, access_cnt);

    delete auxPkt;

    if (pkt->hasData()) {
        // this value is not supposed to be accurate, just enough to
        // keep things going, mimic a closed page
        // also this latency can't be 0
        return mem_intr->accessLatency();
    }

    return 0;
}

Tick
MemCtrl::recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &backdoor)
{
    Tick latency = recvAtomic(pkt);
    dram->getBackdoor(backdoor);
    return latency;
}

bool
MemCtrl::readQueueFull(unsigned int neededEntries) const
{
    DPRINTF(MemCtrl,
            "Read queue limit %d, current size %d, entries needed %d\n",
            readBufferSize, totalReadQueueSize + respQueue.size(),
            neededEntries);

    auto rdsize_new = totalReadQueueSize + respQueue.size() + neededEntries;
    return rdsize_new > readBufferSize;
}

bool
MemCtrl::expectReadQueueFull(unsigned int neededEntries) const
{
    // DPRINTF(MemCtrl,
    //         "Read queue limit %d, current size %d, entries needed %d\n",
    //         readBufferSize, expectReadQueueSize + respQueue.size(),
    //         neededEntries);

    // printf("expectReadQueueSize is %d\n", expectReadQueueSize);
    // printf("respQueue size is %d\n", respQueue.size());
    // printf("neededEnteries: %d\n", neededEntries);
    // printf("readBufferSize is %d\n", readBufferSize);

    auto rdsize_new = expectReadQueueSize + respQueue.size() + neededEntries;
    return rdsize_new > readBufferSize;
}

bool
MemCtrl::writeQueueFull(unsigned int neededEntries) const
{
    DPRINTF(MemCtrl,
            "Write queue limit %d, current size %d, entries needed %d\n",
            writeBufferSize, totalWriteQueueSize, neededEntries);

    auto wrsize_new = (totalWriteQueueSize + neededEntries);
    return  wrsize_new > writeBufferSize;
}

bool
MemCtrl::expectWriteQueueFull(unsigned int neededEntries) const
{
    // DPRINTF(MemCtrl,
    //     "Write queue limit %d, current size [in expectation] %d, entries needed %d\n",
    //     writeBufferSize, expectWriteQueueSize, neededEntries);

    auto wrsize_new = (expectWriteQueueSize + neededEntries);
    return  wrsize_new > writeBufferSize;
}

bool
MemCtrl::addToReadQueue(PacketPtr pkt,
                unsigned int pkt_count, MemInterface* mem_intr)
{
    // only add to the read queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(!pkt->isWrite());

    assert(pkt_count != 0);

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    // Note if the pkt starting address is not aligened to burst size, the
    // address of first packet is kept unaliged. Subsequent packets
    // are aligned to burst size boundaries. This is to ensure we accurately
    // check read packets against packets in write queue.
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;
    unsigned pktsServicedByWrQ = 0;
    BurstHelper* burst_helper = NULL;

    uint32_t burst_size = mem_intr->bytesPerBurst();

    for (int cnt = 0; cnt < pkt_count; ++cnt) {
        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                        base_addr + pkt->getSize()) - addr;
        stats.readPktSize[ceilLog2(size)]++;
        stats.readBursts++;
        stats.requestorReadAccesses[pkt->requestorId()]++;

        // First check write buffer to see if the data is already at
        // the controller
        bool foundInWrQ = false;
        Addr burst_addr = burstAlign(addr, mem_intr);
        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
            for (const auto& vec : writeQueue) {
                for (const auto& p : vec) {
                    // check if the read is subsumed in the write queue
                    // packet we are looking at
                    if (p->addr <= addr &&
                       ((addr + size) <= (p->addr + p->size))) {

                        foundInWrQ = true;
                        stats.servicedByWrQ++;
                        pktsServicedByWrQ++;
                        DPRINTF(MemCtrl,
                                "Read to addr %#x with size %d serviced by "
                                "write queue\n",
                                addr, size);
                        stats.bytesReadWrQ += burst_size;
                        break;
                    }
                }
            }
        }

        // If not found in the write q, make a memory packet and
        // push it onto the read queue
        if (!foundInWrQ) {

            // Make the burst helper for split packets
            if (pkt_count > 1 && burst_helper == NULL) {
                DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                        "memory requests\n", pkt->getAddr(), pkt_count);
                burst_helper = new BurstHelper(pkt_count);
            }

            MemPacket* mem_pkt;
            mem_pkt = mem_intr->decodePacket(pkt, addr, size, true,
                                                    mem_intr->pseudoChannel);
            // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
            // Increment read entries of the rank (dram)
            // Increment count to trigger issue of non-deterministic read (nvm)
            mem_intr->setupRank(mem_pkt->rank, true);
            // Default readyTime to Max; will be reset once read is issued
            mem_pkt->readyTime = MaxTick;
            mem_pkt->burstHelper = burst_helper;

            assert(!readQueueFull(1));
            stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

            DPRINTF(MemCtrl, "Adding to read queue\n");
            // printf("**********************Line %d: push back read queue, address is %lx\n", __LINE__, mem_pkt->pkt->comprBackup);

            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

            // log packet
            logRequest(MemCtrl::READ, pkt->requestorId(),
                       pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->readQueueSize++;

            // Update stats
            stats.avgRdQLen = totalReadQueueSize + respQueue.size();
        }

        // Starting address of next memory pkt (aligned to burst boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }

    // If all packets are serviced by write queue, we send the repsonse back
    if (pktsServicedByWrQ == pkt_count) {
        accessAndRespond(pkt, frontendLatency, mem_intr);
        return true;
    }

    // Update how many split packets are serviced by write queue
    if (burst_helper != NULL)
        burst_helper->burstsServiced = pktsServicedByWrQ;

    // not all/any packets serviced by the write queue
    return false;
}

bool
MemCtrl::addToReadQueueForCompr(PacketPtr pkt,
                unsigned int pkt_count, MemInterface* mem_intr) {
    // only add to the read queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(!pkt->isWrite());

    assert(pkt_count != 0);

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    // Note if the pkt starting address is not aligened to burst size, the
    // address of first packet is kept unaliged. Subsequent packets
    // are aligned to burst size boundaries. This is to ensure we accurately
    // check read packets against packets in write queue.
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;

    unsigned pktsServicedByWrQ = 0;
    unsigned pktsServicedByPageBuffer = 0;
    BurstHelper* burst_helper = NULL;

    uint32_t burst_size = mem_intr->bytesPerBurst();
    /* right now limit the burst size to be 64 */
    assert(burst_size == 64);

    /* prepare the auxiliary information */

    std::vector<uint8_t> sizeMap = {0, 8, 32, 64};

    std::unordered_map<uint64_t, std::vector<uint8_t>> metaDataMap = pkt->comprMetaDataMap;

    unsigned extraAccess = 0;

    if (pkt->getPType() == 0x4) {
        // readMetaData pkt, the address is already in MPA
        assert(pkt_count == 1);
        unsigned size = pkt->getSize();

        bool foundInWrQ = false;
        Addr burst_addr = burstAlign(addr, mem_intr);

        assert(burst_addr == addr);
        assert(size == burst_size);
        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
            for (const auto& vec : writeQueue) {
                for (const auto& p : vec) {
                    // check if the read is subsumed in the write queue
                    // packet we are looking at
                    if (p->addr <= addr &&
                       ((addr + size) <= (p->addr + p->size))) {
                        foundInWrQ = true;
                        pktsServicedByWrQ++;
                        break;
                    }
                }
            }
        }

        std::vector<uint8_t> metaData(64, 0);

        // If not found in the write q, make a memory packet and
        // push it onto the read queue
        if (!foundInWrQ) {
            MemPacket* mem_pkt;
            mem_pkt = mem_intr->decodePacket(pkt, addr, size, true,
                                                    mem_intr->pseudoChannel);
            // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
            // Increment read entries of the rank (dram)
            // Increment count to trigger issue of non-deterministic read (nvm)
            mem_intr->setupRank(mem_pkt->rank, true);
            // Default readyTime to Max; will be reset once read is issued
            mem_pkt->readyTime = MaxTick;
            mem_pkt->burstHelper = burst_helper;

            DPRINTF(MemCtrl, "Adding to read queue\n");


            // printf("**********************Line %d: push back read queue, address is %lx\n", __LINE__, mem_pkt->pkt->comprBackup);
            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

            // log packet
            logRequest(MemCtrl::READ, pkt->requestorId(),
                       pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->readQueueSize++;

            return false;
        } else {
            // printf("Line %d, ener the access and respond for compr\n", __LINE__);
            accessAndRespondForCompr(pkt, frontendLatency, mem_intr);
            return true;
        }
    }

    /* else the pkt is auxPkt or readForCompress */

    if (pkt->getPType() == 0x2) {
        /* pkt is auxPkt */
        for (int cnt = 0; cnt < pkt_count; ++cnt) {
            unsigned size = std::min((addr | (burst_size - 1)) + 1,
                            base_addr + pkt->getSize()) - addr;
            stats.readPktSize[ceilLog2(size)]++;

            /* Compr: use the metadata to do the translation */
            PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));
            uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

            assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */
            std::vector<uint8_t> metaData = metaDataMap[ppn];

            bool crossBoundary = false;
            Addr real_addr = addr;
            unsigned real_size = size;

            if (ppn == pageNum) {
                stats.readBursts++;
                stats.requestorReadAccesses[pkt->requestorId()]++;
                pktsServicedByPageBuffer++;

            } else {
                assert(isValidMetaData(metaData));
                uint8_t type = getType(metaData, cacheLineIdx);
                std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);

                bool inInflate = cLStatus.first;
                real_addr = cLStatus.second;
                real_size = (inInflate)?64:sizeMap[type];

                if (real_size == 0) {
                    stats.readBursts++;
                    stats.requestorReadAccesses[pkt->requestorId()]++;
                    pktsServicedByPageBuffer++;
                } else {
                    Addr end_addr = real_addr + real_size - 1;
                    uint8_t burstN = (end_addr / burst_size) - (real_addr / burst_size) + 1;
                    assert(burstN <= 2);
                    if (burstN > 1) {
                        crossBoundary = true;
                    }

                    stats.readBursts += burstN;
                    stats.requestorReadAccesses[pkt->requestorId()] += burstN;

                    if (crossBoundary) {
                        extraAccess++;
                        unsigned prefix = (real_addr | (burst_size - 1)) + 1 - real_addr;
                        assert(prefix < real_size);
                        unsigned suffix = real_size - prefix;
                        std::vector<unsigned> memPktLen = {prefix, suffix};
                        std::vector<Addr> memPktAddr = {real_addr, (real_addr | (burst_size - 1)) + 1};

                        for (unsigned int j = 0; j < 2; j++) {
                            // First check write buffer to see if the data is already at
                            // the controller
                            bool foundInWrQ = false;
                            Addr burst_addr = burstAlign(memPktAddr[j], mem_intr);
                            // if the burst address is not present then there is no need
                            // looking any further
                            if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
                                for (const auto& vec : writeQueue) {
                                    for (const auto& p : vec) {
                                        // check if the read is subsumed in the write queue
                                        // packet we are looking at
                                        if (p->addr <= memPktAddr[j] &&
                                            ((memPktAddr[j] + memPktLen[j]) <= (p->addr + p->size))) {

                                            foundInWrQ = true;
                                            stats.servicedByWrQ++;
                                            pktsServicedByWrQ++;
                                            DPRINTF(MemCtrl,
                                                    "Read to addr %#x with size %d serviced by "
                                                    "write queue\n",
                                                    addr, size);
                                            stats.bytesReadWrQ += burst_size;
                                            break;
                                        }
                                    }
                                }
                            }

                            if (!foundInWrQ) {
                                // Make the burst helper for split packets
                                if (burst_helper == NULL) {
                                    DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                                            "memory requests\n", pkt->getAddr(), pkt_count);
                                    burst_helper = new BurstHelper(pkt_count);
                                }
                                MemPacket* mem_pkt;
                                mem_pkt = mem_intr->decodePacket(pkt, memPktAddr[j], memPktLen[j], true,
                                                                        mem_intr->pseudoChannel);
                                // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
                                // Increment read entries of the rank (dram)
                                // Increment count to trigger issue of non-deterministic read (nvm)
                                mem_intr->setupRank(mem_pkt->rank, true);
                                // Default readyTime to Max; will be reset once read is issued
                                mem_pkt->readyTime = MaxTick;
                                mem_pkt->burstHelper = burst_helper;

                                // stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

                                DPRINTF(MemCtrl, "Compr: Adding to read queue\n");

                                // printf("**********************Line %d: push back read queue, address is %lx\n",__LINE__,  mem_pkt->pkt->comprBackup);
                                readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

                                // log packet
                                logRequest(MemCtrl::READ, pkt->requestorId(),
                                        pkt->qosValue(), mem_pkt->addr, 1);

                                mem_intr->readQueueSize++;

                                // Update stats
                                stats.avgRdQLen = totalReadQueueSize + respQueue.size();
                            }
                        }
                    } else {
                        // First check write buffer to see if the data is already at
                        // the controller
                        bool foundInWrQ = false;
                        Addr burst_addr = burstAlign(real_addr, mem_intr);
                        // if the burst address is not present then there is no need
                        // looking any further
                        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
                            for (const auto& vec : writeQueue) {
                                for (const auto& p : vec) {
                                    // check if the read is subsumed in the write queue
                                    // packet we are looking at
                                    if (p->addr <= real_addr &&
                                       ((real_addr + real_size) <= (p->addr + p->size))) {

                                        foundInWrQ = true;
                                        stats.servicedByWrQ++;
                                        pktsServicedByWrQ++;
                                        DPRINTF(MemCtrl,
                                                "Read to addr %#x with size %d serviced by "
                                                "write queue\n",
                                                addr, size);
                                        stats.bytesReadWrQ += burst_size;
                                        break;
                                    }
                                }
                            }
                        }

                        // If not found in the write q, make a memory packet and
                        // push it onto the read queue
                        if (!foundInWrQ) {
                            // Make the burst helper for split packets
                            if (pkt_count > 1 && burst_helper == NULL) {
                                DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                                        "memory requests\n", pkt->getAddr(), pkt_count);
                                burst_helper = new BurstHelper(pkt_count);
                            }
                            MemPacket* mem_pkt;
                            mem_pkt = mem_intr->decodePacket(pkt, real_addr, real_size, true,
                                                                    mem_intr->pseudoChannel);

                            // Increment read entries of the rank (dram)
                            // Increment count to trigger issue of non-deterministic read (nvm)
                            mem_intr->setupRank(mem_pkt->rank, true);
                            // Default readyTime to Max; will be reset once read is issued
                            mem_pkt->readyTime = MaxTick;
                            mem_pkt->burstHelper = burst_helper;

                            // stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

                            DPRINTF(MemCtrl, "Adding to read queue\n");
                            // printf("**********************Line %d: push back read queue, address is %lx\n",__LINE__,  mem_pkt->pkt->comprBackup);

                            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

                            // log packet
                            logRequest(MemCtrl::READ, pkt->requestorId(),
                                    pkt->qosValue(), mem_pkt->addr, 1);

                            mem_intr->readQueueSize++;

                            // Update stats
                            stats.avgRdQLen = totalReadQueueSize + respQueue.size();
                        }
                    }
                }
            }
            // Starting address of next memory pkt (aligned to burst boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }
    } else if (pkt->getPType() == 0x10) {
        /* pkt is readForCompress */
        assert((pkt->getAddr() & 0xFFF) == 0);
        assert(pkt->getSize() == 4096);
        assert(pkt_count == 64);

        for (int cnt = 0; cnt < pkt_count; ++cnt) {
            /* Compr: use the metadata to do the translation */
            PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));
            uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

            assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */
            std::vector<uint8_t> metaData = metaDataMap[ppn];

            bool crossBoundary = false;
            Addr real_addr = addr;
            unsigned real_size = 64;

            assert(ppn != pageNum);
            assert(isValidMetaData(metaData));

            uint8_t type = getType(metaData, cacheLineIdx);
            std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);

            bool inInflate = cLStatus.first;
            real_addr = cLStatus.second;
            real_size = (inInflate)?64:sizeMap[type];

            if (real_size == 0) {
                stats.readBursts++;
                stats.requestorReadAccesses[pkt->requestorId()]++;
                pktsServicedByPageBuffer++;
            } else {
                Addr end_addr = real_addr + real_size - 1;
                uint8_t burstN = (end_addr / burst_size) - (real_addr / burst_size) + 1;
                assert(burstN <= 2);
                if (burstN > 1) {
                    crossBoundary = true;
                }

                if (crossBoundary) {
                    extraAccess++;
                    unsigned prefix = (real_addr | (burst_size - 1)) + 1 - real_addr;
                    assert(prefix < real_size);
                    unsigned suffix = real_size - prefix;
                    std::vector<unsigned> memPktLen = {prefix, suffix};
                    std::vector<Addr> memPktAddr = {real_addr, (real_addr | (burst_size - 1)) + 1};

                    for (unsigned int j = 0; j < 2; j++) {
                        // First check write buffer to see if the data is already at
                        // the controller
                        bool foundInWrQ = false;
                        Addr burst_addr = burstAlign(memPktAddr[j], mem_intr);
                        // if the burst address is not present then there is no need
                        // looking any further
                        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
                            for (const auto& vec : writeQueue) {
                                for (const auto& p : vec) {
                                    // check if the read is subsumed in the write queue
                                    // packet we are looking at
                                    if (p->addr <= memPktAddr[j] &&
                                        ((memPktAddr[j] + memPktLen[j]) <= (p->addr + p->size))) {

                                        foundInWrQ = true;
                                        pktsServicedByWrQ++;
                                        DPRINTF(MemCtrl,
                                                "Read to addr %#x with size %d serviced by "
                                                "write queue\n",
                                                memPktAddr[j], memPktLen[j]);
                                        break;
                                    }
                                }
                            }
                        }

                        if (!foundInWrQ) {
                            // Make the burst helper for split packets
                            if (burst_helper == NULL) {
                                DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                                        "memory requests\n", pkt->getAddr(), pkt_count);
                                burst_helper = new BurstHelper(pkt_count);
                            }
                            MemPacket* mem_pkt;
                            mem_pkt = mem_intr->decodePacket(pkt, memPktAddr[j], memPktLen[j], true,
                                                                    mem_intr->pseudoChannel);

                            // Increment read entries of the rank (dram)
                            // Increment count to trigger issue of non-deterministic read (nvm)
                            mem_intr->setupRank(mem_pkt->rank, true);
                            // Default readyTime to Max; will be reset once read is issued
                            mem_pkt->readyTime = MaxTick;
                            mem_pkt->burstHelper = burst_helper;

                            // stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

                            DPRINTF(MemCtrl, "Compr: Adding to read queue\n");
                            // printf("**********************Line %d: push back read queue, address is %lx\n", __LINE__, mem_pkt->pkt->comprBackup);
                            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

                            // log packet
                            logRequest(MemCtrl::READ, pkt->requestorId(),
                                    pkt->qosValue(), mem_pkt->addr, 1);

                            mem_intr->readQueueSize++;
                        }
                    }
                } else {
                    // First check write buffer to see if the data is already at
                    // the controller
                    bool foundInWrQ = false;
                    Addr burst_addr = burstAlign(real_addr, mem_intr);
                    // if the burst address is not present then there is no need
                    // looking any further
                    if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
                        for (const auto& vec : writeQueue) {
                            for (const auto& p : vec) {
                                // check if the read is subsumed in the write queue
                                // packet we are looking at
                                if (p->addr <= real_addr &&
                                   ((real_addr + real_size) <= (p->addr + p->size))) {

                                    foundInWrQ = true;
                                    pktsServicedByWrQ++;
                                    DPRINTF(MemCtrl,
                                            "Read to addr %#x with size %d serviced by "
                                            "write queue\n",
                                            real_addr, real_size);
                                    break;
                                }
                            }
                        }
                    }

                    // If not found in the write q, make a memory packet and
                    // push it onto the read queue
                    if (!foundInWrQ) {
                        // Make the burst helper for split packets
                        if (burst_helper == NULL) {
                            DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                                    "memory requests\n", pkt->getAddr(), pkt_count);
                            burst_helper = new BurstHelper(pkt_count);
                        }
                        MemPacket* mem_pkt;
                        mem_pkt = mem_intr->decodePacket(pkt, real_addr, real_size, true,
                                                                mem_intr->pseudoChannel);

                        // Increment read entries of the rank (dram)
                        // Increment count to trigger issue of non-deterministic read (nvm)
                        mem_intr->setupRank(mem_pkt->rank, true);
                        // Default readyTime to Max; will be reset once read is issued
                        mem_pkt->readyTime = MaxTick;
                        mem_pkt->burstHelper = burst_helper;

                        DPRINTF(MemCtrl, "Adding to read queue\n");
                        // printf("**********************Line %d: push back read queue, address is %lx\n", __LINE__, mem_pkt->pkt->comprBackup);
                        readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

                        // log packet
                        logRequest(MemCtrl::READ, pkt->requestorId(),
                                pkt->qosValue(), mem_pkt->addr, 1);

                        mem_intr->readQueueSize++;
                    }
                }
            }
            // Starting address of next memory pkt (aligned to burst boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }
    }

    // If all packets are serviced by write queue, we send the repsonse back
    if (pktsServicedByWrQ + pktsServicedByPageBuffer == pkt_count) {
        DPRINTF(MemCtrl, "Line %d, next enter the access and respond function\n", __LINE__);
        // printf("Line %d, ener the access and respond for compr\n", __LINE__);
        accessAndRespondForCompr(pkt, frontendLatency, mem_intr);
        return true;
    }

    // Update how many split packets are serviced by write queue
    if (burst_helper != NULL) {
        burst_helper->burstsServiced = pktsServicedByWrQ + pktsServicedByPageBuffer;
        burst_helper->burstCount = pkt_count + extraAccess;
    }

    // not all/any packets serviced by the write queue
    return false;

}

bool
MemCtrl::addToReadQueueForDyL(PacketPtr pkt,
                unsigned int pkt_count, MemInterface* mem_intr) {
    // only add to the read queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(!pkt->isWrite());
    assert(pkt->DyLPType != 0x1);
    assert(pkt_count != 0);

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    // Note if the pkt starting address is not aligened to burst size, the
    // address of first packet is kept unaliged. Subsequent packets
    // are aligned to burst size boundaries. This is to ensure we accurately
    // check read packets against packets in write queue.
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;
    unsigned pktsServicedByWrQ = 0;
    BurstHelper* burst_helper = NULL;

    uint32_t burst_size = mem_intr->bytesPerBurst();

    assert(burst_size == 64);

    DPRINTF(MemCtrl, "Line %d: finish the preparation\n", __LINE__);

    // if pkt is not auxPkt, current pkt is issued by memory controller
    if (pkt->DyLPType != 0x2) {
        DPRINTF(MemCtrl, "Line %d: Enter the add-to-read-queue special part\n", __LINE__);
        for (int cnt = 0; cnt < pkt_count; ++cnt) {
            unsigned size = std::min((addr | (burst_size - 1)) + 1,
                            base_addr + pkt->getSize()) - addr;

            // First check write buffer to see if the data is already at
            // the controller
            bool foundInWrQ = false;
            Addr burst_addr = burstAlign(addr, mem_intr);
            // if the burst address is not present then there is no need
            // looking any further
            if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
                for (const auto& vec : writeQueue) {
                    for (const auto& p : vec) {
                        // check if the read is subsumed in the write queue
                        // packet we are looking at
                        if (p->addr <= addr &&
                        ((addr + size) <= (p->addr + p->size))) {

                            foundInWrQ = true;
                            pktsServicedByWrQ++;
                            break;
                        }
                    }
                }
            }

            // If not found in the write q, make a memory packet and
            // push it onto the read queue
            if (!foundInWrQ) {

                // Make the burst helper for split packets
                if (pkt_count > 1 && burst_helper == NULL) {
                    burst_helper = new BurstHelper(pkt_count);
                }
                MemPacket* mem_pkt;
                mem_pkt = mem_intr->decodePacket(pkt, addr, size, true,
                                                        mem_intr->pseudoChannel);
                // Increment read entries of the rank (dram)
                // Increment count to trigger issue of non-deterministic read (nvm)
                mem_intr->setupRank(mem_pkt->rank, true);
                // Default readyTime to Max; will be reset once read is issued
                mem_pkt->readyTime = MaxTick;
                mem_pkt->burstHelper = burst_helper;
                readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

                // log the request
                logRequest(MemCtrl::READ, pkt->requestorId(),
                        pkt->qosValue(), mem_pkt->addr, 1);

                mem_intr->readQueueSize++;
            }

            // Starting address of next memory pkt (aligned to burst boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }

        // If all packets are serviced by write queue, we perform next steps
        if (pktsServicedByWrQ == pkt_count) {
            accessAndRespondForDyL(pkt, frontendLatency + backendLatency, mem_intr);

            return true;
        }

        // Update how many split packets are serviced by write queue
        if (burst_helper != NULL)
            burst_helper->burstsServiced = pktsServicedByWrQ;

        // not all/any packets serviced by the write queue
        return false;
    }

    for (int cnt = 0; cnt < pkt_count; ++cnt) {
        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                        base_addr + pkt->getSize()) - addr;
        DPRINTF(MemCtrl, "Line %d: Iterate cnt = %d\n", __LINE__, cnt);
        stats.readPktSize[ceilLog2(size)]++;
        stats.readBursts++;
        stats.requestorReadAccesses[pkt->requestorId()]++;

        // First check write buffer to see if the data is already at
        // the controller
        bool foundInWrQ = false;
        Addr burst_addr = burstAlign(addr, mem_intr);

        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
            for (const auto& vec : writeQueue) {
                for (const auto& p : vec) {
                    // check if the read is subsumed in the write queue
                    // packet we are looking at
                    if (p->addr <= addr &&
                    ((addr + size) <= (p->addr + p->size))) {

                        foundInWrQ = true;
                        stats.servicedByWrQ++;
                        pktsServicedByWrQ++;
                        DPRINTF(MemCtrl,
                                "Read to addr %#x with size %d serviced by "
                                "write queue\n",
                                addr, size);
                        stats.bytesReadWrQ += burst_size;
                        break;
                    }
                }
            }
        }
        // If not found in the write q, make a memory packet and
        // push it onto the read queue
        if (!foundInWrQ) {
            DPRINTF(MemCtrl, "Line %d: if not found in WrQ\n", __LINE__);
            // Make the burst helper for split packets
            if (pkt_count > 1 && burst_helper == NULL) {
                DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                        "memory requests\n", pkt->getAddr(), pkt_count);
                burst_helper = new BurstHelper(pkt_count);
            }

            MemPacket* mem_pkt;
            DPRINTF(MemCtrl, "Line %d: before decode\n", __LINE__);
            mem_pkt = mem_intr->decodePacket(pkt, addr, size, true,
                                                    mem_intr->pseudoChannel);
            mem_pkt->memoryAccess = true;
            // DPRINTF(MemCtrl, "Line %d: after decode\n", __LINE__);
            // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
            // Increment read entries of the rank (dram)
            // Increment count to trigger issue of non-deterministic read (nvm)
            mem_intr->setupRank(mem_pkt->rank, true);
            DPRINTF(MemCtrl, "Line %d: after setupRank\n", __LINE__);
            // Default readyTime to Max; will be reset once read is issued
            mem_pkt->readyTime = MaxTick;
            mem_pkt->burstHelper = burst_helper;

            // assert(!readQueueFull(1));
            DPRINTF(MemCtrl, "Line %d: before update the stats\n", __LINE__);
            // stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

            DPRINTF(MemCtrl, "Adding to read queue\n");
            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

            // log packet
            logRequest(MemCtrl::READ, pkt->requestorId(),
                    pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->readQueueSize++;

            // Update stats
            stats.avgRdQLen = totalReadQueueSize + respQueue.size();
        }

        // Starting address of next memory pkt (aligned to burst boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }


    DPRINTF(MemCtrl, "Line %d: Finish finding the packet in write queue\n", __LINE__);

    /* update the expectReadQueueSize, remove the read request found in the readQueue */
    expectReadQueueSize -= pktsServicedByWrQ;

    // If all packets are serviced by write queue, we send the repsonse back
    if (pktsServicedByWrQ == pkt_count) {
        accessAndRespondForDyL(pkt, frontendLatency, mem_intr);
        DPRINTF(MemCtrl, "Line %d\n", __LINE__);
        return true;
    }

    // Update how many split packets are serviced by write queue
    if (burst_helper != NULL)
        burst_helper->burstsServiced = pktsServicedByWrQ;

    // not all/any packets serviced by the write queue
    return false;
}

bool
MemCtrl::addToReadQueueForNew(PacketPtr pkt,
                unsigned int pkt_count, MemInterface* mem_intr) {
    assert(!pkt->isWrite());
    assert(pkt_count != 0);
    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    // Note if the pkt starting address is not aligened to burst size, the
    // address of first packet is kept unaliged. Subsequent packets
    // are aligned to burst size boundaries. This is to ensure we accurately
    // check read packets against packets in write queue.
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;

    unsigned pktsServicedByWrQ = 0;

    BurstHelper* burst_helper = NULL;

    uint32_t burst_size = mem_intr->bytesPerBurst();
    /* right now limit the burst size to be 64 */
    assert(burst_size == 64);

    /* prepare the auxiliary information */

    std::vector<uint8_t> metaData = pkt->newMetaData;

    if (pkt->newPType == 0x4) {
        /* read sub pkt */
        assert(pkt_count == 1);

        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                        base_addr + pkt->getSize()) - addr;
        stats.readPktSize[ceilLog2(size)]++;

        /* New: use the metadata to do the translation */
        PPN ospa_addr = pkt->new_origin;
        PPN ppn = (ospa_addr >> 12 & ((1ULL << 52) - 1));
        uint8_t cacheLineIdx = (ospa_addr >> 6) & 0x3F;

        // First check write buffer to see if the data is already at
        // the controller
        bool foundInWrQ = false;
        Addr burst_addr = burstAlign(addr, mem_intr);

        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
            for (const auto& vec : writeQueue) {
                for (const auto& p : vec) {
                    // check if the read is subsumed in the write queue
                    // packet we are looking at
                    if (p->addr <= addr &&
                        ((addr + size) <= (p->addr + p->size))) {

                        foundInWrQ = true;
                        stats.servicedByWrQ++;
                        pktsServicedByWrQ++;
                        DPRINTF(MemCtrl,
                                "Read to addr %#x with size %d serviced by "
                                "write queue\n",
                                addr, size);
                        stats.bytesReadWrQ += burst_size;
                        break;
                    }
                }
            }
        }

        // If not found in the write q, make a memory packet and
        // push it onto the read queue
        if (!foundInWrQ) {
            // Make the burst helper for split packets
            if (pkt_count > 1 && burst_helper == NULL) {
                DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                        "memory requests\n", pkt->getAddr(), pkt_count);
                burst_helper = new BurstHelper(pkt_count);
            }
            MemPacket* mem_pkt;
            mem_pkt = mem_intr->decodePacket(pkt, addr, size, true,
                                                    mem_intr->pseudoChannel);

            // Increment read entries of the rank (dram)
            // Increment count to trigger issue of non-deterministic read (nvm)
            mem_intr->setupRank(mem_pkt->rank, true);
            // Default readyTime to Max; will be reset once read is issued
            mem_pkt->readyTime = MaxTick;
            mem_pkt->burstHelper = burst_helper;

            // stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

            DPRINTF(MemCtrl, "Adding to read queue\n");

            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

            // log packet
            logRequest(MemCtrl::READ, pkt->requestorId(),
                    pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->readQueueSize++;

            // Update stats
            stats.avgRdQLen = totalReadQueueSize + respQueue.size();
        }

    } else if (pkt->newPType == 0x8) {
        /* read Twice */
        assert(pkt_count == 1);

        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                        base_addr + pkt->getSize()) - addr;
        stats.readPktSize[ceilLog2(size)]++;

        /* New: use the metadata to do the translation */
        PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));
        uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

        // First check write buffer to see if the data is already at
        // the controller
        bool foundInWrQ = false;
        Addr burst_addr = burstAlign(addr, mem_intr);

        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
            for (const auto& vec : writeQueue) {
                for (const auto& p : vec) {
                    // check if the read is subsumed in the write queue
                    // packet we are looking at
                    if (p->addr <= addr &&
                        ((addr + size) <= (p->addr + p->size))) {

                        foundInWrQ = true;
                        stats.servicedByWrQ++;
                        pktsServicedByWrQ++;
                        DPRINTF(MemCtrl,
                                "Read to addr %#x with size %d serviced by "
                                "write queue\n",
                                addr, size);
                        stats.bytesReadWrQ += burst_size;
                        break;
                    }
                }
            }
        }

        // If not found in the write q, make a memory packet and
        // push it onto the read queue
        if (!foundInWrQ) {
            // Make the burst helper for split packets
            if (pkt_count > 1 && burst_helper == NULL) {
                DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                        "memory requests\n", pkt->getAddr(), pkt_count);
                burst_helper = new BurstHelper(pkt_count);
            }
            MemPacket* mem_pkt;
            mem_pkt = mem_intr->decodePacket(pkt, addr, size, true,
                                                    mem_intr->pseudoChannel);

            // Increment read entries of the rank (dram)
            // Increment count to trigger issue of non-deterministic read (nvm)
            mem_intr->setupRank(mem_pkt->rank, true);
            // Default readyTime to Max; will be reset once read is issued
            mem_pkt->readyTime = MaxTick;
            mem_pkt->burstHelper = burst_helper;

            // stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

            DPRINTF(MemCtrl, "Adding to read queue\n");

            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

            // log packet
            logRequest(MemCtrl::READ, pkt->requestorId(),
                    pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->readQueueSize++;

            // Update stats
            stats.avgRdQLen = totalReadQueueSize + respQueue.size();
        }
    } else if (pkt->newPType == 0x10) {
        // readMetaData
        assert(pkt_count == 1);
        unsigned size = pkt->getSize();

        bool foundInWrQ = false;
        Addr burst_addr = burstAlign(addr, mem_intr);

        assert(burst_addr == addr);
        assert(size == burst_size);
        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
            for (const auto& vec : writeQueue) {
                for (const auto& p : vec) {
                    // check if the read is subsumed in the write queue
                    // packet we are looking at
                    if (p->addr <= addr &&
                       ((addr + size) <= (p->addr + p->size))) {
                        foundInWrQ = true;
                        pktsServicedByWrQ++;
                        break;
                    }
                }
            }
        }

        // If not found in the write q, make a memory packet and
        // push it onto the read queue
        if (!foundInWrQ) {
            MemPacket* mem_pkt;
            mem_pkt = mem_intr->decodePacket(pkt, addr, size, true,
                                                    mem_intr->pseudoChannel);
            // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
            // Increment read entries of the rank (dram)
            // Increment count to trigger issue of non-deterministic read (nvm)
            mem_intr->setupRank(mem_pkt->rank, true);
            // Default readyTime to Max; will be reset once read is issued
            mem_pkt->readyTime = MaxTick;
            mem_pkt->burstHelper = burst_helper;

            DPRINTF(MemCtrl, "Adding to read queue\n");

            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

            // log packet
            logRequest(MemCtrl::READ, pkt->requestorId(),
                       pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->readQueueSize++;

            return false;
        } else {
            // printf("Line %d, ener the access and respond for compr\n", __LINE__);
            accessAndRespondForNew(pkt, frontendLatency, mem_intr);
            return true;
        }

    } else if (pkt->newPType == 0x20) {
        /* readPage */
        panic("this type of pkt should never be assigned to the queue");
    } else if (pkt->newPType == 0x80) {
        /* readBlock */
        for (int cnt = 0; cnt < pkt_count; ++cnt) {
            unsigned size = std::min((addr | (burst_size - 1)) + 1,
                            base_addr + pkt->getSize()) - addr;

            // First check write buffer to see if the data is already at
            // the controller
            bool foundInWrQ = false;
            Addr burst_addr = burstAlign(addr, mem_intr);
            // if the burst address is not present then there is no need
            // looking any further
            if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
                for (const auto& vec : writeQueue) {
                    for (const auto& p : vec) {
                        // check if the read is subsumed in the write queue
                        // packet we are looking at
                        if (p->addr <= addr &&
                            ((addr + size) <= (p->addr + p->size))) {

                            foundInWrQ = true;
                            pktsServicedByWrQ++;
                            break;
                        }
                    }
                }
            }

            // If not found in the write q, make a memory packet and
            // push it onto the read queue
            if (!foundInWrQ) {

                // Make the burst helper for split packets
                if (pkt_count > 1 && burst_helper == NULL) {
                    DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                            "memory requests\n", pkt->getAddr(), pkt_count);
                    burst_helper = new BurstHelper(pkt_count);
                }

                MemPacket* mem_pkt;
                mem_pkt = mem_intr->decodePacket(pkt, addr, size, true,
                                                        mem_intr->pseudoChannel);
                // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
                // Increment read entries of the rank (dram)
                // Increment count to trigger issue of non-deterministic read (nvm)
                mem_intr->setupRank(mem_pkt->rank, true);
                // Default readyTime to Max; will be reset once read is issued
                mem_pkt->readyTime = MaxTick;
                mem_pkt->burstHelper = burst_helper;

                DPRINTF(MemCtrl, "Adding to read queue\n");

                readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

                // log packet
                logRequest(MemCtrl::READ, pkt->requestorId(),
                        pkt->qosValue(), mem_pkt->addr, 1);

                mem_intr->readQueueSize++;
            }

            // Starting address of next memory pkt (aligned to burst boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }

    } else {
        panic("wrong type");
    }

    // If all packets are serviced by write queue, we send the repsonse back
    if (pktsServicedByWrQ == pkt_count) {
        DPRINTF(MemCtrl, "Line %d, next enter the access and respond function\n", __LINE__);
        accessAndRespondForNew(pkt, frontendLatency, mem_intr);
        return true;
    }

    // Update how many split packets are serviced by write queue
    if (burst_helper != NULL) {
        burst_helper->burstsServiced = pktsServicedByWrQ;
        burst_helper->burstCount = pkt_count;
    }

    // not all/any packets serviced by the write queue
    return false;

}


void
MemCtrl::addToWriteQueue(PacketPtr pkt, unsigned int pkt_count,
                                MemInterface* mem_intr)
{
    // only add to the write queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(pkt->isWrite());

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;
    uint32_t burst_size = mem_intr->bytesPerBurst();

    for (int cnt = 0; cnt < pkt_count; ++cnt) {
        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                        base_addr + pkt->getSize()) - addr;
        stats.writePktSize[ceilLog2(size)]++;
        stats.writeBursts++;
        stats.requestorWriteAccesses[pkt->requestorId()]++;

        // see if we can merge with an existing item in the write
        // queue and keep track of whether we have merged or not
        bool merged = isInWriteQueue.find(burstAlign(addr, mem_intr)) !=
            isInWriteQueue.end();

        // if the item was not merged we need to create a new write
        // and enqueue it
        if (!merged) {
            MemPacket* mem_pkt;

            mem_pkt = mem_intr->decodePacket(pkt, addr, size, false,
                                                    mem_intr->pseudoChannel);
            // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
            // Default readyTime to Max if nvm interface;
            //will be reset once read is issued
            mem_pkt->readyTime = MaxTick;

            mem_intr->setupRank(mem_pkt->rank, false);

            assert(totalWriteQueueSize < writeBufferSize);
            stats.wrQLenPdf[totalWriteQueueSize]++;

            DPRINTF(MemCtrl, "Adding to write queue\n");

            writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
            isInWriteQueue.insert(burstAlign(addr, mem_intr));

            // log packet
            logRequest(MemCtrl::WRITE, pkt->requestorId(),
                       pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->writeQueueSize++;

            assert(totalWriteQueueSize == isInWriteQueue.size());

            // Update stats
            stats.avgWrQLen = totalWriteQueueSize;

        } else {
            DPRINTF(MemCtrl,
                    "Merging write burst with existing queue entry\n");

            // keep track of the fact that this burst effectively
            // disappeared as it was merged with an existing one
            stats.mergedWrBursts++;
        }

        // Starting address of next memory pkt (aligned to burst_size boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }

    // we do not wait for the writes to be send to the actual memory,
    // but instead take responsibility for the consistency here and
    // snoop the write queue for any upcoming reads
    // @todo, if a pkt size is larger than burst size, we might need a
    // different front end latency
    accessAndRespond(pkt, frontendLatency, mem_intr);
}

bool
MemCtrl::addToWriteQueueForCompr(PacketPtr pkt, unsigned int pkt_count,
                                MemInterface* mem_intr)
{
    // only add to the write queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(pkt->isWrite());
    assert(pkt->getPType() != 1);

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;
    uint32_t burst_size = mem_intr->bytesPerBurst();
    assert(burst_size == 64);

    /* prepare the auxiliary information */

    std::vector<uint8_t> sizeMap = {0, 8, 32, 64};

    std::unordered_map<uint64_t, std::vector<uint8_t>> metaDataMap = pkt->comprMetaDataMap;

    Addr addrAligned = (base_addr >> 6) << 6;

    uint64_t new_size = ((((base_addr + pkt->getSize()) + (burst_size - 1)) >> 6) << 6) - addrAligned;

    std::vector<uint8_t> newData(new_size, 0);

    if (pkt->getPType() == 0x2) {

        bool pageOverFlow = false;
        PPN overflowPageNum = 0;
        std::unordered_set<PPN> updatedMetaData;

        /*
         * first iteration
         * update the metadata
         * check if pageOverFlow
        */
        for (int cnt = 0; cnt < pkt_count; ++cnt) {
            unsigned size = std::min((addr | (burst_size - 1)) + 1,
                            base_addr + pkt->getSize()) - addr;
            stats.writePktSize[ceilLog2(size)]++;

            /* Compr: use the metadata to do the translation */
            PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));
            uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

            assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */
            std::vector<uint8_t> metaData = metaDataMap[ppn];

            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                printf("ppn is: %ld\n", ppn);
                printf("the metadata is :\n");
                for (int k = 0; k < 64; k++) {
                    printf("%02x",static_cast<unsigned>(metaData[k]));

                }
                printf("\n");
            }

            bool crossBoundary = false;
            Addr real_addr = addr;
            unsigned real_size = size;
            std::vector<uint8_t> cacheLine(64, 0);

            if (ppn == pageNum) {
                /* hit in the page buffer */
                if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                    printf("add to write queue: hit in page buffer\n");
                }
                stats.writeBursts++;
                stats.requestorWriteAccesses[pkt->requestorId()]++;

                /* update the metadata if necessary */
                uint8_t type = getType(metaData, cacheLineIdx);
                // for (unsigned int j = 0; j < sizeMap[type]; j++) {
                //     cacheLine[j] = pageBuffer[64 * cacheLineIdx + j];
                // }
                mem_intr->atomicRead(cacheLine.data(), pageBufferAddr + 64 * cacheLineIdx, 64);

                restoreData(cacheLine, type);
                assert(cacheLine.size() == 64);

                /* write the data */
                uint64_t ofs = addr - base_addr;
                uint8_t loc = addr & 0x3F;
                size_t writeSize = std::min(64UL - loc, pkt->getSize() - ofs);

                pkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

                std::vector<uint8_t> compressed = compressForCompr(cacheLine);

                /*write back to pageBuffer and update the metadata if necessary */
                if (isAllZero(cacheLine)) {
                    /* set the metadata entry to be 0 */
                    DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
                    setType(metaData, cacheLineIdx, 0);
                } else {
                    if (compressed.size() <= 8) {
                        /* set the metadata entry to be 0b1*/
                        DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
                        setType(metaData, cacheLineIdx, 0b01);
                    } else if (compressed.size() <= 32) {
                        DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
                        setType(metaData, cacheLineIdx, 0b10);
                    } else {
                        /* set to be 0b11 */
                        DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
                        setType(metaData, cacheLineIdx, 0b11);
                    }
                }
                metaDataMap[ppn] = metaData;
                updatedMetaData.insert(ppn);
            } else {
                uint8_t type = getType(metaData, cacheLineIdx);
                uint8_t old_type = type;
                std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);

                bool inInflate = cLStatus.first;
                real_addr = cLStatus.second;

                if (inInflate) {
                    mem_intr->atomicRead(cacheLine.data(), real_addr, 64);
                    type = 0b11;
                } else {
                    if (type != 0) {
                        mem_intr->atomicRead(cacheLine.data(), real_addr, sizeMap[type]);
                    }
                }
                // printf("2: get real size\n");
                real_size = sizeMap[type];
                // printf("2: restore data\n");
                restoreData(cacheLine, type);

                if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                    printf("the restored(original) cacheline value is :\n");
                    for (int qw = 0; qw < 8; qw++) {
                    for (int er = 0; er < 8; er++) {
                        printf("%02x ", cacheLine[qw* 8 + er]);
                    }
                    printf("\n");
                    }
                    printf("\n");
                }

                /* write the data */
                uint64_t ofs = addr - base_addr;
                uint8_t loc = addr & 0x3F;
                size_t writeSize = std::min(64UL - loc, pkt->getSize() - ofs);

                // printf("the pkt data is :\n");
                // uint8_t* test_start = pkt->getPtr<uint8_t>();
                // for (int zx = 0; zx < pkt->getSize(); zx++) {
                //     if (zx % 8 == 0) {
                //         printf("\n");
                //     }
                //     printf("%02x ", test_start[zx]);

                // }


                // printf("the backup data is :\n");
                // test_start = pkt->comprBackup->getPtr<uint8_t>();
                // for (int zx = 0; zx < pkt->comprBackup->getSize(); zx++) {
                //     if (zx % 8 == 0) {
                //         printf("\n");
                //     }
                //     printf("%02x ", test_start[zx]);
                // }

                // printf("2: write data \n");
                // fflush(stdout);
                pkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

                // if (pkt->getAddr() == 0xe74d8) {
                //     printf("the updated cacheline value is :\n");
                //     for (int qw = 0; qw < 8; qw++) {
                //        for (int er = 0; er < 8; er++) {
                //            printf("%02x ", cacheLine[qw* 8 + er]);
                //        }
                //        printf("\n");
                //     }
                //     printf("\n");
                // }


                // printf("the updated cacheline value is :\n");
                // for (int qw = 0; qw < 8; qw++) {
                //    for (int er = 0; er < 8; er++) {
                //        printf("%02x ", cacheLine[qw* 8 + er]);
                //    }
                //    printf("\n");
                // }
                // printf("\n");

                // printf("2: start to compress\n");
                // fflush(stdout);
                std::vector<uint8_t> compressed = compressForCompr(cacheLine);
                // printf("after compress, the updated cacheline value is :\n");
                // for (int qw = 0; qw < compressed.size(); qw++) {
                //     if (qw % 8 == 0) {
                //         printf("\n");
                //     }
                //     printf("%02x ", cacheLine[qw]);
                // }
                // printf("\n");

                if (compressed.size() > 32) {
                    assert(compressed.size() == 64);
                }

                /* update the metadata if necessary */
                if (inInflate) {
                    /* check if we could write back */
                    if (compressed.size() <= sizeMap[old_type]) {
                        DPRINTF(MemCtrl, "underflow, write back to the oirginal space\n");
                        /* TODO: this may also cost some time, right now just make it atomic */
                        real_addr = moveForwardAtomic(metaData, cacheLineIdx, mem_intr, isAddressCovered(pkt->getAddr(), pkt->getSize(), 1));
                        real_size = sizeMap[old_type];
                        updatedMetaData.insert(ppn);
                        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                            printf("underflow\n");
                        }
                    } else {
                        /* do nothing */
                    }
                } else {
                    if (compressed.size() <= sizeMap[old_type]) {
                        /* if not overflow */
                        /* do nothing */
                    } else {
                        DPRINTF(MemCtrl, "Line %d: the cacheline overflow \n", __LINE__);
                        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                            printf("cacheline overflow\n");
                        }
                        // printf("the pageNum is %d\n", ppn);
                        // printf("the metadata is :\n");\
                        // for (int k = 0; k < 64; k++) {
                        //     printf("%02x",static_cast<unsigned>(metaData[k]));

                        // }
                        // printf("\n");

                        if (hasFreeInflateRoom(metaData)) {
                            Addr inflatedAddr = allocateInflateRoom(metaData, cacheLineIdx);
                            // printf("now the new metadata is :\n");\
                            // for (int k = 0; k < 64; k++) {
                            //     printf("%02x",static_cast<unsigned>(metaData[k]));
                            // }
                            // printf("\n");
                            real_addr = inflatedAddr;
                            real_size = 64;
                            updatedMetaData.insert(ppn);

                            // printf("the new metadata is :\n");
                            // for (int k = 0; k < 64; k++) {
                            //     printf("%02x",static_cast<unsigned>(metaData[k]));

                            // }
                            // printf("\n");
                        } else {
                            /* deal with page overflow */
                            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                                printf("Opps, we have page overflow\n");
                            }
                            overflowPageNum = ppn;
                            pageOverFlow = true;
                            break;
                        }
                    }
                }
                metaDataMap[ppn] = metaData;
                if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                    printf("the new metadata is :\n");
                    for (int k = 0; k < 64; k++) {
                        printf("%02x",static_cast<unsigned>(metaData[k]));

                    }
                    printf("\n");
                }
            }

            for (unsigned int j = 0; j < 64; j++) {
                newData[cnt * 64 + j] = cacheLine[j];
            }
            // Starting address of next memory pkt (aligned to burst_size boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }
        if (pageOverFlow) {
            DPRINTF(MemCtrl, "Compresso: pageoverflow\n");
            assert(blockedForCompr == false);
            blockedForCompr = true;
            PacketPtr readForCompress = new Packet(pkt, pkt->comprTick - 1);
            pkt->ref_cnt++;

            typeOneBlock += 1;
            readForCompress->configAsReadForCompress(pkt->comprMetaDataMap[overflowPageNum], overflowPageNum);

            DPRINTF(MemCtrl, "(pkt) Line %d, the readForCompress pkt %lx\n", __LINE__, readForCompress);

            waitQueue.insert(readForCompress);
            readForCompress->ref_cnt++;

            assignToQueue(readForCompress);
            return false;

        } else {
            pkt->comprMetaDataMap = metaDataMap;
            //update the new metadata for the following pkt in waitQueue
            for (const auto& ppn: updatedMetaData) {
                DPRINTF(MemCtrl, "Line %d, enter the update subseqMetadata\n", __LINE__);
                updateSubseqMetaData(pkt, ppn);
                // printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, ppn, ppn * 64);
                mcache.add(ppn * 64, metaDataMap[ppn]);
                // if (ppn != pageNum) {
                //     mem_intr->atomicWrite(metaDataMap[ppn], ppn * 64, 64, 0);
                // } else {
                //     mPageBuffer = metaDataMap[ppn];
                // }
                mem_intr->atomicWrite(metaDataMap[ppn], ppn * 64, 64, 0);
            }

            addr = base_addr;
            for (int cnt = 0; cnt < pkt_count; ++cnt) {
                /* Compr: use the metadata to do the translation */
                PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));
                uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

                Addr metadata_addr = ppn * 64;
                std::vector<uint8_t> metaData = pkt->comprMetaDataMap[ppn];

                // printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, ppn, metadata_addr);
                mcache.add(metadata_addr, metaData);
                //TODO2 create a write metadata to add to WriteQueue
                mem_intr->atomicWrite(metaData, metadata_addr, 64, 0);

                if (pageNum != ppn) {
                    uint8_t type = getType(metaData, cacheLineIdx);
                    std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);

                    Addr real_addr = cLStatus.second;
                    unsigned real_size = (cLStatus.first)?64:sizeMap[type];

                    if (real_size > 0) {
                        /* only do the memory access if the cacheline is not zero */
                        /* now we have the real_addr, real_size, want to add the pkt to the write queue */
                        Addr addrAligned = burstAlign(real_addr, mem_intr);
                        Addr end_addr = real_addr + real_size - 1;
                        Addr endAddrAligned = burstAlign(end_addr, mem_intr);

                        if (addrAligned != endAddrAligned) {
                            /* the compressed cacheline cross the boundary */
                            assert(endAddrAligned - addrAligned == burst_size);
                            stats.writeBursts += 2;
                            // printf("pkt->requestorId is %d\n", pkt->requestorId());
                            stats.requestorWriteAccesses[pkt->requestorId()] += 2;

                            unsigned prefix = (real_addr | (burst_size - 1)) + 1 - real_addr;
                            assert(prefix < real_size);
                            unsigned suffix = real_size - prefix;
                            std::vector<unsigned> memPktLen = {prefix, suffix};
                            std::vector<Addr> memPktAddr = {real_addr, (real_addr | (burst_size - 1)) + 1};
                            assert(memPktAddr[1] = endAddrAligned);

                            for (int j = 0 ; j < 2; j++) {
                                bool merged = isInWriteQueue.find(burstAlign(memPktAddr[j], mem_intr)) !=
                                    isInWriteQueue.end();

                                // if the item was not merged we need to create a new write
                                // and enqueue it
                                if (!merged) {
                                    MemPacket* mem_pkt;
                                    mem_pkt = mem_intr->decodePacket(pkt, memPktAddr[j], memPktLen[j], false,
                                                                            mem_intr->pseudoChannel);
                                    // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
                                    // Default readyTime to Max if nvm interface;
                                    //will be reset once read is issued
                                    mem_pkt->readyTime = MaxTick;

                                    mem_intr->setupRank(mem_pkt->rank, false);

                                    // stats.wrQLenPdf[totalWriteQueueSize]++;

                                    // DPRINTF(MemCtrl, "Line %d: Adding to write queue\n", __LINE__);
                                    // printf("**********************Line %d: push back write queue, address is %lx\n", __LINE__, mem_pkt->pkt->comprBackup);
                                    writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
                                    isInWriteQueue.insert(burstAlign(addr, mem_intr));

                                    // log packet
                                    logRequest(MemCtrl::WRITE, pkt->requestorId(),
                                            pkt->qosValue(), mem_pkt->addr, 1);

                                    mem_intr->writeQueueSize++;

                                    // assert(totalWriteQueueSize == isInWriteQueue.size());

                                    // Update stats
                                    stats.avgWrQLen = totalWriteQueueSize;

                                } else {
                                    DPRINTF(MemCtrl,
                                            "Merging write burst with existing queue entry\n");

                                    // keep track of the fact that this burst effectively
                                    // disappeared as it was merged with an existing one
                                    stats.mergedWrBursts++;
                                }
                            }

                        } else {
                            stats.writeBursts++;
                            // printf("pkt->requestorId is %d\n", pkt->requestorId());
                            stats.requestorWriteAccesses[pkt->requestorId()]++;
                            // see if we can merge with an existing item in the write
                            // queue and keep track of whether we have merged or not
                            bool merged = isInWriteQueue.find(burstAlign(real_addr, mem_intr)) !=
                                isInWriteQueue.end();

                            // if the item was not merged we need to create a new write
                            // and enqueue it
                            if (!merged) {
                                MemPacket* mem_pkt;
                                mem_pkt = mem_intr->decodePacket(pkt, real_addr, real_size, false,
                                                                        mem_intr->pseudoChannel);
                                // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
                                // Default readyTime to Max if nvm interface;
                                //will be reset once read is issued
                                mem_pkt->readyTime = MaxTick;

                                mem_intr->setupRank(mem_pkt->rank, false);

                                // printf("totalWriteQueuesize %lld, writeBufferSize %lld\n", totalWriteQueueSize, writeBufferSize);
                                // stats.wrQLenPdf[totalWriteQueueSize]++;

                                DPRINTF(MemCtrl, "Line %d: Adding to write queue\n", __LINE__);

                                // printf("**********************Line %d: push back write queue, address is %lx\n", __LINE__, mem_pkt->pkt->comprBackup);
                                writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
                                isInWriteQueue.insert(burstAlign(addr, mem_intr));

                                // log packet
                                logRequest(MemCtrl::WRITE, pkt->requestorId(),
                                        pkt->qosValue(), mem_pkt->addr, 1);

                                mem_intr->writeQueueSize++;

                                // assert(totalWriteQueueSize == isInWriteQueue.size());

                                // Update stats
                                stats.avgWrQLen = totalWriteQueueSize;

                            } else {
                                DPRINTF(MemCtrl,
                                        "Merging write burst with existing queue entry\n");

                                // keep track of the fact that this burst effectively
                                // disappeared as it was merged with an existing one
                                stats.mergedWrBursts++;
                            }
                        }
                    }
                }
                // Starting address of next memory pkt (aligned to burst_size boundary)
                addr = (addr | (burst_size - 1)) + 1;
            }

            assert(new_size % 64 == 0);

            // printf("the changed newdata is :\n");
            // for (int qw = 0; qw < newData.size(); qw++) {
            //     if (qw % 8 == 0) {
            //         printf("\n");
            //     }
            //     printf("%02x ", newData[qw]);
            // }
            // printf("\n");

            pkt->setAddr(addrAligned);
            pkt->setSizeForMC(new_size);
            pkt->allocateForMC();
            pkt->setDataForMC(newData.data(), 0, new_size);
        }

    } else if (pkt->getPType() == 0x8) {
        /* pkt is writeMetaData */
        assert(pkt->getSize() == 64);
        assert(pkt->getAddr() % 64 == 0);

        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                            base_addr + pkt->getSize()) - addr;
        assert(size == 64);


        // see if we can merge with an existing item in the write
        // queue and keep track of whether we have merged or not
        bool merged = isInWriteQueue.find(burstAlign(addr, mem_intr)) !=
            isInWriteQueue.end();

        // if the item was not merged we need to create a new write
        // and enqueue it
        if (!merged) {
            MemPacket* mem_pkt;

            mem_pkt = mem_intr->decodePacket(pkt, addr, size, false,
                                                    mem_intr->pseudoChannel);
            // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
            // Default readyTime to Max if nvm interface;
            //will be reset once read is issued
            mem_pkt->readyTime = MaxTick;

            mem_intr->setupRank(mem_pkt->rank, false);

            DPRINTF(MemCtrl, "Line %d: Adding to write queue\n", __LINE__);
            // printf("**********************Line %d: push back write queue, address is %lx\n", __LINE__, mem_pkt->pkt->comprBackup);
            writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
            isInWriteQueue.insert(burstAlign(addr, mem_intr));

            // log packet
            logRequest(MemCtrl::WRITE, pkt->requestorId(),
                       pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->writeQueueSize++;

            // assert(totalWriteQueueSize == isInWriteQueue.size());

        } else {
            DPRINTF(MemCtrl,
                    "Merging write burst with existing queue entry\n");

            // keep track of the fact that this burst effectively
            // disappeared as it was merged with an existing one
        }
        pkt->writeDataForMC(reinterpret_cast<uint8_t*>(pkt->getAddr()), 0, 64);
        return true;
    } else if (pkt->getPType() == 0x20){
        /* pkt is writeForCompress */
        assert(isEligible(pkt));
        // printf("the pkt is 0x%lx\n", reinterpret_cast<unsigned long>(pkt));
        fflush(stdout);

        assert(blockedForCompr);

        assert((pkt->getAddr() & 0xFFF) == 0);
        assert(pkt->getSize() == 4096);
        assert(pkt_count == 64);

        for (int cnt = 0; cnt < pkt_count; ++cnt) {
            /* Compr: use the metadata to do the translation */
            PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));
            uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

            assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */
            std::vector<uint8_t> metaData = metaDataMap[ppn];

            bool crossBoundary = false;
            std::vector<uint8_t> cacheLine(64, 0);

            uint8_t type = getType(metaData, cacheLineIdx);
            std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);

            bool inInflate = cLStatus.first;
            Addr real_addr = cLStatus.second;
            unsigned real_size = sizeMap[type];

            assert(inInflate == false);

            if (real_size > 0) {
                DPRINTF(MemCtrl, "real_addr: %lx\n", real_addr);

                /* now we have the real_addr, real_size, want to add the pkt to the write queue */
                Addr addrAligned = burstAlign(real_addr, mem_intr);
                Addr end_addr = real_addr + real_size - 1;

                DPRINTF(MemCtrl, "end_addr: %lx\n", end_addr);
                Addr endAddrAligned = burstAlign(end_addr, mem_intr);

                if (addrAligned != endAddrAligned) {
                    DPRINTF(MemCtrl, "endAddrAligned is %lx, addrAligned is %lx\n", endAddrAligned, addrAligned);
                    /* the compressed cacheline cross the boundary */
                    assert(endAddrAligned - addrAligned == burst_size);

                    unsigned prefix = (real_addr | (burst_size - 1)) + 1 - real_addr;
                    assert(prefix < real_size);
                    unsigned suffix = real_size - prefix;
                    std::vector<unsigned> memPktLen = {prefix, suffix};
                    std::vector<Addr> memPktAddr = {real_addr, (real_addr | (burst_size - 1)) + 1};
                    assert(memPktAddr[1] == endAddrAligned);

                    for (int j = 0 ; j < 2; j++) {
                        bool merged = isInWriteQueue.find(burstAlign(memPktAddr[j], mem_intr)) !=
                            isInWriteQueue.end();

                        // if the item was not merged we need to create a new write
                        // and enqueue it
                        if (!merged) {
                            MemPacket* mem_pkt;
                            mem_pkt = mem_intr->decodePacket(pkt, memPktAddr[j], memPktLen[j], false,
                                                                    mem_intr->pseudoChannel);
                            // Default readyTime to Max if nvm interface;
                            //will be reset once read is issued
                            mem_pkt->readyTime = MaxTick;

                            mem_intr->setupRank(mem_pkt->rank, false);

                            DPRINTF(MemCtrl, "Line %d: Adding to write queue\n", __LINE__);
                            // printf("**********************Line %d: push back write queue, address is %lx\n", __LINE__, mem_pkt->pkt->comprBackup);
                            writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
                            isInWriteQueue.insert(burstAlign(addr, mem_intr));

                            // log packet
                            logRequest(MemCtrl::WRITE, pkt->requestorId(),
                                    pkt->qosValue(), mem_pkt->addr, 1);

                            mem_intr->writeQueueSize++;

                            // assert(totalWriteQueueSize == isInWriteQueue.size());

                        } else {
                            DPRINTF(MemCtrl,
                                    "Merging write burst with existing queue entry\n");

                            // keep track of the fact that this burst effectively
                            // disappeared as it was merged with an existing one
                        }
                    }

                } else {
                    // see if we can merge with an existing item in the write
                    // queue and keep track of whether we have merged or not
                    bool merged = isInWriteQueue.find(burstAlign(real_addr, mem_intr)) !=
                        isInWriteQueue.end();

                    // if the item was not merged we need to create a new write
                    // and enqueue it
                    if (!merged) {
                        MemPacket* mem_pkt;
                        mem_pkt = mem_intr->decodePacket(pkt, real_addr, real_size, false,
                                                                mem_intr->pseudoChannel);
                        // Default readyTime to Max if nvm interface;
                        //will be reset once read is issued
                        mem_pkt->readyTime = MaxTick;

                        mem_intr->setupRank(mem_pkt->rank, false);

                        DPRINTF(MemCtrl, "Line %d: Adding to write queue\n", __LINE__);
                        // printf("**********************Line %d: push back write queue, address is %lx\n", __LINE__, mem_pkt->pkt->comprBackup);
                        writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
                        isInWriteQueue.insert(burstAlign(addr, mem_intr));

                        // log packet
                        logRequest(MemCtrl::WRITE, pkt->requestorId(),
                                pkt->qosValue(), mem_pkt->addr, 1);

                        mem_intr->writeQueueSize++;

                        // assert(totalWriteQueueSize == isInWriteQueue.size());

                    } else {
                        DPRINTF(MemCtrl,
                                "Merging write burst with existing queue entry\n");

                        // keep track of the fact that this burst effectively
                        // disappeared as it was merged with an existing one
                    }
                }
            }
            addr = (addr | (burst_size - 1)) + 1;
        }

    } else {
        panic("unknown pkt type");
    }

    // we do not wait for the writes to be send to the actual memory,
    // but instead take responsibility for the consistency here and
    // snoop the write queue for any upcoming reads
    // @todo, if a pkt size is larger than burst size, we might need a
    // different front end latency
    // printf("Line %d, ener the access and respond for compr\n", __LINE__);
    DPRINTF(MemCtrl, "Line %d, next enter the access and respond function\n", __LINE__);
    accessAndRespondForCompr(pkt, frontendLatency, mem_intr);
    DPRINTF(MemCtrl, "Line %d, finish access and respond\n", __LINE__);
    return true;
}

void
MemCtrl::addToWriteQueueForDyL(PacketPtr pkt, unsigned int pkt_count,
                                MemInterface* mem_intr) {
    // only add to the write queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    // printf("enter the add to write queue, pkt is 0x%lx\n", reinterpret_cast<unsigned long>(pkt));
    assert(pkt->isWrite());
    assert(pkt->DyLPType != 0x1);

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;
    uint32_t burst_size = mem_intr->bytesPerBurst();

    /* If the pkt is not auxPkt */
    if (pkt->DyLPType != 0x2) {
        DPRINTF(MemCtrl, "Line %d: Enter the add-to-write-queue special part\n", __LINE__);
        for (int cnt = 0; cnt < pkt_count; ++cnt) {
            unsigned size = std::min((addr | (burst_size - 1)) + 1,
                            base_addr + pkt->getSize()) - addr;

            // see if we can merge with an existing item in the write
            // queue and keep track of whether we have merged or not
            bool merged = isInWriteQueue.find(burstAlign(addr, mem_intr)) !=
                isInWriteQueue.end();

            // if the item was not merged we need to create a new write
            // and enqueue it
            if (!merged) {
                MemPacket* mem_pkt;
                mem_pkt = mem_intr->decodePacket(pkt, addr, size, false,
                                                        mem_intr->pseudoChannel);
                // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
                // Default readyTime to Max if nvm interface;
                // will be reset once read is issued
                mem_pkt->readyTime = MaxTick;

                mem_intr->setupRank(mem_pkt->rank, false);

                DPRINTF(MemCtrl, "Line %d: Adding to write queue\n", __LINE__);

                writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
                isInWriteQueue.insert(burstAlign(addr, mem_intr));

                // log packet
                logRequest(MemCtrl::WRITE, pkt->requestorId(),
                        pkt->qosValue(), mem_pkt->addr, 1);

                mem_intr->writeQueueSize++;

            } else {
                DPRINTF(MemCtrl,
                        "Line %d: Merging write burst with existing queue entry\n", __LINE__);
            }

            // Starting address of next memory pkt (aligned to burst_size boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }

        // we do not wait for the writes to be send to the actual memory,
        // but instead take responsibility for the consistency here and
        // snoop the write queue for any upcoming reads
        // @todo, if a pkt size is larger than burst size, we might need a
        // different front end latency
        std::vector<uint8_t> data(pkt->getSize());
        pkt->writeDataForMC(data.data(), 0, pkt->getSize());
        // if (pkt->pType == 0x40) {   // write CTE
        //     DPRINTF(MemCtrl, "Line %d: The address is 0x%llx\n", __LINE__, pkt->getAddr());
        //     for (int i = 0; i < pkt->getSize(); i++) {
        //         DPRINTF(MemCtrl, "data[%d] = 0x%llx\n", i, data[i]);
        //     }
        // }
        if (coverageTestMC(pkt->getAddr(), 0x198662, data.size())) {
            printf("Line %d: the addr is 0x%lx, size is %d\n", __LINE__, pkt->getAddr(), data.size());
            printf("pkt type %d, pkt address is 0x%lx\n", pkt->DyLPType, reinterpret_cast<unsigned long>(pkt));
        }

        mem_intr->atomicWrite(data, pkt->getAddr(), data.size());
        if (pkt->DyLPType == 0x8) {    // after writeUncompressed, issue the real memory request
            DPRINTF(MemCtrl, "Line %d: current pkt is writeUncompress\n", __LINE__);
            PacketPtr origin_pkt = pkt->DyLCandidate;
            PPN origin_ppn = (origin_pkt->DyLBackup) >> 12;
            if (pagesInDecompress.find(origin_ppn) == pagesInDecompress.end()) {
                printf("the address of origin_pkt is 0x%lx\n", reinterpret_cast<unsigned long>(origin_pkt));
                printf("error: find origin ppn %d not in Decompress\n", origin_ppn);
                panic("Fetal Error");
            }
            pagesInDecompress.erase(origin_ppn);

            // printf("the size of waitForDecompress is %d\n", waitForDeCompress.size());
            std::vector<PacketPtr> pktToErase;
            for(auto const& aux_pkt: waitForDeCompress) {
                assert(aux_pkt->getAddr() == aux_pkt->DyLBackup);
                PPN ppn = (aux_pkt->DyLBackup) >> 12;
                if (ppn != origin_ppn) {
                    continue;
                } else {
                    pktToErase.emplace_back(aux_pkt);
                }
                /* should be conflicted with pageInDecompress before otherwise the pkt should be already proceed when the writeForCompress is finished */
                // uint8_t accessData[8];
                // Addr cteAddr = startAddrForCTE + ppn * 8;
                // dram->atomicRead(accessData, cteAddr, 8);
                // uint64_t real_cte = 0;
                // for (int i = 0; i < 8; i++) {
                //     real_cte = (real_cte << 8) | (accessData[i] & 0xFF);
                // }
                // assert(((real_cte >> 62) & 0x1) ==  0);

                bool sign = false;
                unsigned size = aux_pkt->getSize();
                uint32_t burst_size = dram->bytesPerBurst();

                unsigned offset = aux_pkt->getAddr() & (burst_size - 1);
                unsigned int pkt_count = divCeil(offset + size, burst_size);
                // assert(pageInProcess.find(ppn) != pageInProcess.end());

                /* don't need to decompress anymore */
                if (aux_pkt->isWrite()) {
                    Addr addr = pkt->getAddr();
                    Addr realAddr = addr | (aux_pkt->getAddr() & ((1ULL << 12) - 1));
                    aux_pkt->setAddr(realAddr);

                    addToWriteQueueForDyL(aux_pkt, pkt_count, dram);
                    stats.writeReqs++;
                    stats.bytesWrittenSys += size;

                    // If we are not already scheduled to get a request out of the
                    // queue, do so now
                    if (!sign) {
                        if (!nextReqEvent.scheduled()) {
                            DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
                            schedule(nextReqEvent, curTick());
                        }
                    }
                } else {
                    assert(aux_pkt->isRead());
                    Addr addr = pkt->getAddr();
                    Addr realAddr = addr | (aux_pkt->getAddr() & ((1ULL << 12) - 1));
                    aux_pkt->setAddr(realAddr);
                    sign = addToReadQueueForDyL(aux_pkt, pkt_count, dram);
                    stats.readReqs++;
                    stats.bytesReadSys += size;

                    if (!sign) {
                        // If we are not already scheduled to get a request out of the
                        // queue, do so now
                        if (!nextReqEvent.scheduled()) {
                            DPRINTF(MemCtrl, "Request scheduled immediately\n");
                            schedule(nextReqEvent, curTick());
                        }
                    }
                }
            }


            for (auto &pkt : pktToErase) {
                waitForDeCompress.remove(pkt);
            }

            /* translate the dram address */
            Addr addr = pkt->getAddr();
            Addr real_addr = addr | (origin_pkt->getAddr() & ((1ULL << 12) - 1));
            bool sign = false;

            origin_pkt->setAddr(real_addr);

            unsigned real_size = origin_pkt->getSize();

            unsigned real_offset = pkt->getAddr() & (burst_size - 1);
            unsigned int real_pkt_count = divCeil(real_offset + real_size, burst_size);

            if (origin_pkt->isRead()) {
                sign = addToReadQueueForDyL(origin_pkt, real_pkt_count, dram);
                stats.readReqs++;
                stats.bytesReadSys += real_size;
                if (!sign) {
                    // If we are not already scheduled to get a request out of the
                    // queue, do so now
                    if (!nextReqEvent.scheduled()) {
                        DPRINTF(MemCtrl, "Request scheduled immediately\n");
                        schedule(nextReqEvent, curTick());
                    }
                }
            } else {
                addToWriteQueueForDyL(origin_pkt, real_pkt_count, dram);
                stats.writeReqs++;
                stats.bytesWrittenSys += real_size;
                if (!sign) {
                    if (!nextReqEvent.scheduled()) {
                        DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
                        schedule(nextReqEvent, curTick());
                    }
                }
            }

        } else if (pkt->DyLPType == 0x20) {   // writeCompressed
            /* do nothing */
        } else if (pkt->DyLPType == 0x80) {
            // writeCTE
            /* do nothing */
        } else {
            panic("The pkt should not have another (write) type\n");
        }
        delete pkt;
        return;
    }

    for (int cnt = 0; cnt < pkt_count; ++cnt) {
        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                        base_addr + pkt->getSize()) - addr;
        stats.writePktSize[ceilLog2(size)]++;
        stats.writeBursts++;
        stats.requestorWriteAccesses[pkt->requestorId()]++;

        // see if we can merge with an existing item in the write
        // queue and keep track of whether we have merged or not
        bool merged = isInWriteQueue.find(burstAlign(addr, mem_intr)) !=
            isInWriteQueue.end();

        // if the item was not merged we need to create a new write
        // and enqueue it
        if (!merged) {
            MemPacket* mem_pkt;
            mem_pkt = mem_intr->decodePacket(pkt, addr, size, false,
                                                    mem_intr->pseudoChannel);
            mem_pkt->memoryAccess = true;
            // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
            // Default readyTime to Max if nvm interface;
            //will be reset once read is issued
            mem_pkt->readyTime = MaxTick;

            mem_intr->setupRank(mem_pkt->rank, false);

            // assert(totalWriteQueueSize < writeBufferSize);  // in the new scenario, this is not always true
            // stats.wrQLenPdf[totalWriteQueueSize]++;

            DPRINTF(MemCtrl, "Adding to write queue\n");

            writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
            isInWriteQueue.insert(burstAlign(addr, mem_intr));

            // log packet
            logRequest(MemCtrl::WRITE, pkt->requestorId(),
                    pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->writeQueueSize++;
            expectWriteQueueSize++;

            assert(totalWriteQueueSize == isInWriteQueue.size());

            // Update stats
            stats.avgWrQLen = totalWriteQueueSize;

        } else {
            DPRINTF(MemCtrl,
                    "Merging write burst with existing queue entry\n");

            // keep track of the fact that this burst effectively
            // disappeared as it was merged with an existing one
            stats.mergedWrBursts++;
        }

        // Starting address of next memory pkt (aligned to burst_size boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }

    // we do not wait for the writes to be send to the actual memory,
    // but instead take responsibility for the consistency here and
    // snoop the write queue for any upcoming reads
    // @todo, if a pkt size is larger than burst size, we might need a
    // different front end latency
    accessAndRespondForDyL(pkt, frontendLatency, mem_intr);
}


bool
MemCtrl::addToWriteQueueForNew(PacketPtr pkt, unsigned int pkt_count,
                                MemInterface* mem_intr) {
    assert(pkt->isWrite());

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;
    uint32_t burst_size = mem_intr->bytesPerBurst();
    assert(burst_size == 64);

    /* prepare the auxiliary information */

    std::vector<uint8_t> metaData = pkt->newMetaData;
    // assert(metaData.size() == 64);
    // printf("metaData.data 0x%lx\n", metaData.data());

    Addr addrAligned = burstAlign(base_addr, mem_intr);

    uint64_t new_size = ((((base_addr + pkt->getSize()) + (burst_size - 1)) >> 6) << 6) - addrAligned;

    if (pkt->newPType == 0x4) {
        /* write sub-pkt*/
        Addr origin_addr = pkt->new_origin;
        uint8_t cacheLineIdx = (origin_addr >> 6) & 0x3F;
        uint8_t type = new_getType(metaData, cacheLineIdx);

        if (type >= 0b100) {
            /* the real data is in the overflow region */

            assert(pkt->getSize() == 64);
            uint8_t overflowIdx = 0;

            mem_intr->atomicRead(&overflowIdx, addr, 1);

            Addr real_addr = calOverflowAddr(metaData, overflowIdx);

            if (isAddressCovered(pkt->new_backup->getAddr(), pkt->new_backup->getSize(), 1)) {
                printf("the data is in the overflow region\n");
                printf("the mpa addr is 0x%lx\n", addr);
                printf("overflowIdx is %d\n", overflowIdx);
                printf("the real addr is 0x%lx\n", real_addr);
            }

            // first issue write command to the addr in the data region
            issueWriteCmdForNew(pkt, addr, 1, mem_intr);

            // second issue write command to real_addr
            assert(real_addr % 64 == 0);
            issueWriteCmdForNew(pkt, real_addr, 64, mem_intr);
            pkt->setAddr(real_addr); // set the real address

        } else {
            /* the data is in the original space */
            Addr addr_aligned = burstAlign(addr, mem_intr);
            assert(burst_size == 64);
            uint64_t curSize = (addr_aligned + burst_size) - addr;
            if(sizeMap[type] <= curSize) {
                assert(pkt->suffixLen == 0);
                // first issue write command to origin_addr
                issueWriteCmdForNew(pkt, origin_addr, 1, mem_intr);

                // second issue write command to zeroAddr
                issueWriteCmdForNew(pkt, zeroAddr, 1, mem_intr);
            } else {
                // the cacheline is across the boundary
                // first issue write command to origin_addr
                issueWriteCmdForNew(pkt, origin_addr, 1, mem_intr);
                // second issue write commad to origin_addr + burst_size
                Addr new_addr = origin_addr + burst_size;
                if (pkt->suffixLen != 0) {
                    new_addr = pkt->newBlockAddr;
                }
                issueWriteCmdForNew(pkt, new_addr, sizeMap[type] - curSize, mem_intr);
            }
        }
    } else if (pkt->newPType == 0x40) {
        for (int cnt = 0; cnt < pkt_count; ++cnt) {
            unsigned size = std::min((addr | (burst_size - 1)) + 1,
                            base_addr + pkt->getSize()) - addr;

            // see if we can merge with an existing item in the write
            // queue and keep track of whether we have merged or not
            bool merged = isInWriteQueue.find(burstAlign(addr, mem_intr)) !=
                isInWriteQueue.end();

            // if the item was not merged we need to create a new write
            // and enqueue it
            if (!merged) {
                MemPacket* mem_pkt;

                mem_pkt = mem_intr->decodePacket(pkt, addr, size, false,
                                                        mem_intr->pseudoChannel);
                // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
                // Default readyTime to Max if nvm interface;
                //will be reset once read is issued
                mem_pkt->readyTime = MaxTick;

                mem_intr->setupRank(mem_pkt->rank, false);

                DPRINTF(MemCtrl, "Adding to write queue\n");

                writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
                isInWriteQueue.insert(burstAlign(addr, mem_intr));

                // log packet
                logRequest(MemCtrl::WRITE, pkt->requestorId(),
                        pkt->qosValue(), mem_pkt->addr, 1);

                mem_intr->writeQueueSize++;

            } else {
                DPRINTF(MemCtrl,
                        "Merging write burst with existing queue entry\n");
            }

            // Starting address of next memory pkt (aligned to burst_size boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }

    } else {
        panic("wrong type");
    }
    // we do not wait for the writes to be send to the actual memory,
    // but instead take responsibility for the consistency here and
    // snoop the write queue for any upcoming reads
    // @todo, if a pkt size is larger than burst size, we might need a
    // different front end latency
    accessAndRespondForNew(pkt, frontendLatency, mem_intr);
    return true;
}

void
MemCtrl::printQs() const
{
#if TRACING_ON
    DPRINTF(MemCtrl, "===READ QUEUE===\n\n");
    for (const auto& queue : readQueue) {
        for (const auto& packet : queue) {
            DPRINTF(MemCtrl, "Read %#x\n", packet->addr);
        }
    }

    DPRINTF(MemCtrl, "\n===RESP QUEUE===\n\n");
    for (const auto& packet : respQueue) {
        DPRINTF(MemCtrl, "Response %#x\n", packet->addr);
    }

    DPRINTF(MemCtrl, "\n===WRITE QUEUE===\n\n");
    for (const auto& queue : writeQueue) {
        for (const auto& packet : queue) {
            DPRINTF(MemCtrl, "Write %#x\n", packet->addr);
        }
    }
#endif // TRACING_ON
}

bool
MemCtrl::recvTimingReq(PacketPtr pkt)
{
    // This is where we enter from the outside world
    DPRINTF(MemCtrl, "recvTimingReq: request %s addr %#x size %d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());

    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller\n");

    recordMemConsumption();

    bool isAccepted = false;
    if (operationMode == "normal") {
        isAccepted = recvTimingReqLogic(pkt);
    } else if (operationMode == "compresso") {
        isAccepted = recvTimingReqLogicForCompr(pkt);
    } else if (operationMode == "DyLeCT") {
        // printf("brefore enter the logic, the pkt address is 0x%lx\n", pkt);
        isAccepted = recvTimingReqLogicForDyL(pkt);
        // printf("isAccepted: %d\n", isAccepted);
        // printf("is blocked: %d\n", blockedForDyL);
    } else if (operationMode == "new") {
        isAccepted = recvTimingReqLogicForNew(pkt);
        // printf("isAccepted %d\n", isAccepted);
    } else if (operationMode == "secure") {
        isAccepted = recvTimingReqLogicForSecure(pkt);
    }
    return isAccepted;
}

bool
MemCtrl::recvTimingReqLogic(PacketPtr pkt) {
    // Calc avg gap between requests
    if (prevArrival != 0) {
        stats.totGap += curTick() - prevArrival;
    }
    prevArrival = curTick();

    recvLastPkt = curTick();

    panic_if(!(dram->getAddrRange().contains(pkt->getAddr())),
             "Can't handle address range for packet %s\n", pkt->print());

    // Find out how many memory packets a pkt translates to
    // If the burst size is equal or larger than the pkt size, then a pkt
    // translates to only one memory packet. Otherwise, a pkt translates to
    // multiple memory packets
    unsigned size = pkt->getSize();
    uint32_t burst_size = dram->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    PPN page_num = (pkt->getAddr() >> 12);
    stat_page_used.insert(page_num);

    // run the QoS scheduler and assign a QoS priority value to the packet
    qosSchedule( { &readQueue, &writeQueue }, burst_size, pkt);

    // check local buffers and do not accept if full
    if (pkt->isWrite()) {
        assert(size != 0);
        if (writeQueueFull(pkt_count)) {
            DPRINTF(MemCtrl, "Write queue full, not accepting\n");
            // remember that we have to retry this port
            retryWrReq = true;
            stats.numWrRetry++;
            return false;
        } else {
            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
                printf("marker accept TimingReq: request %s addr %#x size %d\n",
                    pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
                printf("%lx\n", pkt);
                fflush(stdout);
            }
            // uint8_t* my_test_start = pkt->getPtr<uint8_t>();
            // for (int is = 0; is < pkt->getSize(); is++) {
            //     if (is % 8 == 0) {
            //         printf("\n");
            //     }
            //     printf("%02x ",static_cast<unsigned>(my_test_start[is]));
            // }
            // printf("\n");
            addToWriteQueue(pkt, pkt_count, dram);
            // If we are not already scheduled to get a request out of the
            // queue, do so now
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Request scheduled immediately\n");
                schedule(nextReqEvent, curTick());
            }
            stats.writeReqs++;
            stats.bytesWrittenSys += size;
        }
    } else {
        assert(pkt->isRead());
        assert(size != 0);
        if (readQueueFull(pkt_count)) {
            DPRINTF(MemCtrl, "Read queue full, not accepting\n");
            // remember that we have to retry this port
            retryRdReq = true;
            stats.numRdRetry++;
            return false;
        } else {
            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
                printf("marker accept TimingReq: request %s addr %#x size %d\n",
                    pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
                printf("%lx\n", pkt);
                fflush(stdout);
            }

            if (!addToReadQueue(pkt, pkt_count, dram)) {
                // If we are not already scheduled to get a request out of the
                // queue, do so now
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Request scheduled immediately\n");
                    schedule(nextReqEvent, curTick());
                }
            }
            stats.readReqs++;
            stats.bytesReadSys += size;
        }
    }

    return true;
}

/*
    specially function for timing access in Compresso
    1. refuse the pkt if the read/write candidate queue is full
    2. create a auxiliary pkt for processing the real pkt
    3. prepare for the metadata of the real pkt's data, make the write pkt aligned
*/
bool
MemCtrl::recvTimingReqLogicForCompr(PacketPtr pkt, bool hasBlocked){
    if (!hasBlocked) {
        // Calc avg gap between requests
        if (prevArrival != 0) {
            stats.totGap += curTick() - prevArrival;
        }
        prevArrival = curTick();
    } else {
        assert(!blockedForCompr);
    }

    panic_if(!(dram->getAddrRange().contains(pkt->getAddr())),
                "Can't handle address range for packet %s\n", pkt->print());

    // Find out how many memory packets a pkt translates to
    // If the burst size is equal or larger than the pkt size, then a pkt
    // translates to only one memory packet. Otherwise, a pkt translates to
    // multiple memory packets
    unsigned size = pkt->getSize();
    uint32_t burst_size = dram->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    if (blockedForCompr) {
        /* currently there are some pages being recompressed */
        DPRINTF(MemCtrl, "Line %d, the memory controller now is blocked\n", __LINE__);
        if (blockedNum + pkt_count <= std::min(writeBufferSizeForCompr, readBufferSizeForCompr)) {
            pkt->comprTick = curTick();
            blockPktQueue.emplace_back(pkt);
            blockedNum += pkt_count;
            return true;
        } else {
            if(pkt->isWrite()) {
                retryWrReq = true;
            } else {
                assert(pkt->isRead());
                retryRdReq = true;
            }
            return false;
        }
    }

    // run the QoS scheduler and assign a QoS priority value to the packet
    qosSchedule( { &readQueue, &writeQueue }, burst_size, pkt);

    DPRINTF(MemCtrl, "finish the Qos scheduler\n");

    recvLastPkt = curTick();

    /* refuse the pkt if exceed the buffer size */
    assert(size != 0);
    if (pkt->isWrite()) {
        if (curWriteNum + pkt_count > writeBufferSizeForCompr) {
            DPRINTF(MemCtrl, "The write block queue is full, not accepting\n");
            assert(!hasBlocked);
            retryWrReq = true;
            stats.numWrRetry++;
            return false;
        } else {
            curWriteNum += pkt_count;
        }
    } else {
        assert(pkt->isRead());
        if (curReadNum + pkt_count > readBufferSizeForCompr) {
            DPRINTF(MemCtrl, "The read block queue is full, not accepting\n");
            assert(!hasBlocked);
            // remember that we have to retry this port
            retryRdReq = true;
            stats.numRdRetry++;
            return false;
        } else {
            PPN ppn = (pkt->getAddr() >> 12 & ((1ULL << 52) - 1));
            uint8_t cacheLineIdx = (pkt->getAddr() >> 6) & 0x3F;
            Addr memory_addr = ppn * 64;
            bool isZero = false;
            if (mcache.isExist(memory_addr)) {
                std::vector<uint8_t> metadata = mcache.find(memory_addr);
                std::pair<bool, Addr> cLStatus = addressTranslation(metadata, cacheLineIdx);
                bool inInflate = cLStatus.first;
                uint8_t type = getType(metadata, cacheLineIdx);
                if ((type == 0) && (!inInflate)) {
                    isZero = true;
                }
            }
            if ((pkt_count == 1) && (pageNum != ppn) && isZero) {
                /* zero-aware optimization */
                bool needsResponse = pkt->needsResponse();
                std::vector<uint8_t> allZero(pkt->getSize(), 0);
                pkt->setDataForMC(allZero.data(), 0, pkt->getSize());
                if (needsResponse) {
                    pkt->makeResponse();
                    // access already turned the packet into a response
                    assert(pkt->isResponse());
                    // response_time consumes the static latency and is charged also
                    // with headerDelay that takes into account the delay provided by
                    // the xbar and also the payloadDelay that takes into account the
                    // number of data beats.
                    Tick response_time = curTick() + frontendLatency + pkt->headerDelay +
                                        pkt->payloadDelay;
                    // Here we reset the timing of the packet before sending it out.
                    pkt->headerDelay = pkt->payloadDelay = 0;

                    // queue the packet in the response queue to be sent out after
                    // the static latency has passed
                    port.schedTimingResp(pkt, response_time);
                } else {
                    // @todo the packet is going to be deleted, and the MemPacket
                    // is still having a pointer to it
                    pendingDelete.reset(pkt);
                }

                return true;
            } else {
                curReadNum += pkt_count;
            }
        }
    }

    /* initial an auxiliary pkt for next steps */
    PacketPtr auxPkt = new Packet(pkt, curTick());
    auxPkt->allocateForMC();
    memcpy(auxPkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

    DPRINTF(MemCtrl, "(pkt) Line %d, the aux pkt %lx for the request %s\n", __LINE__, auxPkt, pkt->cmdString());
    DPRINTF(MemCtrl, "the auxPkt is %s\n", auxPkt->cmdString());

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
        // printf("\n\n***********************************\n\n");
        printf("marker accept TimingReq: request %s addr %#x size %d\n",
            pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
        printf("%lx\n", auxPkt);
        fflush(stdout);
    }

    if (hasBlocked) {
        auxPkt->comprTick = pkt->comprTick;  /* update the real access time if current pkt is blocked */
    }

    waitQueue.insert(auxPkt);
    auxPkt->ref_cnt++;

    prepareMetaData(auxPkt);
    return true;
}

bool
MemCtrl::recvTimingReqLogicForDyL(PacketPtr pkt, bool hasBlocked) {
    if (!hasBlocked) {
        // Calc avg gap between requests
        if (prevArrival != 0) {
            stats.totGap += curTick() - prevArrival;
        }
        prevArrival = curTick();
        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
            printf("recv Timing req at tick %ld\n", curTick());
        }

    } else {
        assert(!blockedForDyL);
    }

    recvLastPkt = curTick();

    panic_if(!(dram->getAddrRange().contains(pkt->getAddr())),
            "Can't handle address range for packet %s\n", pkt->print());

    // Find out how many memory packets a pkt translates to
    // If the burst size is equal or larger than the pkt size, then a pkt
    // translates to only one memory packet. Otherwise, a pkt translates to
    // multiple memory packets
    unsigned size = pkt->getSize();
    uint32_t burst_size = dram->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    if (blockedForDyL || functionalBlockedForDyL) {
        assert(!hasBlocked);
        // printf("blockedNumForDyL: %d\n", blockedNumForDyL);
        // printf("pkt_count: %d\n", pkt_count);
        if (((blockedNumForDyL + pkt_count) <= readBufferSize) && ((blockedNumForDyL + pkt_count) <= writeBufferSize)) {
            blockedQueueForDyL.push_back(pkt);
            blockedNumForDyL += pkt_count;
            return true;
        } else {
            if(pkt->isWrite()) {
                retryWrReq = true;
            } else {
                assert(pkt->isRead());
                retryRdReq = true;
            }
            return false;
        }
    }

    // run the QoS scheduler and assign a QoS priority value to the packet
    qosSchedule( { &readQueue, &writeQueue }, burst_size, pkt);

    PacketPtr auxPkt = new Packet(pkt);
    auxPkt->DyLBackup = pkt->getAddr();
    auxPkt->allocateForMC();
    memcpy(auxPkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

    // calculate the address of the CTE
    PPN ppn = (auxPkt->getAddr()) >> 12;
    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
        printf("\n***********a new request************\n");
        printf("the address of auxPkt is 0x%lx\n", reinterpret_cast<unsigned long>(auxPkt));
        printf("current ppn is %d\n", ppn);
    }
    Addr cteAddr = startAddrForCTE + ppn * 8;
    Addr cteAddrAligned = (cteAddr >> 6) << 6;
    uint8_t loc = (cteAddr >> 3) & ((1 << 3) - 1);
    assert (loc < 8);

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
        printf("the cteAddrAligned is 0x%lx\n", cteAddrAligned);
    }

    // check if the metadata cache hit
    std::vector<uint8_t> cacheLine = mcache.find(cteAddrAligned);
    assert(cacheLine.size() == 64);
    uint64_t cteCandi = 0;
    for (unsigned int i = loc * 8; i < (loc + 1) * 8; i++) {
        cteCandi = (cteCandi << 8) | cacheLine[i];
    }
    bool cacheHit = (((cteCandi >> 63) & 0x1) != 0);
    bool sign = false;

    if (cacheHit) {
        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
            printf("cachehit, the cte is 0x%lx\n", cteCandi);
            // printf("loc is %d\n", loc);
        }

        // fflush(stdout);
        uint8_t testPtr[8];
        dram->atomicRead(testPtr, cteAddr, 8);
        for (int i = 0; i < 8; i++) {
            assert(testPtr[i] == cacheLine[(loc * 8) + i]);
        }
        Addr addr = ((cteCandi >> 32) & ((1ULL << 30) - 1)) << 12;
        assert(addr != 0);
    }

    // check local buffers and do not accept if full
    if (auxPkt->isWrite()) {
        assert(size != 0);
        if (expectWriteQueueFull(pkt_count)) {
            DPRINTF(MemCtrl, "Write queue full, not accepting\n");
            // remember that we have to retry this port
            // printf("pkt_count is %d\n", pkt_count);
            // printf("pktinProcess is %d\n", pktInProcess);
            // printf("expectWriteQueueSize is %d\n", expectWriteQueueSize);
            retryWrReq = true;
            stats.numWrRetry++;
            delete auxPkt;
            return false;
        } else {
            // printf("Line %d: recency list push: %d\n", __LINE__, ppn);
            // auto it = std::find(recencyList.begin(), recencyList.end(), ppn);
            // if (it != recencyList.end()) {
            //     recencyList.remove(ppn);
            //     recencyList.push_front(ppn);
            // }
            // accessCnt = 0;
            // DPRINTF(MemCtrl, "Line %d: [Recency List] update the recency list, push front the %lld\n", __LINE__, ppn);

            pktInProcess++;
            inProcessWritePkt.emplace_back(auxPkt);

            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
                printf("marker accept TimingReq: request %s addr %#x size %d\n",
                    pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
                printf("%lx\n", auxPkt);
                fflush(stdout);
            }

            recencyList.remove(ppn);
            recencyList.push_front(ppn);


            auto it = recencyMap.find(ppn);
            if (it != recencyMap.end()) {
                recencyList.erase(it->second);
            }

            recencyList.push_front(ppn);
            recencyMap[ppn] = recencyList.begin();


            if (recencyList.size() > recencyListThreshold) {
                assert(blockedForDyL == false);
                blockedForDyL = true;
            }

            /* store the original address */
            pkt->setBackUp(pkt->getAddr());

            if (pagesInDecompress.find(ppn) != pagesInDecompress.end()) {
                // panic("not implement yet[C1]");
                waitForDeCompress.push_back(auxPkt);
                return true;
            }

            /* if hit in cache, do not need to read the CTE from memory*/
            if (cacheHit) {
                DPRINTF(MemCtrl, "Metadata cache hit [write]\n");
                if ((cteCandi & (1ULL << 62)) == 0) {   // the page is currently uncompressed
                    /* translate the address instantly */
                    Addr addr = ((cteCandi >> 32) & ((1ULL << 30) - 1)) << 12;
                    Addr realAddr = addr | (auxPkt->getAddr() & ((1ULL << 12) - 1));
                    auxPkt->setAddr(realAddr);

                    addToWriteQueueForDyL(auxPkt, pkt_count, dram);
                    stats.writeReqs++;
                    stats.bytesWrittenSys += size;
                } else {  // the page is currently compressed
                    if (isAddressCovered(pkt->getAddr(), 8, 1)) {
                        printf("Opps, the page is compressed\n");
                    }
                    assert(pagesInDecompress.find(ppn) == pagesInDecompress.end());
                    assert(pagesInCompress.find(ppn) == pagesInCompress.end());

                    pagesInDecompress.insert(ppn);
                    /* the MC should issue readCompress and then writeUncompress packet to decompress the page */
                    PacketPtr readCompress = new Packet(auxPkt);
                    auxPkt->ref_cnt++;

                    if (isAddressCovered(pkt->getAddr(), 0, 1)) {
                        printf("create a readCompress 1: origin auxPkt address is 0x%lx\n", reinterpret_cast<unsigned long>(auxPkt));
                    }

                    DPRINTF(MemCtrl, "Line %d: create a new packet for read Compress, the address is 0x%llx\n", __LINE__, (uint64_t)readCompress);

                    /* read the dram address and the page size from CTE */
                    Addr addr = ((cteCandi >> 10) & ((1ULL << 40) - 1)) << 8;
                    uint64_t pageSize = ((cteCandi >> 50) & ((1ULL << 12) - 1)) + 1;   // size range [1, 2kiB]

                    readCompress->configAsReadCompress(addr, pageSize, auxPkt);
                    unsigned rc_offset = (readCompress->getAddr()) & (burst_size - 1);
                    unsigned int rc_pkt_count = divCeil(rc_offset + pageSize, burst_size);
                    sign = addToReadQueueForDyL(readCompress, rc_pkt_count, dram);
                }
            } else {
                /* cache miss. The MC should issue a packet to read the CTE */
                PacketPtr readCTE = new Packet(auxPkt);
                auxPkt->ref_cnt++;

                DPRINTF(MemCtrl, "Line %d: create a new packet for read CTE, the address is 0x%llx\n", __LINE__, (uint64_t)readCTE);


                readCTE->configAsReadCTE(cteAddrAligned, auxPkt);
                sign = addToReadQueueForDyL(readCTE, 1, dram);
            }
            // If we are not already scheduled to get a request out of the
            // queue, do so now
            if (!sign) {
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
                    schedule(nextReqEvent, curTick());
                }
            }
        }
    } else {
        assert(auxPkt->isRead());
        assert(size != 0);
        if (expectReadQueueFull(pkt_count)) {
            DPRINTF(MemCtrl, "Read queue full, not accepting\n");
            // remember that we have to retry this port
            printf("pkt_count is %d\n", pkt_count);
            printf("pktinProcess is %d\n", pktInProcess);
            printf("expectReadQueueSize is %d\n", expectReadQueueSize);
            delete auxPkt;
            retryRdReq = true;
            stats.numRdRetry++;
            return false;
        } else {
            // auto it = std::find(recencyList.begin(), recencyList.end(), ppn);
            // if (it != recencyList.end()) {
            //     recencyList.remove(ppn);
            //     recencyList.push_front(ppn);
            // }
            // // DPRINTF(MemCtrl, "Line %d: [Recency List] update the recency list, push front the %lld\n", __LINE__, ppn);
            // accessCnt = 0;

            pktInProcess++;

            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
                printf("marker accept TimingReq: request %s addr %#x size %d\n",
                    pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
                printf("%lx\n", auxPkt);
                fflush(stdout);
            }

            auto it = recencyMap.find(ppn);
            if (it != recencyMap.end()) {
                recencyList.erase(it->second);
            }

            recencyList.push_front(ppn);
            recencyMap[ppn] = recencyList.begin();

            if (recencyList.size() > recencyListThreshold) {
                assert(blockedForDyL == false);
                blockedForDyL = true;
            }

            /* store the original address */
            pkt->setBackUp(pkt->getAddr());

            // DPRINTF(MemCtrl,"the backup address of pkt is 0x%lx\n", pkt->DyLBackup);
            // DPRINTF(MemCtrl, "After accept the pkt, the address of packet is 0x%lx, the pkt_count is %d\n", (uint64_t)pkt, pkt_count);
            expectReadQueueSize += pkt_count;

            if (pagesInDecompress.find(ppn) != pagesInDecompress.end()) {
                // panic("not implement yet[C2]");
                waitForDeCompress.push_back(auxPkt);
                return true;
            }

            if (cacheHit) {
                if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                    printf("Read cache hit\n");
                    printf("metadata of page %d is 0x%lx\n", ppn, cteCandi);
                    printf("loc is %d\n", loc);
                }

                if ((cteCandi & (1ULL << 62)) == 0) {   // the page is currently uncompressed
                    Addr addr = ((cteCandi >> 32) & ((1ULL << 30) - 1)) << 12;
                    Addr realAddr = addr | (auxPkt->getAddr() & ((1ULL << 12) - 1));

                    if (isAddressCovered(pkt->getAddr(), 8, 1)) {
                        printf("the address is 0x%lx\n", addr);
                        printf("the realAddr is 0x%lx\n", realAddr);
                    }

                    auxPkt->setAddr(realAddr);
                    sign = addToReadQueueForDyL(auxPkt, pkt_count, dram);
                    stats.readReqs++;
                    stats.bytesReadSys += size;
                } else {  // the page is currently compressed
                    if (isAddressCovered(pkt->getAddr(), 8, 1)) {
                        printf("Opps, the page is compressed\n");
                    }
                    assert(pagesInDecompress.find(ppn) == pagesInDecompress.end());
                    assert(pagesInCompress.find(ppn) == pagesInCompress.end());

                    pagesInDecompress.insert(ppn);
                    PacketPtr readCompress = new Packet(auxPkt);
                    auxPkt->ref_cnt++;

                    if (isAddressCovered(pkt->getAddr(), 0, 1)) {
                        printf("create a readCompress [read]: origin auxPkt address is 0x%lx\n", reinterpret_cast<unsigned long>(auxPkt));
                    }

                    Addr addr = ((cteCandi >> 10) & ((1ULL << 40) - 1)) << 8;
                    uint64_t pageSize = ((cteCandi >> 50) & ((1ULL << 12) - 1)) + 1;   // size range [1, 2kiB]
                    assert (freeList.size() > 0);
                    readCompress->configAsReadCompress(addr, pageSize, auxPkt);
                    unsigned rc_offset = readCompress->getAddr() & (burst_size - 1);
                    unsigned int rc_pkt_count = divCeil(rc_offset + pageSize, burst_size);
                    sign = addToReadQueueForDyL(readCompress, rc_pkt_count, dram);
                }
            } else {
                if (isAddressCovered(pkt->getAddr(), 8, 1)) {
                    printf("cache miss for read\n");
                }
                PacketPtr readCTE = new Packet(auxPkt);
                auxPkt->ref_cnt++;

                readCTE->configAsReadCTE(cteAddrAligned, auxPkt);
                sign = addToReadQueueForDyL(readCTE, 1, dram);
            }
            if (!sign) {
                // If we are not already scheduled to get a request out of the
                // queue, do so now
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Request scheduled immediately\n");
                    schedule(nextReqEvent, curTick());
                }
            }
        }
    }

    return true;
}

uint64_t
MemCtrl::parseMetaDataForSecure(const std::vector<uint8_t>& metaData, int type) {
    /*
        type = 0: return dram address
        type = 1: return compressed size
    */
    if (type == 0) {
        uint64_t addr = 0;
        for (int i = 3; i < 7; i++) {
            addr = (addr << 8) | metaData[i];
        }
        addr <<= 11;
        return addr;
    } else if (type == 1) {
        uint64_t c_size = 0;
        for (int i = 1; i < 3; i++) {
            c_size = (c_size << 8) | metaData[i];
        }
        return c_size;
    } else {
        panic("unknown type when parsing the metadata");
    }
}

bool
MemCtrl::recvTimingReqLogicForSecure(PacketPtr pkt, bool hasBlocked)
{
    if (!hasBlocked) {
        // Calc avg gap between requests
        if (prevArrival != 0) {
            stats.totGap += curTick() - prevArrival;
        }
        prevArrival = curTick();
        // printf("\n=====================\n");
        // printf("recv new pkt from outside world");
        // printf("blockedForSecure? %d\n", blockedForSecure);
        // printf("Timing-Req: request %s addr %#x size %d\n",
        //     pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());

    } else {
        assert(!blockedForSecure);
    }

    recvLastPkt = curTick();

    panic_if(!(dram->getAddrRange().contains(pkt->getAddr())),
            "Can't handle address range for packet %s\n", pkt->print());

    // Find out how many memory packets a pkt translates to
    // If the burst size is equal or larger than the pkt size, then a pkt
    // translates to only one memory packet. Otherwise, a pkt translates to
    // multiple memory packets
    unsigned size = pkt->getSize();
    uint32_t burst_size = dram->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    if (blockedForSecure) {
        assert(!hasBlocked);
        // printf("blocked for secure 1\n");
        if ((blockedNumForSecure + pkt_count) <= std::max(readBufferSize, writeBufferSize)) {
            blockedQueueForSecure.emplace_back(pkt);
            blockedNumForSecure += pkt_count;
            // printf("add to the blocked queue\n");
            return true;
        } else {
            // printf("the blocked queue is full\n");
            if(pkt->isWrite()) {
                retryWrReq = true;
            } else {
                assert(pkt->isRead());
                retryRdReq = true;
            }
            return false;
        }
    }

    // run the QoS scheduler and assign a QoS priority value to the packet
    qosSchedule( { &readQueue, &writeQueue }, burst_size, pkt);

    /* check if the queue has enough room */
    if(pkt->isWrite()) {
        assert(size != 0);
        if (expectWriteQueueFull(pkt_count)) {
            DPRINTF(MemCtrl, "Write queue full, not accepting\n");
            // remember that we have to retry this port
            retryWrReq = true;
            stats.numWrRetry++;
            assert(!hasBlocked);

            printf("write queue full. pkt count is %d, expectWriteQueueSize is %d\n", pkt_count, expectWriteQueueSize);
            return false;
        }
    } else {
        assert(pkt->isRead());
        assert(size != 0);
        if (expectReadQueueFull(pkt_count)) {
            DPRINTF(MemCtrl, "Read queue full, not accepting\n");
            // remember that we have to retry this port
            retryRdReq = true;
            stats.numRdRetry++;
            assert(!hasBlocked);


            printf("read queue full. pkt count is %d, expectReadQueueSize is %d, respQueue size %d\n", pkt_count, expectReadQueueSize, respQueue.size());
            return false;
        }
    }

    /* create an auxiliary pkt */
    PacketPtr auxPkt = new Packet(pkt);
    auxPkt->configAsSecureAuxPkt(pkt, pkt->getAddr(), pkt->getSize());

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
        printf("\n\n================\n\n");
        printf("marker accept TimingReq: request %s addr %#x size %d\n",
            pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
        printf("%lx\n", auxPkt);
        fflush(stdout);
    }

    processPktListForSecure.emplace_back(auxPkt);

    PPN ppn = (pkt->getAddr()) >> 12;

    /* cal the dram address for metadata */
    Addr mAddr = startAddrForSecureMetaData + ppn * 8;

    // check if the metadata cache hit
    std::vector<uint8_t> metaDataEntry = mcache.find(mAddr);
    assert(metaDataEntry.size() == 64);

    std::vector<uint8_t> metaDataCandi(8, 0);

    memcpy(metaDataCandi.data(), metaDataEntry.data(), 8);

    bool cacheHit = (metaDataCandi[0] >= 0x80);

    if (cacheHit) {

        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
            printf("the cache hit\n");
        }

        // set backup
        auxPkt->backupForSecure = pkt->getAddr();

        // prepare the metadata info
        auxPkt->metaDataMapForSecure[ppn] = metaDataCandi;

        if (pkt->isWrite()) {
            addToWriteQueueForSecure(auxPkt, pkt_count, dram);
            // If we are not already scheduled to get a request out of the
            // queue, do so now
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Request scheduled immediately\n");
                schedule(nextReqEvent, curTick());
            }
            stats.writeReqs++;
            stats.bytesWrittenSys += size;
        } else {
            if (!addToReadQueueForSecure(auxPkt, pkt_count, dram)) {
                // If we are not already scheduled to get a request out of the
                // queue, do so now
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Request scheduled immediately\n");
                    schedule(nextReqEvent, curTick());
                }
            }
            stats.readReqs++;
            stats.bytesReadSys += size;
        }
    } else {
        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
            printf("the cache miss\n");
        }
        blockedForSecure = true;

        if (mcache.isFull()) {

            Addr victim_page_maddr = mcache.lastElemAddr();
            PPN victim_page_ppn = (victim_page_maddr - startAddrForSecureMetaData) / 8;
            assert(mcache.isExist(victim_page_maddr));
            std::vector<uint8_t> metaData = mcache.find(victim_page_maddr, false);
            mcache.pop();
            Addr dram_addr = parseMetaDataForSecure(metaData, 0);

            /* create a readForCompress pkt */


            PacketPtr readForCompress = new Packet(auxPkt);
            readForCompress->configAsSecureReadForCompress(auxPkt, dram_addr, 4096);
            readForCompress->metaDataMapForSecure[victim_page_ppn] = metaData;

            // printf("secure: create a readForCompress pkt: 0x%lx\n", readForCompress);

            pendingPktForSecure = readForCompress;

            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                printf("the next pkt is readForCompress\n");
            }

        } else {
            /* create a readMetaData pkt */
            PacketPtr readMetaData = new Packet(auxPkt);
            readMetaData->configAsSecureReadMetaData(auxPkt, mAddr, 8);

            pendingPktForSecure = readMetaData;

            if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
                printf("secure: create a readMetaData pkt: 0x%lx\n", readMetaData);
                printf("the next pkt is readMetaData\n");
            }
        }

        if (pktInProcess == 0) {
            unsigned p_size = pendingPktForSecure->getSize();
            unsigned p_offset = pendingPktForSecure->getAddr() & (burst_size - 1);
            unsigned int p_pkt_count = divCeil(p_offset + p_size, burst_size);

            if (!addToReadQueueForSecure(pendingPktForSecure, p_pkt_count, dram)) {
                // If we are not already scheduled to get a request out of the
                // queue, do so now
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Request scheduled immediately\n");
                    schedule(nextReqEvent, curTick());
                }
            }

            pendingPktForSecure = nullptr;
        }

    }

    return true;
}

void
MemCtrl::addSubPktToWriteQueueForSecure(PacketPtr pkt, unsigned int pkt_count, MemInterface* mem_intr, bool updateStats) {
    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;
    uint32_t burst_size = mem_intr->bytesPerBurst();

    for (int cnt = 0; cnt < pkt_count; ++cnt) {
        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                        base_addr + pkt->getSize()) - addr;

        if (updateStats) {
            stats.writePktSize[ceilLog2(size)]++;
            stats.writeBursts++;
            stats.requestorWriteAccesses[pkt->requestorId()]++;
        }

        // see if we can merge with an existing item in the write
        // queue and keep track of whether we have merged or not
        bool merged = isInWriteQueue.find(burstAlign(addr, mem_intr)) !=
            isInWriteQueue.end();

        // if the item was not merged we need to create a new write
        // and enqueue it
        if (!merged) {
            MemPacket* mem_pkt;
            mem_pkt = mem_intr->decodePacket(pkt, addr, size, false,
                                                    mem_intr->pseudoChannel);

            if (updateStats) {
                mem_pkt->memoryAccess = true;
            }

            // Default readyTime to Max if nvm interface;
            //will be reset once read is issued
            mem_pkt->readyTime = MaxTick;

            mem_intr->setupRank(mem_pkt->rank, false);

            // assert(totalWriteQueueSize < writeBufferSize);  // in the new scenario, this is not always true
            // stats.wrQLenPdf[totalWriteQueueSize]++;

            DPRINTF(MemCtrl, "Adding to write queue\n");

            writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
            isInWriteQueue.insert(burstAlign(addr, mem_intr));

            // log packet
            logRequest(MemCtrl::WRITE, pkt->requestorId(),
                    pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->writeQueueSize++;

            assert(totalWriteQueueSize == isInWriteQueue.size());

            if (updateStats) {
                expectWriteQueueSize++;
                stats.avgWrQLen = totalWriteQueueSize;
            }
        } else {
            DPRINTF(MemCtrl,
                    "Merging write burst with existing queue entry\n");

            if (updateStats) {
                // keep track of the fact that this burst effectively
                // disappeared as it was merged with an existing one
                stats.mergedWrBursts++;
            }
        }

        // Starting address of next memory pkt (aligned to burst_size boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }
}

void
MemCtrl::addToWriteQueueForSecure(PacketPtr pkt, unsigned int pkt_count,
                                        MemInterface* mem_intr)
{
    // only add to the write queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(pkt->isWrite());

    if (pkt->securePType == 0x1) {
        /* auxPkt */
        addSubPktToWriteQueueForSecure(pkt, pkt_count, mem_intr, true);
        pktInProcess++;
    } else {
        addSubPktToWriteQueueForSecure(pkt, pkt_count, mem_intr, false);
    }

    // we do not wait for the writes to be send to the actual memory,
    // but instead take responsibility for the consistency here and
    // snoop the write queue for any upcoming reads
    // @todo, if a pkt size is larger than burst size, we might need a
    // different front end latency
    accessAndRespondForSecure(pkt, frontendLatency, mem_intr);
}

bool
MemCtrl::addSubPktToReadQueueForSecure(PacketPtr pkt, unsigned int pkt_count, MemInterface* mem_intr, bool updateStats) {
    const Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;
    uint32_t burst_size = mem_intr->bytesPerBurst();

    unsigned pktsServicedByWrQ = 0;
    BurstHelper* burst_helper = NULL;

    if (updateStats) {
        expectReadQueueSize += pkt_count;
    }


    for (int cnt = 0; cnt < pkt_count; ++cnt) {
        unsigned size = std::min((addr | (burst_size - 1)) + 1,
                        base_addr + pkt->getSize()) - addr;

        if (updateStats) {
            stats.readPktSize[ceilLog2(size)]++;
            stats.readBursts++;
            stats.requestorReadAccesses[pkt->requestorId()]++;
        }

        // First check write buffer to see if the data is already at
        // the controller
        bool foundInWrQ = false;
        Addr burst_addr = burstAlign(addr, mem_intr);

        // if the burst address is not present then there is no need
        // looking any further
        if (isInWriteQueue.find(burst_addr) != isInWriteQueue.end()) {
            for (const auto& vec : writeQueue) {
                for (const auto& p : vec) {
                    // check if the read is subsumed in the write queue
                    // packet we are looking at
                    if (p->addr <= addr &&
                    ((addr + size) <= (p->addr + p->size))) {

                        foundInWrQ = true;
                        pktsServicedByWrQ++;
                        DPRINTF(MemCtrl,
                                "Read to addr %#x with size %d serviced by "
                                "write queue\n",
                                addr, size);
                        if (updateStats) {
                            stats.servicedByWrQ++;
                            stats.bytesReadWrQ += burst_size;
                        }
                        break;
                    }
                }
            }
        }
        // If not found in the write q, make a memory packet and
        // push it onto the read queue
        if (!foundInWrQ) {
            // Make the burst helper for split packets
            if (pkt_count > 1 && burst_helper == NULL) {
                DPRINTF(MemCtrl, "Read to addr %#x translates to %d "
                        "memory requests\n", pkt->getAddr(), pkt_count);
                burst_helper = new BurstHelper(pkt_count);
            }

            MemPacket* mem_pkt;
            mem_pkt = mem_intr->decodePacket(pkt, addr, size, true,
                                                    mem_intr->pseudoChannel);

            if (updateStats) {
                mem_pkt->memoryAccess = true;
            }

            // Increment read entries of the rank (dram)
            // Increment count to trigger issue of non-deterministic read (nvm)
            mem_intr->setupRank(mem_pkt->rank, true);

            // Default readyTime to Max; will be reset once read is issued
            mem_pkt->readyTime = MaxTick;
            mem_pkt->burstHelper = burst_helper;

            // assert(!readQueueFull(1));
            // stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

            DPRINTF(MemCtrl, "Adding to read queue\n");
            readQueue[mem_pkt->qosValue()].push_back(mem_pkt);

            // log packet
            logRequest(MemCtrl::READ, pkt->requestorId(),
                    pkt->qosValue(), mem_pkt->addr, 1);

            mem_intr->readQueueSize++;

            if (updateStats) {
                stats.avgRdQLen = totalReadQueueSize + respQueue.size();
            }
        }

        // Starting address of next memory pkt (aligned to burst boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }

    if (updateStats) {
        /* update the expectReadQueueSize, remove the read request found in the readQueue */
        expectReadQueueSize -= pktsServicedByWrQ;
    }

    // If all packets are serviced by write queue, we send the repsonse back
    if (pktsServicedByWrQ == pkt_count) {
        accessAndRespondForSecure(pkt, frontendLatency, mem_intr);
        return true;
    }

    // Update how many split packets are serviced by write queue
    if (burst_helper != NULL)
        burst_helper->burstsServiced = pktsServicedByWrQ;

    // not all/any packets serviced by the write queue
    return false;
}

bool
MemCtrl::addToReadQueueForSecure(PacketPtr pkt,
                unsigned int pkt_count, MemInterface* mem_intr)
{
    // only add to the read queue here. whenever the request is
    // eventually done, set the readyTime, and call schedule()
    assert(!pkt->isWrite());

    assert(pkt_count != 0);

    // if the request size is larger than burst size, the pkt is split into
    // multiple packets
    // Note if the pkt starting address is not aligened to burst size, the
    // address of first packet is kept unaliged. Subsequent packets
    // are aligned to burst size boundaries. This is to ensure we accurately
    // check read packets against packets in write queue.
    bool allServicedByWrQ = false;

    if (pkt->securePType == 0x1) {
        pktInProcess++;
        allServicedByWrQ = addSubPktToReadQueueForSecure(pkt, pkt_count, mem_intr, true);
    } else {
        allServicedByWrQ = addSubPktToReadQueueForSecure(pkt, pkt_count, mem_intr, false);
    }

    return allServicedByWrQ;
}


void
MemCtrl::afterDecompForSecure(PacketPtr pkt, MemInterface* mem_intr)
{
    /* the pkt is writeForDecompress */
    assert(pkt->securePType == 0x20);

    unsigned size = pkt->getSize();
    uint32_t burst_size = mem_intr->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    addToWriteQueueForSecure(pkt, pkt_count, mem_intr);
    // If we are not already scheduled to get a request out of the
    // queue, do so now
    if (!nextReqEvent.scheduled()) {
        DPRINTF(MemCtrl, "Request scheduled immediately\n");
        schedule(nextReqEvent, curTick());
    }
}

bool
MemCtrl::recvTimingReqLogicForNew(PacketPtr pkt, bool hasBlocked) {

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
        // printf("\n\n***********************************\n\n");
        // printf("ENTER THE recvTimingReqLogicForNew: request %s addr %#x size %d\n",
        //     pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
        // fflush(stdout);
    }

    if (pkt->cmd == MemCmd::SwapReq) {
        assert((pkt->getAddr() & 0x3F) + pkt->getSize() <= 64);
    }

    if (!hasBlocked) {
        if (prevArrival != 0) {
            stats.totGap += curTick() - prevArrival;
        }
        prevArrival = curTick();
    } else {
        assert(!blockedForNew);
    }

    recvLastPkt = curTick();

    panic_if(!(dram->getAddrRange().contains(pkt->getAddr())),
             "Can't handle address range for packet %s\n", pkt->print());

    // Find out how many memory packets a pkt translates to
    // If the burst size is equal or larger than the pkt size, then a pkt
    // translates to only one memory packet. Otherwise, a pkt translates to
    // multiple memory packets
    unsigned size = pkt->getSize();
    uint32_t burst_size = dram->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    if ((curTick() - lastRecomprTick > recomprInterval) || blockedForNew) {
        assert(!hasBlocked);
        if (blockedNum + pkt_count <= std::min(writeBufferSizeForNew, readBufferSizeForNew)) {
            blockedNum += pkt_count;
            waitQueueForNew.emplace_back(std::make_pair(pkt, 1));
            if (pkt->isWrite()) {
                inProcessWritePkt.emplace_back(pkt);
            }
            if (!blockedForNew) {
                blockedForNew = true;
                // printf("pkt In Process is %d\n", pktInProcess);
                if (pktInProcess == 0) {
                    // start the recompression procedure
                    readForRecompress(pkt, dram);
                }
            }
            return true;
        } else {
            if(pkt->isWrite()) {
                retryWrReq = true;
            } else {
                assert(pkt->isRead());
                retryRdReq = true;
            }
            return false;
        }
    }

    // run the QoS scheduler and assign a QoS priority value to the packet
    qosSchedule( { &readQueue, &writeQueue }, burst_size, pkt);

    // check local buffers and do not accept if full
    if (pkt->isWrite()) {
        assert(size != 0);
        if (curWriteNum + pkt_count > writeBufferSizeForNew) {
            assert(!hasBlocked);
            DPRINTF(MemCtrl, "Write queue full, not accepting\n");
            // remember that we have to retry this port
            printf("write queue full, not accepting, curWriteNum %d, pkt_count %d, writeBufferSizeForNew %d\n", curWriteNum, pkt_count, writeBufferSizeForNew);
            retryWrReq = true;
            stats.numWrRetry++;
            return false;
        } else {
            stats.writeReqs++;
            stats.bytesWrittenSys += size;
            curWriteNum += pkt_count;
        }
    } else {
        assert(pkt->isRead());
        assert(size != 0);
        if (curReadNum + pkt_count > readBufferSizeForNew) {
            DPRINTF(MemCtrl, "Read queue full, not accepting\n");
            printf("read queue full, not accepting, curReadNum %d, pkt_count %d, readBufferSizeForNew %d\n", curReadNum, pkt_count, readBufferSizeForNew);
            assert(!hasBlocked);
            // remember that we have to retry this port
            retryRdReq = true;
            stats.numRdRetry++;
            return false;
        } else {
            stats.readReqs++;
            stats.bytesReadSys += size;
            curReadNum += pkt_count;
        }
    }

    /* initial an auxiliary pkt for next steps */
    pktInProcess++;
    PacketPtr aux_pkt = new Packet(pkt);
    aux_pkt->new_subPktCnt = pkt_count;
    aux_pkt->allocateForMC();
    memcpy(aux_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

    if (pkt->isWrite()) {
        if (std::find(inProcessWritePkt.begin(), inProcessWritePkt.end(), pkt) != inProcessWritePkt.end()) {
            inProcessWritePkt.remove(pkt);
        }
        inProcessWritePkt.emplace_back(aux_pkt);
    }

    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
        printf("\n\n***********************************\n\n");
        printf("marker accept TimingReq: request %s addr %#x size %d\n",
            pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
        printf("%lx\n", aux_pkt);
        fflush(stdout);
    }

    assert(burst_size == 64);
    for (int cnt = 0; cnt < pkt_count; cnt++) {
        PacketPtr sub_pkt = new Packet(aux_pkt);
        sub_pkt->configAsSubPkt(aux_pkt, cnt);
        prepareMetaDataForNew(sub_pkt, dram);
    }

    return true;
}

void
MemCtrl::processRespondEvent(MemInterface* mem_intr,
                        MemPacketQueue& queue,
                        EventFunctionWrapper& resp_event,
                        bool& retry_rd_req)
{
    DPRINTF(MemCtrl,
            "processRespondEvent(): Some req has reached its readyTime\n");

    MemPacket* mem_pkt = queue.front();

    // media specific checks and functions when read response is complete
    // DRAM only
    mem_intr->respondEvent(mem_pkt->rank);

    if (mem_pkt->burstHelper) {
        // it is a split packet
        mem_pkt->burstHelper->burstsServiced++;
        if (mem_pkt->burstHelper->burstsServiced ==
            mem_pkt->burstHelper->burstCount) {
            // we have now serviced all children packets of a system packet
            // so we can now respond to the requestor
            // @todo we probably want to have a different front end and back
            // end latency for split packets
            if (operationMode == "normal") {
                accessAndRespond(mem_pkt->pkt, frontendLatency + backendLatency,
                                mem_intr);
            } else if (operationMode == "compresso") {
                DPRINTF(MemCtrl, "Line %d, next enter the access and respond function\n", __LINE__);
                // printf("Line %d, ener the access and respond for compr\n", __LINE__);
                accessAndRespondForCompr(mem_pkt->pkt, frontendLatency + backendLatency,
                                mem_intr);
            } else if (operationMode == "DyLeCT") {
                accessAndRespondForDyL(mem_pkt->pkt, frontendLatency + backendLatency,
                                mem_intr);
            } else if (operationMode == "new") {
                accessAndRespondForNew(mem_pkt->pkt, frontendLatency + backendLatency,
                                mem_intr);
            } else if (operationMode == "secure") {
                accessAndRespondForSecure(mem_pkt->pkt, frontendLatency + backendLatency,
                                mem_intr);
            } else {
                panic("unknown operation mode for memory controller");
            }
            delete mem_pkt->burstHelper;
            mem_pkt->burstHelper = NULL;
        }
    } else {
        // it is not a split packet
        if (operationMode == "normal") {
            accessAndRespond(mem_pkt->pkt, frontendLatency + backendLatency,
                            mem_intr);
        } else if (operationMode == "compresso") {
            DPRINTF(MemCtrl, "Line %d, next enter the access and respond function\n", __LINE__);
            // printf("Line %d, ener the access and respond for compr\n", __LINE__);
            accessAndRespondForCompr(mem_pkt->pkt, frontendLatency + backendLatency,
                            mem_intr);
        } else if (operationMode == "DyLeCT") {
            accessAndRespondForDyL(mem_pkt->pkt, frontendLatency + backendLatency,
                            mem_intr);
        } else if (operationMode == "new") {
            accessAndRespondForNew(mem_pkt->pkt, frontendLatency + backendLatency,
                            mem_intr);
        } else if (operationMode == "secure") {
            accessAndRespondForSecure(mem_pkt->pkt, frontendLatency + backendLatency,
                            mem_intr);
        } else {
            panic("unknown operation mode for memory controller");
        }
    }

    // printf("====================before pop mempktqueu========================\n");
    // for (const auto memP: queue) {
    //     printf("the address of auxpkt is%lx, the address of pkt is %lx\n", memP->pkt, memP->pkt->comprBackup);
    // }

    // printf("=================================================\n");

    queue.pop_front();

    // printf("====================after pop mempktqueu========================\n");
    // for (const auto memP: queue) {
    //     printf("the address of auxpkt is%lx, the address of pkt is %lx\n", memP->pkt, memP->pkt->comprBackup);
    // }

    // printf("=================================================\n");


    if (!queue.empty()) {
        assert(queue.front()->readyTime >= curTick());
        assert(!resp_event.scheduled());
        schedule(resp_event, queue.front()->readyTime);
    } else {
        // if there is nothing left in any queue, signal a drain
        if (drainState() == DrainState::Draining &&
            !totalWriteQueueSize && !totalReadQueueSize &&
            allIntfDrained()) {

            DPRINTF(Drain, "Controller done draining\n");
            signalDrainDone();
        } else {
            // check the refresh state and kick the refresh event loop
            // into action again if banks already closed and just waiting
            // for read to complete
            // DRAM only
            mem_intr->checkRefreshState(mem_pkt->rank);
        }
    }

    delete mem_pkt;

    // We have made a location in the queue available at this point,
    // so if there is a read that was forced to wait, retry now
    if (retry_rd_req) {
        retry_rd_req = false;
        port.sendRetryReq();
    }
}

MemPacketQueue::iterator
MemCtrl::chooseNext(MemPacketQueue& queue, Tick extra_col_delay,
                                                MemInterface* mem_intr)
{
    // This method does the arbitration between requests.

    MemPacketQueue::iterator ret = queue.end();

    if (!queue.empty()) {
        if (queue.size() == 1) {
            // available rank corresponds to state refresh idle
            MemPacket* mem_pkt = *(queue.begin());
            if (mem_pkt->pseudoChannel != mem_intr->pseudoChannel) {
                return ret;
            }
            if (packetReady(mem_pkt, mem_intr)) {
                ret = queue.begin();
                DPRINTF(MemCtrl, "Single request, going to a free rank\n");
            } else {
                DPRINTF(MemCtrl, "Single request, going to a busy rank\n");
            }
        } else if (memSchedPolicy == enums::fcfs) {
            // check if there is a packet going to a free rank
            for (auto i = queue.begin(); i != queue.end(); ++i) {
                MemPacket* mem_pkt = *i;
                if (mem_pkt->pseudoChannel != mem_intr->pseudoChannel) {
                    continue;
                }
                if (packetReady(mem_pkt, mem_intr)) {
                    ret = i;
                    break;
                }
            }
        } else if (memSchedPolicy == enums::frfcfs) {
            Tick col_allowed_at;
            std::tie(ret, col_allowed_at)
                    = chooseNextFRFCFS(queue, extra_col_delay, mem_intr);
        } else {
            panic("No scheduling policy chosen\n");
        }
    }
    return ret;
}

std::pair<MemPacketQueue::iterator, Tick>
MemCtrl::chooseNextFRFCFS(MemPacketQueue& queue, Tick extra_col_delay,
                                MemInterface* mem_intr)
{
    auto selected_pkt_it = queue.end();
    Tick col_allowed_at = MaxTick;

    // time we need to issue a column command to be seamless
    const Tick min_col_at = std::max(mem_intr->nextBurstAt + extra_col_delay,
                                    curTick());

    std::tie(selected_pkt_it, col_allowed_at) =
                 mem_intr->chooseNextFRFCFS(queue, min_col_at);

    if (selected_pkt_it == queue.end()) {
        DPRINTF(MemCtrl, "%s no available packets found\n", __func__);
    }

    return std::make_pair(selected_pkt_it, col_allowed_at);
}

void
MemCtrl::accessAndRespond(PacketPtr pkt, Tick static_latency,
                                                MemInterface* mem_intr)
{
    DPRINTF(MemCtrl, "Responding to Address %#x.. \n", pkt->getAddr());

    bool needsResponse = pkt->needsResponse();
    // do the actual memory access which also turns the packet into a
    // response
    panic_if(!mem_intr->getAddrRange().contains(pkt->getAddr()),
             "Can't handle address range for packet %s\n", pkt->print());
    mem_intr->access(pkt, access_cnt);

    // turn packet around to go back to requestor if response expected
    if (needsResponse) {
        // access already turned the packet into a response
        assert(pkt->isResponse());
        // response_time consumes the static latency and is charged also
        // with headerDelay that takes into account the delay provided by
        // the xbar and also the payloadDelay that takes into account the
        // number of data beats.
        Tick response_time = curTick() + static_latency + pkt->headerDelay +
                             pkt->payloadDelay;
        // Here we reset the timing of the packet before sending it out.
        pkt->headerDelay = pkt->payloadDelay = 0;

        // queue the packet in the response queue to be sent out after
        // the static latency has passed
        port.schedTimingResp(pkt, response_time);
    } else {
        // @todo the packet is going to be deleted, and the MemPacket
        // is still having a pointer to it
        pendingDelete.reset(pkt);
    }

    DPRINTF(MemCtrl, "Done\n");

    return;
}

void
MemCtrl::accessAndRespondForCompr(PacketPtr pkt, Tick static_latency,
                                                MemInterface* mem_intr)
{
    uint32_t burst_size = mem_intr->bytesPerBurst();
    uint32_t pType = pkt->getPType();
    assert(pType != 1);
    // printf("pType is %d\n", pType);
    if (pType == 0x2) {
        /* auxPkt */
        assert(pkt->comprBackup);
        PacketPtr real_recv_pkt = pkt->comprBackup;

        DPRINTF(MemCtrl, "Responding to pkt 0x%lx\n", real_recv_pkt);
        DPRINTF(MemCtrl, "Responding to Address %#x.. \n", real_recv_pkt->getAddr());

        bool needsResponse = real_recv_pkt->needsResponse();
        // do the actual memory access which also turns the packet into a
        // response
        panic_if(!mem_intr->getAddrRange().contains(real_recv_pkt->getAddr()),
                 "Can't handle address range for packet %s\n", real_recv_pkt->print());

        // mem_intr->accessForCompr(pkt, burst_size, pageNum, pageBuffer, mPageBuffer);
        mem_intr->accessForCompr(pkt, burst_size, pageNum, pageBufferAddr);

        if (recordForCheckReady == false) {
            waitQueue.erase(pkt);
            assert(pkt->ref_cnt > 0);
            pkt->ref_cnt--;
            checkForReadyPkt();
        } else {
            to_delete_pkt.emplace_back(pkt);
        }

        // printf("after erase the current pkt, the waitQueue size is %d\n", waitQueue.size());

        unsigned size = real_recv_pkt->getSize();
        unsigned offset = real_recv_pkt->getAddr() & (burst_size - 1);
        unsigned int pkt_count = divCeil(offset + size, burst_size);

        if (pkt->isWrite()) {
            curWriteNum -= pkt_count;
        } else {
            assert(pkt->isRead());
            assert(real_recv_pkt->getSize() == pkt->getSize());
            curReadNum -= pkt_count;
            memcpy(real_recv_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), real_recv_pkt->getSize());
        }

        // turn packet around to go back to requestor if response expected
        if (needsResponse) {
            // access already turned the packet into a response
            assert(real_recv_pkt->isResponse());
            // response_time consumes the static latency and is charged also
            // with headerDelay that takes into account the delay provided by
            // the xbar and also the payloadDelay that takes into account the
            // number of data beats.
            Tick response_time = curTick() + static_latency + real_recv_pkt->headerDelay +
                                 real_recv_pkt->payloadDelay;
            // Here we reset the timing of the packet before sending it out.
            real_recv_pkt->headerDelay = real_recv_pkt->payloadDelay = 0;

            // queue the packet in the response queue to be sent out after
            // the static latency has passed
            port.schedTimingResp(real_recv_pkt, response_time);
        } else {
            // @todo the packet is going to be deleted, and the MemPacket
            // is still having a pointer to it
            pendingDelete.reset(real_recv_pkt);
        }
        assert(pkt->ref_cnt > 0);
        pkt->ref_cnt--;
        if (pkt->ref_cnt == 0) {
            delete pkt;
        }
    } else if (pType == 0x4) {
        /* readMetaData */
        assert(pkt->comprBackup);
        PacketPtr auxPkt = pkt->comprBackup;

        /* pkt is readMetaData */
        assert(pkt->getSize() == 64);
        panic_if(!mem_intr->getAddrRange().contains(pkt->getAddr()),
                 "Can't handle address range for packet %s\n", pkt->print());
        std::vector<uint8_t> metaData(64, 0);

        PPN ppn = pkt->getAddr() / 64;

        // if (ppn == pageNum) {
        //     metaData = mPageBuffer;
        // } else {
        //     mem_intr->atomicRead(metaData.data(), pkt->getAddr(), 64);
        // }
        mem_intr->atomicRead(metaData.data(), pkt->getAddr(), 64);

        /* finish read the metaData */
        if (!isValidMetaData(metaData)) {
            assert(pageNum != ppn);
            // flush the pageBuffer in memory
            blockedForCompr = true;
            PacketPtr writeForCompress = new Packet(pkt, auxPkt->comprTick - 1);
            pkt->ref_cnt++;

            typeTwoBlock += 1;
            DPRINTF(MemCtrl, "(pkt) Line %d, the writeForCompress pkt %lx\n", __LINE__, writeForCompress);

            std::vector<uint8_t> uncompressPage(4096);
            std::vector<uint8_t> mPageBuffer(64, 0);
            mem_intr->atomicRead(mPageBuffer.data(), pageNum * 64, 64);

            for (int u = 0; u < 64; u++) {
                std::vector<uint8_t> curCL(64, 0);
                mem_intr->atomicRead(curCL.data(), pageBufferAddr + 64 * u, 64);
                // memcpy(curCL.data(), pageBuffer.data() + 64 * u, 64);
                restoreData(curCL, getType(mPageBuffer, u));
                assert(curCL.size() == 64);
                memcpy(uncompressPage.data() + 64 * u, curCL.data(), 64);
            }

            // finialize the mPageBuffer
            uint64_t cPageSize = 0;
            for (uint8_t u = 0; u < 64; u++) {
                uint8_t type = getType(mPageBuffer, u);
                uint8_t dataLen = sizeMap[type];
                cPageSize += dataLen;
            }
            assert(cPageSize <= 4096);

            uint64_t cur = 0;
            while (cur < cPageSize) {
                assert(freeList.size() > 0);
                Addr chunkAddr = freeList.front();
                DPRINTF(MemCtrl, "Line %d, freeList pop\n", __LINE__);
                freeList.pop_front();

                int chunkIdx = cur / 512;

                uint64_t MPFN = (chunkAddr >> 9);
                for (int u = 3; u >= 0; u--) {   // 4B per chunk
                    uint8_t val = MPFN & 0xFF;
                    MPFN >>= 8;
                    mPageBuffer[2 + chunkIdx * 4 + u] = val;
                }
                if (cur + 512 < cPageSize) {
                    cur += 512;
                } else {
                    break;
                }
            }
            stat_used_bytes += cPageSize;

            // store the size of compressedPage into the control block (using 12 bit)
            uint64_t compressedSize = cPageSize;

            mPageBuffer[1] = compressedSize & (0xFF);
            mPageBuffer[0] = mPageBuffer[0] | ((compressedSize >> 8) & 0xF);

            // DPRINTF(MemCtrl, "the final metadata is: \n");
            // for (int k = 0; k < 64; k++) {
            //     printf("%02x",static_cast<unsigned>(mPageBuffer[k]));

            // }
            // printf("\n");

            writeForCompress->configAsWriteForCompress(uncompressPage.data(), pageNum);
            writeForCompress->comprMetaDataMap[pageNum] = mPageBuffer;

            waitQueue.insert(writeForCompress);
            writeForCompress->ref_cnt++;

            mem_intr->atomicWrite(mPageBuffer, pageNum * 64, 64);

            mcache.add(pageNum * 64, mPageBuffer);

            updateSubseqMetaData(writeForCompress, pageNum);

            initialPageBuffer(ppn);

            assert(auxPkt->comprMetaDataMap.find(ppn) != auxPkt->comprMetaDataMap.end());
            std::vector<uint8_t> curMD(64, 0);
            mem_intr->atomicRead(curMD.data(), ppn * 64, 64);
            auxPkt->comprMetaDataMap[ppn] = curMD;

            assignToQueue(writeForCompress);

        } else {
            assert(auxPkt->comprMetaDataMap.find(ppn) != auxPkt->comprMetaDataMap.end());
            auxPkt->comprMetaDataMap[ppn] = metaData;
            unsigned size = auxPkt->comprBackup->getSize();
            unsigned offset = auxPkt->getAddr() & (burst_size - 1);
            unsigned int pkt_count = divCeil(offset + auxPkt->getSize(), burst_size);

            if (auxPkt->IncreEntryCnt()) {
                /* auxPkt is ready for process */
                auxPkt->comprIsReady = true;
                assignToQueue(auxPkt, true);
            }
        }
        assert(pkt->ref_cnt > 0);
        pkt->ref_cnt--;
        if (pkt->ref_cnt == 0) {
            assert(pkt->comprBackup->ref_cnt > 0);
            pkt->comprBackup->ref_cnt--;
            if (pkt->comprBackup->ref_cnt == 0) {
                delete pkt->comprBackup;
            }
            delete pkt;
        }

    } else if (pType == 0x8) {
        /* writeMetaData */
        panic("should never enter this");

    } else if (pType == 0x10) {
        /* readForCompress */
        assert(pkt->getSize() == 4096);
        assert((pkt->getAddr() & 0xFFF) == 0);
        assert(pkt->comprBackup->isWrite());
        PPN ppn = (pkt->getAddr() >> 12 & ((1ULL << 52) - 1));
        pkt->checkIfValid();
        // mem_intr->accessForCompr(pkt, burst_size, pageNum, pageBuffer, mPageBuffer);
        mem_intr->accessForCompr(pkt, burst_size, pageNum, pageBufferAddr);

        std::vector<uint8_t> newMetaData = recompressTiming(pkt);
        assert(pkt->comprTick != 0);

        PacketPtr writeForCompress = new Packet(pkt->comprBackup, pkt->comprTick);
        pkt->comprBackup->ref_cnt++;

        DPRINTF(MemCtrl, "(pkt) Line %d, the writeForCompress pkt %lx\n", __LINE__, reinterpret_cast<uint64_t>(writeForCompress));
        uint8_t* start_addr = pkt->getPtr<uint8_t>();
        writeForCompress->configAsWriteForCompress(start_addr, ppn);
        writeForCompress->comprMetaDataMap[ppn] = newMetaData;

        // TODO2: right now just instantly update the metadata in memory
        // printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, ppn, ppn * 64);
        mcache.add(ppn * 64, newMetaData);
        mem_intr->atomicWrite(newMetaData, ppn * 64, 64);
        DPRINTF(MemCtrl, "Line %d, enter the update subseqMetadata\n", __LINE__);
        updateSubseqMetaData(writeForCompress, ppn);

        /* readForCompress has finished its life time */
        if (recordForCheckReady == false) {
            /* remove the readForCompress from the waitQueue */
            waitQueue.erase(pkt);
            assert(pkt->ref_cnt > 0);
            pkt->ref_cnt--;

            /* add the writeForCompress to the waitQueue */
            waitQueue.insert(writeForCompress);
            writeForCompress->ref_cnt++;

            assignToQueue(writeForCompress);
        } else {
            // printf("record is true\n");
            assert(writeForCompress->comprIsProc == false && writeForCompress->comprIsReady);
            to_delete_pkt.emplace_back(pkt);
            to_add_pkt.emplace_back(writeForCompress);
        }

        assert(pkt->ref_cnt > 0);
        pkt->ref_cnt--;
        if (pkt->ref_cnt == 0) {
            assert(pkt->comprBackup->ref_cnt > 1);
            pkt->comprBackup->ref_cnt--;
            delete pkt;
        }

    } else if (pType == 0x20) {
        /* writeForCompress */
        assert(pkt->getSize() == 4096);
        PacketPtr backup_pkt = pkt->comprBackup;

        // mem_intr->accessForCompr(pkt, burst_size, pageNum, pageBuffer, mPageBuffer);
        mem_intr->accessForCompr(pkt, burst_size, pageNum, pageBufferAddr);

        if (recordForCheckReady == false) {
            waitQueue.erase(pkt);
            assert(pkt->ref_cnt > 0);
            pkt->ref_cnt--;
            checkForReadyPkt();
        } else {
            to_delete_pkt.emplace_back(pkt);
        }

        if (backup_pkt->getPType() == 0x4) {
            /* backup is readMetaData, the writeForCompress is triggered by replace the pageBuffer */
            assert(typeTwoBlock >= 1);
            typeTwoBlock--;
            PacketPtr auxPkt = backup_pkt->comprBackup;
            PPN ppn = backup_pkt->getAddr() / 64;
            assert(auxPkt->comprMetaDataMap.find(ppn) != auxPkt->comprMetaDataMap.end());

            unsigned size = auxPkt->comprBackup->getSize();
            unsigned offset = auxPkt->getAddr() & (burst_size - 1);
            unsigned int pkt_count = divCeil(offset + auxPkt->getSize(), burst_size);
            if (auxPkt->IncreEntryCnt()) {
                /* auxPkt is ready for process */
                auxPkt->comprIsReady = true;
                assignToQueue(auxPkt);
            }

            assert(pkt->ref_cnt > 0);
            pkt->ref_cnt--;
            if (pkt->ref_cnt == 0) {
                assert(backup_pkt->ref_cnt > 0);
                backup_pkt->ref_cnt--;
                if (backup_pkt->ref_cnt == 0) {
                    assert(backup_pkt->comprBackup->ref_cnt > 0);
                    backup_pkt->comprBackup->ref_cnt--;
                    if (backup_pkt->comprBackup->ref_cnt == 0) {
                        delete backup_pkt->comprBackup;
                    }
                    delete backup_pkt;
                }
                delete pkt;
            }

        } else {
            assert(backup_pkt->getPType() == 0x2);
            assert(backup_pkt->comprBackup);

            Addr backup_addr = backup_pkt->getAddr();
            uint64_t backup_size = backup_pkt->getSize();

            assert(backup_addr >= pkt->getAddr() && (backup_addr + backup_size <= pkt->getAddr() + 4096));
            assert(typeOneBlock >= 1);
            typeOneBlock--;

            PacketPtr real_recv_pkt = backup_pkt->comprBackup;

            DPRINTF(MemCtrl, "Responding to pkt 0x%lx\n", real_recv_pkt);
            DPRINTF(MemCtrl, "Responding to Address %#x.. \n", real_recv_pkt->getAddr());

            bool needsResponse = real_recv_pkt->needsResponse();
            // do the actual memory access which also turns the packet into a
            // response
            panic_if(!mem_intr->getAddrRange().contains(real_recv_pkt->getAddr()),
                    "Can't handle address range for packet %s\n", real_recv_pkt->print());

            /* make the backup_pkt 64Byte-Aligned */
            const Addr base_addr = backup_pkt->getAddr();
            /* prepare the auxiliary information */
            Addr addrAligned = (base_addr >> 6) << 6;

            uint64_t new_size = ((((base_addr + backup_pkt->getSize()) + (burst_size - 1)) >> 6) << 6) - addrAligned;

            std::vector<uint8_t> newData(new_size, 0);

            uint8_t* cur_page = pkt->getPtr<uint8_t>();
            uint64_t offsetInPages = addrAligned - pkt->getAddr();
            assert(offsetInPages + new_size <= 4096);
            memcpy(newData.data(), cur_page + offsetInPages, new_size);

            backup_pkt->setAddr(addrAligned);
            backup_pkt->setSizeForMC(new_size);
            backup_pkt->allocateForMC();
            backup_pkt->setDataForMC(newData.data(), 0, new_size);

            mem_intr->accessForCompr(backup_pkt, burst_size, pageNum, pageBufferAddr);   // Also this will not change anything, but we need this to update the architectural status
            // mem_intr->accessForCompr(backup_pkt, burst_size, pageNum, pageBuffer, mPageBuffer);   // Also this will not change anything, but we need this to update the architectural status

            if (recordForCheckReady == false) {
                waitQueue.erase(backup_pkt);
                assert(backup_pkt->ref_cnt > 0);
                backup_pkt->ref_cnt--;

                checkForReadyPkt();
            } else {
                to_delete_pkt.emplace_back(backup_pkt);
            }

            unsigned size = real_recv_pkt->getSize();
            unsigned offset = real_recv_pkt->getAddr() & (burst_size - 1);
            unsigned int pkt_count = divCeil(offset + size, burst_size);

            if (backup_pkt->isWrite()) {
                curWriteNum -= pkt_count;
            } else {
                panic("a read request should never incur a pageoverflow\n");
            }

            // turn packet around to go back to requestor if response expected
            if (needsResponse) {
                // access already turned the packet into a response
                assert(real_recv_pkt->isResponse());
                // response_time consumes the static latency and is charged also
                // with headerDelay that takes into account the delay provided by
                // the xbar and also the payloadDelay that takes into account the
                // number of data beats.
                Tick response_time = curTick() + static_latency + real_recv_pkt->headerDelay +
                                    real_recv_pkt->payloadDelay;
                // Here we reset the timing of the packet before sending it out.
                real_recv_pkt->headerDelay = real_recv_pkt->payloadDelay = 0;

                // queue the packet in the response queue to be sent out after
                // the static latency has passed
                port.schedTimingResp(real_recv_pkt, response_time);
            } else {
                // @todo the packet is going to be deleted, and the MemPacket
                // is still having a pointer to it
                pendingDelete.reset(real_recv_pkt);
            }

            assert(pkt->ref_cnt > 0);
            pkt->ref_cnt--;
            if (pkt->ref_cnt == 0) {
                assert(backup_pkt->ref_cnt > 0);
                backup_pkt->ref_cnt--;
                if (backup_pkt->ref_cnt == 0) {
                    delete backup_pkt;
                }
                delete pkt;
            }
        }

        if (typeOneBlock + typeTwoBlock == 0) {
            blockedForCompr = false;
            blockedNum = 0;

            /* the memory controller is no longer blocked */
            /* reprocess the blocked pkt */
            unsigned int u = 0;
            for (; u < blockPktQueue.size();) {
                PacketPtr blocked_pkt = blockPktQueue[u];
                // printf("the address is blocked pkt is %lx\n", reinterpret_cast<uint64_t>(blocked_pkt));
                bool is_accepted = recvTimingReqLogicForCompr(blocked_pkt, true);
                if (!is_accepted) {
                    panic("should be always accept at this moment");
                }
                u++;
                if (blockedForCompr) {
                    break;
                }
            }
            blockPktQueue.erase(blockPktQueue.begin(), blockPktQueue.begin() + u);
        }

    } else {
        panic("unknown pkt type in accessAndRespondForCompr");
    }
    return;
}

void
MemCtrl::accessAndRespondForDyL(PacketPtr pkt, Tick static_latency,
                                                MemInterface* mem_intr)
{
    assert(pkt->DyLPType != 0x1);

    if (pkt->DyLPType != 0x2) {
        /* the pkt is not auxPkt, i.e. the pkt is especially created by the memory controller */
        int type = pkt->DyLPType;
        uint32_t burst_size = dram->bytesPerBurst();
        Addr base_addr = pkt->getAddr();

        /* get the information from the original packet so that we could locate the 8-Byte CTE*/
        PacketPtr aux_pkt = pkt->DyLCandidate;

        PPN ppn = 0;
        if (type == 0x10) {
            /* for readUncompress, the ppn is not original pkt's ppn */
            ppn = pkt->compressPageId;
        } else {
            assert(aux_pkt->getAddr() == aux_pkt->DyLCandidate->getAddr());
            ppn = (aux_pkt->getAddr()) >> 12;
        }
        // DPRINTF(MemCtrl, "Line %d: step 3\n", __LINE__);
        Addr cteAddr = startAddrForCTE + ppn * 8;
        Addr cteAddrAligned = (cteAddr >> 6) << 6;
        uint8_t loc = (cteAddr >> 3) & ((1 << 3) - 1);
        assert (loc < 8);

        if (type == 0x04) {   // readCompressed

            /* decompress the page*/
            uint64_t bufferSize = pkt->getSize();
            uint8_t* buffer = new uint8_t[bufferSize];
            mem_intr->atomicRead(buffer, base_addr, bufferSize);
            std::vector<uint8_t> dPage = decompressPage(buffer, bufferSize);
            delete []buffer;
            if (bufferSize <= 256) {
                smallFreeList.push_back(base_addr);
                stat_used_bytes -= 256;
            } else if (bufferSize <= 1024) {
                moderateFreeList.push_back(base_addr);
                stat_used_bytes -= 1024;
            } else {
                assert(bufferSize <= 2048);
                largeFreeList.push_back(base_addr);
                stat_used_bytes -= 2048;
            }

            assert(dPage.size() == 4096);
            assert(freeList.size() > 0);

            assert(delayForDecompress.find(aux_pkt) == delayForDecompress.end());
            if (isAddressCovered(aux_pkt->getAddr(), 0, 1)) {
                printf("the address of origin pkt is 0x%lx\n", reinterpret_cast<unsigned long>(aux_pkt));
                printf("the address of readCompress is 0x%lx\n", reinterpret_cast<unsigned long>(pkt));
            }
            delayForDecompress[aux_pkt] = curTick() + decompress_latency;
            assert(decompressedPage.find(aux_pkt) == decompressedPage.end());
            decompressedPage[aux_pkt] = dPage;

            delete pkt;

            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
                schedule(nextReqEvent, curTick());
            }

        } else if (type == 0x10) { // readUncompressed

            /* step 1: compress the cold page */

            uint64_t bufferSize = 4096;
            std::vector<uint8_t> buffer(bufferSize, 0);

            mem_intr->atomicRead(buffer.data(), base_addr, bufferSize);

            std::vector<uint8_t> cPage = compressPage(buffer.data(), bufferSize);
            uint64_t cSize = cPage.size();
            potentialRecycle--;

            if(cSize <= 2048) {
                Addr newAddr = 0;
                freeList.push_back(pkt->getAddr());  // return back the freed page
                assert(stat_used_bytes >= 4096);
                stat_used_bytes -= 4096;
                if (coverageTestMC(pkt->getAddr(), 0, 4096)) {
                    printf("give back freed page, address is 0x%lx\n", pkt->getAddr());
                    printf("the page number is %d\n", ppn);
                    if (findSameElem(pkt->getAddr())) {
                        printf("line %d: the address is 0x%lx\n", __LINE__, pkt->getAddr());
                        panic("deprecate?");

                    }
                }
                if (cSize <= 256) {
                    if (smallFreeList.size() > 0) {
                        newAddr = smallFreeList.front();
                        smallFreeList.pop_front();
                    } else {
                        newAddr = freeList.front();
                        freeList.pop_front();
                        for (int i = 1; i < 16; i++) {
                            smallFreeList.push_back(newAddr | (i << 8));
                        }
                    }
                    stat_used_bytes += 256;
                } else if (cSize <= 1024) {
                    if (moderateFreeList.size() > 0) {
                        newAddr = moderateFreeList.front();
                        moderateFreeList.pop_front();
                    } else {
                        newAddr = freeList.front();
                        freeList.pop_front();
                        for (int i = 1; i < 4; i++) {
                            moderateFreeList.push_back(newAddr | (i << 10));
                        }
                    }
                    stat_used_bytes += 1024;
                } else {
                    if (largeFreeList.size() > 0) {
                        newAddr = largeFreeList.front();
                        largeFreeList.pop_front();
                    } else {
                        newAddr = freeList.front();
                        freeList.pop_front();
                        largeFreeList.push_back(newAddr | (1 << 11));

                    }
                    stat_used_bytes += 2048;
                }
                // update the CTE and cache
                /* generate a write packet for update the CTE */
                PacketPtr writeCTE = new Packet(aux_pkt);

                writeCTE->configAsWriteCTE(cteAddr, aux_pkt, 8);
                uint8_t* dataPtr = writeCTE->getPtrForMC<uint8_t>();
                uint64_t newCTE = (1ULL << 63) | (1ULL << 62) | (((cSize - 1) & ((1ULL << 12) - 1)) << 50) | ((newAddr >> 8) << 10);

                if (isAddressCovered((ppn << 12), 8, 1)) {
                    printf("ReadForCompress: The new cte for the page %d is 0x%lx\n", ppn, newCTE);
                    printf("new address is 0x%lx\n", newAddr);
                }
                for (int i = 7; i >= 0; i--){
                    dataPtr[i] = newCTE & 0xFF;
                    newCTE = newCTE >> 8;
                }
                /* update the cache if necessary */
                if (mcache.isExist(cteAddrAligned)) {
                    std::vector<uint8_t> cacheLine = mcache.find(cteAddrAligned);
                    for (unsigned int i = 0; i < 8; i++) {
                        cacheLine[loc * 8 + i] = dataPtr[i];
                    }
                    // printf("Line %d, update the cache\n", __LINE__);
                    // printf("the align address is 0x%lx\n", cteAddrAligned);
                    // printf("the loc is %d\n", loc);
                    // for (int qw = 0; qw < 8; qw++) {
                    //     uint64_t forTest = 0;
                    //     for (int zx = 0; zx < 8; zx++) {
                    //         forTest = (forTest << 8) | (cacheLine[qw *8 + zx] & 0xFF);
                    //     }
                    //     printf("cte is 0x%lx\n", forTest);
                    // }
                    mcache.updateIfExist(cteAddrAligned, cacheLine);
                }
                unsigned wcte_offset = writeCTE->getAddr() & (burst_size - 1);
                unsigned int wcte_pkt_count = divCeil(wcte_offset + writeCTE->getSize(), burst_size);
                assert(wcte_pkt_count == 1);
                addToWriteQueueForDyL(writeCTE, wcte_pkt_count, mem_intr);

                /* write the compressed page to the new location*/
                PacketPtr writeCompress = new Packet(aux_pkt);

                writeCompress->configAsWriteCompress(newAddr, cSize, aux_pkt, cPage);
                unsigned wc_offset = writeCompress->getAddr() & (burst_size - 1);
                unsigned int wc_pkt_count = divCeil(wc_offset + cSize, burst_size);
                addToWriteQueueForDyL(writeCompress, wc_pkt_count, mem_intr);

                delete pkt;
                assert(aux_pkt);
                assert(aux_pkt->usedForComp == 1);
                delete aux_pkt;
            }


            assert(recencyList.size() <= recencyListThreshold);
            blockedForDyL = false;
            if (pktInProcess == 0) {
                for (const auto &pkt: functionalBlockedQueueForDyL) {
                    // printf("Line %d: re process functional request: 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(pkt));
                    recvFunctionalLogicForDyL(pkt, mem_intr);
                }
                functionalBlockedQueueForDyL.clear();
                functionalBlockedForDyL = false;
            }
            pagesInCompress.erase(ppn);
            if (functionalBlockedForDyL == false) {
                assert(!blockedForDyL);
                while (!blockedQueueForDyL.empty() && !blockedForDyL) {
                    PacketPtr pkt = blockedQueueForDyL.front();
                    unsigned size = pkt->getSize();
                    uint32_t burst_size = dram->bytesPerBurst();

                    unsigned offset = pkt->getAddr() & (burst_size - 1);
                    unsigned int pkt_count = divCeil(offset + size, burst_size);
                    blockedQueueForDyL.pop_front();
                    blockedNumForDyL -= pkt_count;
                    bool isAccepted = recvTimingReqLogicForDyL(pkt, true);
                    assert(isAccepted);  // should be always accepted at this time
                }
            }

        } else if (type == 0x40) {  // readCTE
            assert(pkt->getSize() == 64);

            /* read the cacheLine first */
            std::vector<uint8_t> cteCL(64);
            mem_intr->atomicRead(cteCL.data(), pkt->getAddr(), pkt->getSize());

            // printf("cte address align is 0x%lx\n", pkt->getAddr());
            // for (int qw = 0; qw < 8; qw++) {
            //     uint64_t forTest = 0;
            //     printf("the page ID is %d\n", (pkt->getAddr() - startAddrForCTE) / 8);
            //     for (int zx = 0; zx < 8; zx++) {
            //         forTest = (forTest << 8) | (cteCL[qw *8 + zx] & 0xFF);
            //     }
            //     printf("Cte is 0x%lx\n", forTest);
            // }
            // printf(">>>>>>>>>>>>  cache add mew entry\n");

            mcache.add(pkt->getAddr(), cteCL);
            // DPRINTF(MemCtrl, "after that adding, the size of mcache is %d\n", mcache.getSize());

            /* get the information from the original packet so that we could locate the 8-Byte CTE*/
            PPN ppn = (aux_pkt->getAddr()) >> 12;
            Addr cteAddr = startAddrForCTE + ppn * 8;
            Addr cteAddrAligned = (cteAddr >> 6) << 6;
            uint8_t loc = (cteAddr >> 3) & ((1 << 3) - 1);
            assert (loc < 8);;

            /* generate the CTE and the dram address */
            uint64_t cte = 0;
            for (unsigned int i = (loc * 8); i < (loc + 1) * 8; i++) {
                cte = (cte << 8) | cteCL[i];
            }
            if (isAddressCovered(aux_pkt->getAddr(), pkt->getSize(), 1)) {
                printf("Line %d, the CTE of page %d is 0x%lx\n", __LINE__, ppn, cte);
            }

            Addr addr = ((cte >> 32) & ((1ULL << 30) - 1)) << 12;

            /* There could be some cases when CTE is invalid */

            assert(pkt->getAddr() == cteAddrAligned);
            // DPRINTF(MemCtrl, "Line %d: The cte is 0x%llx\n", __LINE__, cte);
            // DPRINTF(MemCtrl, "Line %d: Read from the address 0x%llx, loc: %d\n", __LINE__, pkt->getAddr(), loc);
            // DPRINTF(MemCtrl, "Line %d: The address of origin pkt is 0x%lx\n", __LINE__, (uint64_t)origin_pkt);

            if (((cte >> 63) & 0x1) == 0) {
                // allocate a new page
                if (isAddressCovered(aux_pkt->getAddr(), 8, 1)) {
                    printf("need to allocate a new page\n");
                    printf("freeList.size() is %d\n", freeList.size());
                }
                assert(freeList.size() > 0);
                addr = freeList.front();
                freeList.pop_front();
                stat_used_bytes += 4096;

                std::vector<uint8_t> zeroPage(4096, 0);
                mem_intr->atomicWrite(zeroPage, addr, 4096, 0);
                cte = (1ULL << 63) | (0ULL << 62) | (((addr >> 12) & ((1ULL << 30) - 1)) << 32);
                for (int i = (loc * 8 + 7); i >= loc * 8; i--) {
                    cteCL[i] = cte & ((1ULL << 8) - 1);
                    cte = cte >> 8;
                }
                PacketPtr writeCTE = new Packet(aux_pkt);


                writeCTE->configAsWriteCTE(pkt->getAddr(), aux_pkt, 64);
                uint8_t* data = writeCTE->getPtr<uint8_t>();
                for (unsigned int i = 0; i < 64; i++) {
                    data[i] = cteCL[i];
                }
                assert((writeCTE->getAddr() & ((1 << 6) - 1)) == 0);
                addToWriteQueueForDyL(writeCTE, 1, mem_intr);
            }

            if (((cte >> 62) & 0x1) == 1) {
                /* the current page is compressed */
                if (pagesInDecompress.find(ppn) != pagesInDecompress.end()) {
                    // panic("not implement yet[C]");
                    waitForDeCompress.push_back(aux_pkt);
                } else {
                    bool sign = false;
                    if (isAddressCovered(aux_pkt->getAddr(), 8, 1)) {
                        printf("After read CTE: Opps, the page is compressed\n");
                        printf("the ppn is %d\n", ppn);
                        printf("the aux pkt is 0x%lx\n", reinterpret_cast<unsigned long>(aux_pkt));
                    }
                    assert(pagesInDecompress.find(ppn) == pagesInDecompress.end());
                    assert(pagesInCompress.find(ppn) == pagesInCompress.end());

                    pagesInDecompress.insert(ppn);
                    /* the MC should issue readCompress and then writeUncompress packet to decompress the page */
                    PacketPtr readCompress = new Packet(aux_pkt);

                    if (isAddressCovered(aux_pkt->getAddr(), 0, 1)) {
                        printf("create a readCompress 3: aux pkt address is 0x%lx\n", reinterpret_cast<unsigned long>(aux_pkt));
                    }

                    DPRINTF(MemCtrl, "Line %d: create a new packet for read Compress, the address is 0x%llx\n", __LINE__, (uint64_t)readCompress);

                    /* read the dram address and the page size from CTE */
                    Addr addr = ((cte >> 10) & ((1ULL << 40) - 1)) << 8;
                    uint64_t pageSize = ((cte >> 50) & ((1ULL << 12) - 1)) + 1;   // size range [1, 2kiB]

                    readCompress->configAsReadCompress(addr, pageSize, aux_pkt);
                    unsigned rc_offset = (readCompress->getAddr()) & (burst_size - 1);
                    unsigned int rc_pkt_count = divCeil(rc_offset + pageSize, burst_size);
                    sign = addToReadQueueForDyL(readCompress, rc_pkt_count, dram);

                    if (!sign) {
                        if (!nextReqEvent.scheduled()) {
                            DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
                            schedule(nextReqEvent, curTick());
                        }
                    }
                }
            } else {
                /* translate the address for the original packet */
                Addr real_addr = addr | (aux_pkt->getAddr() & ((1ULL << 12) - 1));
                aux_pkt->setAddr(real_addr);
                // DPRINTF(MemCtrl, "Line %d: Finish tranlate\n", __LINE__);

                unsigned origin_offset = aux_pkt->getAddr() & (burst_size - 1);
                uint64_t size = aux_pkt->getSize();
                unsigned int origin_pkt_count = divCeil(origin_offset + size, burst_size);

                /* add the origin pkt to the specific queue */
                bool sign = false;
                if (aux_pkt->isWrite()) {
                    addToWriteQueueForDyL(aux_pkt, origin_pkt_count, dram);
                    stats.writeReqs++;
                    stats.bytesWrittenSys += size;

                    // If we are not already scheduled to get a request out of the
                    // queue, do so now
                    if (!sign) {
                        if (!nextReqEvent.scheduled()) {
                            DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
                            schedule(nextReqEvent, curTick());
                        }
                    }
                } else {
                    assert(aux_pkt->isRead());
                    sign = addToReadQueueForDyL(aux_pkt, origin_pkt_count, dram);
                    stats.readReqs++;
                    stats.bytesReadSys += size;

                    if (!sign) {
                        // If we are not already scheduled to get a request out of the
                        // queue, do so now
                        if (!nextReqEvent.scheduled()) {
                            DPRINTF(MemCtrl, "Request scheduled immediately\n");
                            schedule(nextReqEvent, curTick());
                        }
                    }
                }
            }

        } else {
            panic("[Timing Access] The packet should not have another type\n");
        }
    } else {
        DPRINTF(MemCtrl, "Responding to Address %#x.. \n", pkt->getAddr());

        PacketPtr origin_pkt = pkt->DyLCandidate;
        assert(origin_pkt->DyLBackup == origin_pkt->getAddr());
        origin_pkt->setAddr(pkt->getAddr());

        bool needsResponse = origin_pkt->needsResponse();
        // do the actual memory access which also turns the packet into a
        // response
        panic_if(!mem_intr->getAddrRange().contains(origin_pkt->getAddr()),
                "Can't handle address range for packet %s\n", origin_pkt->print());
        mem_intr->accessForDyL(origin_pkt, pkt);

        if (origin_pkt->isWrite()) {
            assert(std::find(inProcessWritePkt.begin(), inProcessWritePkt.end(), pkt) != inProcessWritePkt.end());
            inProcessWritePkt.remove(pkt);
        } else {
            /* update the read data if it collide with the pending function write [it is weird though] */
            for (const auto& pkt: functionalBlockedQueueForDyL) {
                if (pkt->isWrite()) {
                    Addr writePktStart = pkt->getAddr();
                    Addr writePktEnd = writePktStart + pkt->getSize();
                    Addr pktStart = origin_pkt->getAddr();
                    Addr pktEnd = pktStart + origin_pkt->getSize();
                    if ((writePktStart < pktEnd) && (pktStart < writePktEnd)) {
                        printf("Timing read collide with Func. write\n");
                        Addr overlap_start = std::max(writePktStart, pktStart);
                        Addr overlap_end = std::min(writePktEnd, pktEnd);
                        size_t overlap_len = overlap_end - overlap_start;
                        size_t pkt_offset = overlap_start - pktStart;
                        size_t write_pkt_offset = overlap_start - writePktStart;
                        std::memcpy(
                            origin_pkt->getPtr<uint8_t>() + pkt_offset,
                            pkt->getPtr<uint8_t>() + write_pkt_offset,
                            overlap_len
                        );
                    }
                }
            }
        }
        assert(pktInProcess > 0);
        pktInProcess--;
        if (pktInProcess == 0 && !blockedForDyL) {
            for (const auto& pkt : functionalBlockedQueueForDyL) {
                // printf("Line %d: re process functional request: 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(pkt));
                recvFunctionalLogicForDyL(pkt, mem_intr);
            }
            functionalBlockedQueueForDyL.clear();
            functionalBlockedForDyL = false;

            assert(!blockedForDyL);
            while (!blockedQueueForDyL.empty() && !blockedForDyL) {
                PacketPtr pkt = blockedQueueForDyL.front();
                unsigned size = pkt->getSize();
                uint32_t burst_size = dram->bytesPerBurst();

                unsigned offset = pkt->getAddr() & (burst_size - 1);
                unsigned int pkt_count = divCeil(offset + size, burst_size);
                blockedQueueForDyL.pop_front();
                blockedNumForDyL -= pkt_count;
                bool isAccepted = recvTimingReqLogicForDyL(pkt, true);
                assert(isAccepted);  // should be always accepted at this time
            }
        } else if (pktInProcess == 0 && blockedForDyL) {
            compressColdPage(pkt, mem_intr);
        }
        // turn packet around to go back to requestor if response expected
        if (needsResponse) {
            // access already turned the packet into a response
            assert(origin_pkt->isResponse());
            // response_time consumes the static latency and is charged also
            // with headerDelay that takes into account the delay provided by
            // the xbar and also the payloadDelay that takes into account the
            // number of data beats.
            Tick response_time = curTick() + static_latency + origin_pkt->headerDelay +
                                origin_pkt->payloadDelay;
            // Here we reset the timing of the packet before sending it out.
            origin_pkt->headerDelay = origin_pkt->payloadDelay = 0;


            DPRINTF(MemCtrl, "[access and response] the pkt getAddress is 0x%llx\n", origin_pkt->getAddr());
            DPRINTF(MemCtrl, "[access and response] the address of auxPkt is 0x%lx\n", (uint64_t)pkt);
            // assert(pkt->getAddr() != 0);
            // queue the packet in the response queue to be sent out after
            // the static latency has passed
            port.schedTimingResp(origin_pkt, response_time);
        } else {
            // @todo the packet is going to be deleted, and the MemPacket
            // is still having a pointer to it
            pendingDelete.reset(origin_pkt);
        }

        if (pkt->usedForComp == 0) {
            delete pkt;
        }
        DPRINTF(MemCtrl, "Done\n");
    }

    return;
}

void
MemCtrl::accessAndRespondForNew(PacketPtr pkt, Tick static_latency,
                                                MemInterface* mem_intr) {
    uint32_t burst_size = mem_intr->bytesPerBurst();

    if (pkt->newPType == 0x02) {
        /* aux pkt */
        assert(pkt->new_backup);
        PacketPtr real_recv_pkt = pkt->new_backup;

        bool needsResponse = real_recv_pkt->needsResponse();

        panic_if(!mem_intr->getAddrRange().contains(real_recv_pkt->getAddr()),
                 "Can't handle address range for packet %s\n", real_recv_pkt->print());

        unsigned size = real_recv_pkt->getSize();
        unsigned offset = real_recv_pkt->getAddr() & (burst_size - 1);
        unsigned int pkt_count = divCeil(offset + size, burst_size);

        if (pkt->isWrite()) {
            curWriteNum -= pkt_count;
            assert(std::find(inProcessWritePkt.begin(), inProcessWritePkt.end(), pkt) != inProcessWritePkt.end());
            inProcessWritePkt.remove(pkt);
        } else {
            assert(pkt->isRead());
            assert(real_recv_pkt->getSize() == pkt->getSize());
            curReadNum -= pkt_count;
            memcpy(real_recv_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), real_recv_pkt->getSize());
        }

        mem_intr->accessForNew(pkt, 0);

        // turn packet around to go back to requestor if response expected
        if (needsResponse) {
            // access already turned the packet into a response
            assert(real_recv_pkt->isResponse());
            // response_time consumes the static latency and is charged also
            // with headerDelay that takes into account the delay provided by
            // the xbar and also the payloadDelay that takes into account the
            // number of data beats.
            Tick response_time = curTick() + static_latency + real_recv_pkt->headerDelay +
                                 real_recv_pkt->payloadDelay;
            // Here we reset the timing of the packet before sending it out.
            real_recv_pkt->headerDelay = real_recv_pkt->payloadDelay = 0;

            // queue the packet in the response queue to be sent out after
            // the static latency has passed
            port.schedTimingResp(real_recv_pkt, response_time);
        } else {
            // @todo the packet is going to be deleted, and the MemPacket
            // is still having a pointer to it
            pendingDelete.reset(real_recv_pkt);
        }

	    delete pkt;
        pktInProcess--;
        if (pktInProcess == 0) {
            if (blockedForNew) {
                assert(waitQueueForNew.size() > 0);
                readForRecompress(waitQueueForNew.front().first, mem_intr);
            }
        }
    } else if (pkt->newPType == 0x04) {
        /* sub pkt */
        PacketPtr aux_pkt = pkt->new_backup;

        Addr origin_addr = pkt->new_origin;  /* the origin address in OSPA space */

        mem_intr->accessForNew(pkt, 1);

        if (pkt->isRead()) {
            std::vector<uint8_t> metaData = pkt->newMetaData;
            uint8_t cacheLineIdx = (origin_addr >> 6) & 0x3F;
            uint8_t type = new_getType(metaData, cacheLineIdx);

            Addr new_addr = 0;
            uint8_t coverage = new_getCoverage(metaData);
            if (coverage <= cacheLineIdx) {
                assert(type == 0);
                assert(pkt->suffixLen == 0);
                new_addr = zeroAddr;
            } else {
                if (type >= 0b100) {
                    assert(pkt->suffixLen == 0);
                    uint8_t overflowIdx = *(pkt->getPtr<uint8_t>());
                    new_addr = calOverflowAddr(metaData, overflowIdx);
                } else {
                    new_addr = burstAlign(pkt->getAddr(), mem_intr) + burst_size;
                    if (pkt->suffixLen != 0) {
                        new_addr = pkt->newBlockAddr;
                    }
                }

            }
            Addr start_addr = (type >= 0b100)?(new_addr):(pkt->getAddr());
            PacketPtr readTwice = new Packet(pkt);
            readTwice->configAsReadTwice(pkt, new_addr, start_addr);

            if (!addToReadQueueForNew(readTwice, 1, dram)) {
                // If we are not already scheduled to get a request out of the
                // queue, do so now
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Request scheduled immediately\n");
                    schedule(nextReqEvent, curTick());
                }
            }

        } else {
            assert(pkt->isWrite());
            aux_pkt->new_subPktCnt--;
            if (aux_pkt->new_subPktCnt == 0) {
                accessAndRespondForNew(aux_pkt, static_latency, mem_intr);
            }
            delete pkt;
        }

    } else if (pkt->newPType == 0x08) {
        /* read Twice */
        PacketPtr sub_pkt = pkt->new_backup;
        PacketPtr aux_pkt = sub_pkt->new_backup;

        assert(sub_pkt->isRead());

        pkt->setAddr(pkt->new_origin);

        mem_intr->accessForNew(pkt, 1);

        if (isAddressCovered(aux_pkt->getAddr(), aux_pkt->getSize(), 1)) {
            printf("the mpa address is 0x%lx\n", pkt->new_origin);
            printf("the readed size is %d\n", pkt->getSize());
        }

        Addr origin_addr = sub_pkt->new_origin;

        std::vector<uint8_t> cacheLine(64, 0);
        uint8_t cacheLineIdx = (origin_addr >> 6) & 0x3F;
        uint8_t type = new_getType(sub_pkt->newMetaData, cacheLineIdx);

        memcpy(cacheLine.data(), pkt->getPtr<uint8_t>(), pkt->getSize());

        if (isAddressCovered(aux_pkt->getAddr(), aux_pkt->getSize(), 1)) {
            printf("before restore , the data is\n");
            for (int i = 0; i < pkt->getSize(); i++) {
                if (i % 8 == 0) {
                    printf("\n");
                }
                printf("%02x ", static_cast<unsigned int>(cacheLine[i]));
            }
            printf("\n");
        }

        new_restoreData(cacheLine, type);

        if (isAddressCovered(aux_pkt->getAddr(), aux_pkt->getSize(), 1)) {
            printf("the original address of sub-pkt is 0x%lx\n", origin_addr);
            printf("ppn is %d, the metadata is:\n", (origin_addr >> 12));
            for (int k = 0; k < 64; k++) {
                printf("%02x",static_cast<unsigned>(sub_pkt->newMetaData[k]));
            }
            printf("\n");
            printf("the readed cacheline is:");

            for (int i = 0; i < cacheLine.size(); i++) {
                if (i % 8 == 0) {
                    printf("\n");
                }
                printf("%02x ", static_cast<unsigned int>(cacheLine[i]));
            }
            printf("\n");
        }

        uint64_t cur_loc = origin_addr & 0x3F;
        uint64_t cur_size = std::min(64UL - cur_loc, aux_pkt->getAddr() + aux_pkt->getSize() - origin_addr);

        assert(aux_pkt->getAddr() <= origin_addr);

        uint64_t offset = origin_addr - aux_pkt->getAddr();
        if (offset != 0) {
            assert(cur_loc == 0);
        }
        memcpy(aux_pkt->getPtr<uint8_t>() + offset, cacheLine.data() + cur_loc, cur_size);

        aux_pkt->new_subPktCnt--;
        if (aux_pkt->new_subPktCnt == 0) {
            accessAndRespondForNew(aux_pkt, static_latency, mem_intr);
        }
        delete pkt;
        delete sub_pkt;

    } else if (pkt->newPType == 0x10) {
        /* readMetaData */
        PacketPtr sub_pkt = pkt->new_backup;

        assert(pkt->getSize() == 64);
        std::vector<uint8_t> metaData(64, 0);

        mem_intr->atomicRead(metaData.data(), pkt->getAddr(), 64);

        if (!isValidMetaData(metaData)) {
            metaData = originMetaData;
            new_allocateBlock(metaData, 1);
        }

        sub_pkt->newMetaData = metaData;

        /* update metadata when necessary */
        updateMetaDataForNew(sub_pkt, mem_intr);

        metaData = sub_pkt->newMetaData;

        mem_intr->atomicWrite(metaData, pkt->getAddr(), 64);
        mcache.add(pkt->getAddr(), metaData);

        assignToQueueForNew(sub_pkt);
        delete pkt;

    } else if (pkt->newPType == 0x20) {
        // readPage

        PPN target_page = pkt->new_targetPage;
        // printf("[FOR TEST] finish read the compressed page: ppn %d\n", target_page);
        Addr memory_addr = target_page * 64;

        std::vector<uint8_t> metaData(64, 0);
        mem_intr->atomicRead(metaData.data(), memory_addr, 64);

        recompressForNew(pkt, metaData);

        if (mcache.isExist(memory_addr)){
            mcache.updateIfExist(memory_addr, metaData);
        }
        mem_intr->atomicWrite(metaData, memory_addr, 64);

        blockedForNew = false;
        lastRecomprTick = curTick();
        blockedNum = 0;
        for (auto const& elem: waitQueueForNew) {
            uint8_t pkt_type = elem.second;
            PacketPtr pkt = elem.first;
            if (pkt_type == 1) {
                recvTimingReqLogicForNew(pkt, true);
            } else {
                recvFunctionalLogicForNew(pkt, mem_intr, true);
            }
        }
        waitQueueForNew.clear();

    } else if (pkt->newPType == 0x40) {
        // writeBlock
        PacketPtr readPage = pkt->new_backup;

        /* actually write the pkt */
        std::vector<uint8_t> data(pkt->getSize(), 0);
        memcpy(data.data(), pkt->getPtr<uint8_t>(), pkt->getSize());
        assert(pkt->getSize() == 512 || pkt->getSize() == 1024);

        // printf("[FOR TEST] write to the new block\n");
        // printf("the new address is 0x%lx, the size is %ld\n", pkt->getAddr(), pkt->getSize());

        mem_intr->atomicWrite(data, pkt->getAddr(), pkt->getSize());

        assert(readPage->new_subPktCnt > 0);
        readPage->new_subPktCnt--;
        if (readPage->new_subPktCnt == 0) {
            delete readPage;
        }

        delete pkt;
    } else if (pkt->newPType == 0x80) {
        // readBlock
        PacketPtr readPage = pkt->new_backup;
        uint32_t chunk_idx = pkt->pktIdx;
        assert(pkt->getSize() == pageSizeMap[chunk_idx + 1] - pageSizeMap[chunk_idx]);

        mem_intr->atomicRead(readPage->getPtr<uint8_t>() + pageSizeMap[chunk_idx], pkt->getAddr(), pkt->getSize());

        // printf("[LCF] chunkIdx is %d, the address is 0x%lx\n", chunk_idx, pkt->getAddr());

        // uint8_t* for_test_a = readPage->getPtr<uint8_t>() + pageSizeMap[chunk_idx];

        // for (int rt = 0; rt < pkt->getSize(); rt++) {
        //     if (rt % 8 == 0) {
        //         printf("\n");
        //     }
        //     printf("%02x ", static_cast<unsigned int>(*(for_test_a + rt)));
        // }
        // printf("\n");


        assert(readPage->new_subPktCnt > 0);
        readPage->new_subPktCnt--;
        if (readPage->new_subPktCnt == 0) {
            accessAndRespondForNew(readPage, static_latency, mem_intr);
        }
        delete pkt;
    } else {
        panic("wrong type");
    }
    return;
}

/*
    try to recycle pkt for secure based on the reference count
*/
void
MemCtrl::tryRecyclePkt(PacketPtr pkt, bool needDecrRefCnt) {
    if (needDecrRefCnt) {
        assert(pkt->ref_cnt > 0);
        pkt->ref_cnt--;
    }

    if (pkt->ref_cnt == 0) {
        delete pkt;
    }
}

/*
    initialize the metadata for secure when the corresponding page is first visited
*/
void
MemCtrl::initialMetaDataForSecure(std::vector<uint8_t>& metaDataEntry) {
    Addr chunk_addr = allocateChunkForSecure(1);

    // printf("chunk_addr is 0x%lx\n", chunk_addr);
    metaDataEntry[0] = 0x80;

    chunk_addr >>= 11;

    for (int i = 6; i >= 3; i--) {
        metaDataEntry[i] = chunk_addr & 0xFF;
        chunk_addr >>= 8;
    }
}


/*
    chunk_type=0: allocate 2048-Byte chunk
    chunk_type=1: allocate 4096-Byte chunk
*/
Addr
MemCtrl::allocateChunkForSecure(int chunk_type) {
    Addr chunk_addr = 0;
    if (chunk_type == 1) {
        if (largeChunkList.size() <= 0) {
            printf("the largeChunkList size is %d\n", largeChunkList.size());
            printf("the smallChunkList size is %d\n", smallChunkList.size());
            panic("the largeChunkList run out of capacaity 0\n");
        }
        chunk_addr = largeChunkList.front();
        largeChunkList.pop_front();
        stat_used_bytes += 4096;

        std::vector<uint8_t> zero_page(4096, 0);
        dram->atomicWrite(zero_page, chunk_addr, 4096);
    } else {
        if (smallChunkList.size() == 0) {
            if (largeChunkList.size() <= 0) {
                printf("the largeChunkList size is %d\n", largeChunkList.size());
                printf("the smallChunkList size is %d\n", smallChunkList.size());
                panic("the largeChunkList run out of capacaity 1\n");
            }

            chunk_addr = largeChunkList.front();
            largeChunkList.pop_front();
            smallChunkList.emplace_back(chunk_addr + 2048);
        } else {
            chunk_addr = smallChunkList.front();
            smallChunkList.pop_front();
        }
        stat_used_bytes += 2048;

        std::vector<uint8_t> zero_page(2048, 0);
        dram->atomicWrite(zero_page, chunk_addr, 2048);
    }
    return chunk_addr;
}

void
MemCtrl::recycleChunkForSecure(Addr chunk_addr, int chunk_type) {
    if (chunk_type == 1) {
        largeChunkList.emplace_back(chunk_addr);
        assert(stat_used_bytes >= 4096);
        stat_used_bytes -= 4096;
    } else {
        smallChunkList.emplace_back(chunk_addr);
        assert(stat_used_bytes >= 2048);
        stat_used_bytes -= 2048;
    }
}

void
MemCtrl::updateMetaDataForInProcessPkt(bool isEligible, PPN ppn, const std::vector<uint8_t>& metaData) {
    for (auto pkt: processPktListForSecure) {
        if (pkt->metaDataMapForSecure.find(ppn) != pkt->metaDataMapForSecure.end()) {
            if (!isEligible) {
                panic("the preceeding pkt is still in process, should not compress the page now\n");
            } else {
                pkt->metaDataMapForSecure[ppn] = metaData;
            }
        }
    }
}

void
MemCtrl::accessAndRespondForSecure(PacketPtr pkt, Tick static_latency,
                                                MemInterface* mem_intr)
{
    if (pkt->securePType == 0x1) {
        PacketPtr origin_pkt = pkt->preForSecure;

        DPRINTF(MemCtrl, "Responding to Address %#x.. \n", origin_pkt->getAddr());

        bool needsResponse = origin_pkt->needsResponse();
        // do the actual memory access which also turns the packet into a
        // response

        panic_if(!mem_intr->getAddrRange().contains(pkt->getAddr()),
                "Can't handle address range for packet %s\n", pkt->print());

        Addr phyAddr = origin_pkt->getAddr();
        PPN ppn = phyAddr >> 12;

        assert(pkt->metaDataMapForSecure.find(ppn) != pkt->metaDataMapForSecure.end());
        std::vector<uint8_t> metaData = pkt->metaDataMapForSecure[ppn];

        if (isAddressCovered(origin_pkt->getAddr(), origin_pkt->getSize(), 1)) {
            printf("the ppn is %d\n", ppn);
            printf("the metaData is \n");

            for (int i = 0; i < 8; i++) {
                printf("%02lx ", metaData[i]);
            }

            printf("\n");
        }

        Addr page_dram_addr = parseMetaDataForSecure(metaData, 0);
        Addr dram_addr = page_dram_addr | (phyAddr & ((0x1 << 12) - 1));

        if (isAddressCovered(origin_pkt->getAddr(), origin_pkt->getSize(), 1)) {
            printf("dram_addr: 0x%lx\n", dram_addr);
        }

        pkt->setAddr(dram_addr);

        mem_intr->accessForSecure(pkt, access_cnt);

        processPktListForSecure.remove(pkt);

        assert(pktInProcess > 0);
        pktInProcess--;
        if (pktInProcess == 0 && blockedForSecure) {
            /* pendingPktForSecure is initialized in recvTimingReqLogicForSecure */

            assert(pendingPktForSecure != nullptr);

            unsigned size = pendingPktForSecure->getSize();
            uint32_t burst_size = mem_intr->bytesPerBurst();

            unsigned offset = pendingPktForSecure->getAddr() & (burst_size - 1);
            unsigned int pkt_count = divCeil(offset + size, burst_size);

            if(!addToReadQueueForSecure(pendingPktForSecure, pkt_count, mem_intr)) {
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Request scheduled immediately\n");
                    schedule(nextReqEvent, curTick());
                }
            }
            pendingPktForSecure = nullptr;
        }

        // turn packet around to go back to requestor if response expected
        if (needsResponse) {
            // access already turned the packet into a response
            assert(origin_pkt->isResponse());
            // response_time consumes the static latency and is charged also
            // with headerDelay that takes into account the delay provided by
            // the xbar and also the payloadDelay that takes into account the
            // number of data beats.
            Tick response_time = curTick() + static_latency + pkt->headerDelay +
                                            pkt->payloadDelay;
            // Here we reset the timing of the packet before sending it out.
            origin_pkt->headerDelay = origin_pkt->payloadDelay = 0;

            // queue the packet in the response queue to be sent out after
            // the static latency has passed
            port.schedTimingResp(origin_pkt, response_time);
        } else {
            // @todo the packet is going to be deleted, and the MemPacket
            // is still having a pointer to it
            pendingDelete.reset(origin_pkt);
        }

        DPRINTF(MemCtrl, "Done\n");

        tryRecyclePkt(pkt, true);

    } else if (pkt->securePType == 0x2) {
        /* readMetaData */

        PacketPtr aux_pkt = pkt->preForSecure;

        std::vector<uint8_t> metaDataEntry(64, 0);
        mem_intr->atomicRead(metaDataEntry.data(), pkt->getAddr(), pkt->getSize());

        PacketPtr origin_pkt = aux_pkt->preForSecure;
        if (isAddressCovered(origin_pkt->getAddr(), origin_pkt->getSize(), 1)) {
            printf("finish read the metadata\n");
            printf("read metadata from memory is:\n ");
            for (int i = 0; i < 8; i++) {
                printf("%02x ", metaDataEntry[i]);
            }
            printf("\n");
        }

        if (metaDataEntry[0] < 0x80) {
            /* the metaData is not valid yet (this page is visited for the first time) */
            // printf("initial the metadata\n");
            initialMetaDataForSecure(metaDataEntry);
        }

        assert(!mcache.isFull());

        PPN ppn = aux_pkt->getAddr() >> 12;

        mcache.add(pkt->getAddr(), metaDataEntry);

        std::vector<uint8_t> metaData(8);
        memcpy(metaData.data(), metaDataEntry.data(), 8);

        aux_pkt->metaDataMapForSecure[ppn] = metaData;

        Addr dram_addr = parseMetaDataForSecure(metaData, 0);

        PacketPtr readForDecompress = new Packet(aux_pkt);
        readForDecompress->configAsSecureReadForDecompress(aux_pkt, dram_addr, 4096);
        readForDecompress->metaDataMapForSecure[ppn] = metaData;

        if (!addToReadQueueForSecure(readForDecompress, 64, mem_intr)) {
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
                schedule(nextReqEvent, curTick());
            }
        }

        tryRecyclePkt(pkt, true);

    } else if (pkt->securePType == 0x4) {
        /* readForCompress */
        /* get page information */
        assert(pkt->metaDataMapForSecure.size() == 1);
        auto kv = pkt->metaDataMapForSecure.begin();
        PPN ppn = kv->first;
        std::vector<uint8_t> metaData = kv->second;

        /* read page */
        assert(pkt->getSize() == 4096);
        std::vector<uint8_t> uPage(4096, 0);
        mem_intr->atomicRead(uPage.data(), pkt->getAddr(), pkt->getSize());

        /* attempt to compress */
        std::vector<uint8_t> cPage = compressPage(uPage.data(), 4096);
        uint64_t cSize = cPage.size();

        Addr new_chunk_addr = 0;
        assert(pktInProcess == 0);

        if (cSize <= 2048) {
            new_chunk_addr = allocateChunkForSecure(0);
        } else {
            new_chunk_addr = allocateChunkForSecure(1);
        }

        Addr old_chunk_addr = parseMetaDataForSecure(metaData, 0);
        Addr new_dram_addr = new_chunk_addr + 2048;

        /* update the metaData */

        if (isAddressCovered(ppn * 4096, 4096, 1)) {
            printf("update the metadata for page: ppn is %d\n", ppn);

            printf("the old metaData is \n");

            for (int i = 0; i < 8; i++) {
                printf("%02lx ", metaData[i]);
            }

            printf("\n");
        }

        if (cSize <= 2048) {
            metaData[0] = metaData[0] | (0x40);  // set the compressed bit to one
            for (int i = 2; i >= 1; i--) {
                metaData[i] = cSize & 0xFF;
                cSize = cSize >> 8;
            }
            assert(cSize == 0);
        }

        new_chunk_addr >>= 11;

        for (int i = 6; i >= 3; i--) {
            metaData[i] = new_chunk_addr & 0xFF;
            new_chunk_addr >>= 8;
        }

        if (isAddressCovered(ppn * 4096, 4096, 1)) {
            printf("the new metaData is \n");

            for (int i = 0; i < 8; i++) {
                printf("%02lx ", metaData[i]);
            }

            printf("\n");
        }

        recycleChunkForSecure(old_chunk_addr, 1);

        /* update metadata for pkts */
        pkt->metaDataMapForSecure[ppn] = metaData;
        updateMetaDataForInProcessPkt(false, ppn, metaData);

        /* write the (un)compressed page to pkt's content */
        if (cSize <= 2048) {
            memcpy(pkt->getPtr<uint8_t>(), cPage.data(), cPage.size());
        } else {
            memcpy(pkt->getPtr<uint8_t>(), uPage.data(), 4096);
        }

        if (isAddressCovered(ppn * 4096, 4096, 1)) {
            uint8_t* test = pkt->getPtr<uint8_t>();
            for (int i = 0; i < 8; i++) {

                printf("%02x ", test[i + 0xe40]);
            }
            printf("\n");
        }

        /* update the metadata in memory */
        Addr mAddr = startAddrForSecureMetaData + ppn * 8;
        mem_intr->atomicWrite(metaData, mAddr, 8);

        /* create a readForWrite pkt */
        PacketPtr readForWrite = new Packet(pkt);
        readForWrite->configAsSecureReadForWrite(pkt, new_dram_addr, 2048);

        // printf("secure: create readForWrite 0x%lx", readForWrite);

        if (!addToReadQueueForSecure(readForWrite, 32, mem_intr)) {
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Request scheduled immediately\n");
                schedule(nextReqEvent, curTick());
            }
        }
        pkt->ref_cnt--;

    } else if (pkt->securePType == 0x8) {
        /* writeForCompress */
        assert(pkt->getSize() == 4096);

        PacketPtr auxPkt = pkt->preForSecure;

        std::vector<uint8_t> cPage(4096);
        memcpy(cPage.data(), pkt->getPtr<uint8_t>(), 4096);

        /* write the "compressed" page to memory */
        mem_intr->atomicWrite(cPage, pkt->getAddr(), 4096);

        /* cache miss. The MC should issue a packet to read the metadata */
        PacketPtr readMetaDataForSecure = new Packet(auxPkt);

        PPN ppn = (auxPkt->getAddr()) >> 12;

        /* cal the dram address for metadata */
        Addr mAddr = startAddrForSecureMetaData + ppn * 8;

        readMetaDataForSecure->configAsSecureReadMetaData(auxPkt, mAddr, 64);

        // printf("secure: create readMetaData 0x%lx", readMetaDataForSecure);

        if (!addToReadQueueForSecure(readMetaDataForSecure, 1, dram)) {
            // If we are not already scheduled to get a request out of the
            // queue, do so now
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Request scheduled immediately\n");
                schedule(nextReqEvent, curTick());
            }
        }

        tryRecyclePkt(pkt, true);
        auxPkt->ref_cnt--;

    } else if (pkt->securePType == 0x10) {
        /* readForDecompress */

        PacketPtr auxPkt = pkt->preForSecure;
        PPN ppn = auxPkt->getAddr() >> 12;
        std::vector<uint8_t> metaData = auxPkt->metaDataMapForSecure[ppn];

        std::vector<uint8_t> origin_page(4096);

        bool pageIsCompressed = (((metaData[0] >> 6) & 0x1) == 1);

        if (pageIsCompressed) {
            /* the page is currently compressed */
            uint64_t cPageSize = parseMetaDataForSecure(metaData, 1);
            std::vector<uint8_t> cPage(cPageSize);

            mem_intr->atomicRead(cPage.data(), pkt->getAddr(), cPageSize);
            origin_page = decompressPage(cPage.data(), cPageSize);
        } else {
            /* the page is uncompressed */
            mem_intr->atomicRead(origin_page.data(), pkt->getAddr(), 4096);
        }

        /* update metadata */

        if (isAddressCovered(auxPkt->getAddr(), auxPkt->getSize(), 1)) {
            printf("update ppn is: %d\n", ppn);
            printf("old the metaData is \n");

            for (int i = 0; i < 8; i++) {
                printf("%02lx ", metaData[i]);
            }

            printf("\n");
        }

        Addr new_chunk_addr = allocateChunkForSecure(1);
        Addr old_chunk_addr = parseMetaDataForSecure(metaData, 0);

        if (pageIsCompressed) {
            recycleChunkForSecure(old_chunk_addr, 0);
        } else {
            recycleChunkForSecure(old_chunk_addr, 1);
        }

        /* the page is valid & uncompressed */
        metaData[0] = 0x80;

        /* set the new chunk addr for page */
        Addr dram_addr = new_chunk_addr;
        new_chunk_addr >>= 11;

        for (int i = 6; i >= 3; i--) {
            metaData[i] = new_chunk_addr & 0xFF;
            new_chunk_addr >>= 8;
        }


        if (isAddressCovered(auxPkt->getAddr(), auxPkt->getSize(), 1)) {
            printf("new the metaData is \n");

            for (int i = 0; i < 8; i++) {
                printf("%02lx ", metaData[i]);
            }

            printf("\n");

            printf("the dram address is 0x%lx\n", dram_addr);

            for (int i = 0; i < 8; i++) {
                printf("%02x ", origin_page[i + 0xe40]);
            }

            printf("\n");
        }

        /* update the new metadata for pkt */
        updateMetaDataForInProcessPkt(true, ppn, metaData);

        /* write the new metadata in mcache and memory */
        Addr mAddr = startAddrForSecureMetaData + ppn * 8;
        assert(mcache.isExist(mAddr));

        std::vector<uint8_t> metaDataEntry(64, 0);
        memcpy(metaDataEntry.data(), metaData.data(), 8);
        mcache.add(mAddr, metaDataEntry);

        mem_intr->atomicWrite(metaData, mAddr, 8);


        PacketPtr writeForDecompress = new Packet(auxPkt);


        writeForDecompress->configAsSecureWriteForDecompress(auxPkt, dram_addr, origin_page.data(), 4096);
        writeForDecompress->metaDataMapForSecure[ppn] = metaData;

        // printf("secure: create writeForDecompress 0x%lx", writeForDecompress);

        delayByDecompressForSecure[writeForDecompress] = curTick() + decompress_latency;

        auxPkt->ref_cnt--;
        tryRecyclePkt(pkt);

        if (!nextReqEvent.scheduled()) {
            DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
            schedule(nextReqEvent, curTick());
        }

    } else if (pkt->securePType == 0x20) {
        /* writeForDecompress */
        // printf("finish writeForDecompress\n");

        std::vector<uint8_t> dPage(4096);
        memcpy(dPage.data(), pkt->getPtr<uint8_t>(), 4096);
        mem_intr->atomicWrite(dPage, pkt->getAddr(), 4096);

        blockedForSecure = false;

        /* issue the real memory request */
        PacketPtr auxPkt = pkt->preForSecure;
        assert(auxPkt->securePType == 0x1);

        /* translate the dram address */
        Addr addr = pkt->getAddr();
        Addr dram_addr = addr | (auxPkt->getAddr() & ((1ULL << 12) - 1));
        bool sign = false;

        auxPkt->backupForSecure = auxPkt->getAddr();
        auxPkt->setAddr(dram_addr);

        uint32_t burst_size = dram->bytesPerBurst();
        unsigned pkt_size = auxPkt->getSize();

        unsigned pkt_offset = auxPkt->getAddr() & (burst_size - 1);
        unsigned int pkt_count = divCeil(pkt_offset + pkt_size, burst_size);

        if (auxPkt->isRead()) {
            sign = addToReadQueueForSecure(auxPkt, pkt_count, dram);
            stats.readReqs++;
            stats.bytesReadSys += pkt_size;
            if (!sign) {
                // If we are not already scheduled to get a request out of the
                // queue, do so now
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Request scheduled immediately\n");
                    schedule(nextReqEvent, curTick());
                }
            }
        } else {
            addToWriteQueueForSecure(auxPkt, pkt_count, dram);
            stats.writeReqs++;
            stats.bytesWrittenSys += pkt_size;
            if (!sign) {
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
                    schedule(nextReqEvent, curTick());
                }
            }
        }
        // printf("finish processed auxPkt\n");
        // printf("is blocked for secure? %d\n", blockedForSecure);
        // printf("is blocked queue for secure empty? %d\n", blockedQueueForSecure.empty());

        /* finally, we could process next reqs */
        while (!blockedQueueForSecure.empty() && !blockedForSecure) {
            // printf("the blocked queue is \n");
            // for (int z = 0; z < blockedQueueForSecure.size(); z++) {
            //     printf("the %dth pkt is 0x%lx\n", z, pkt);
            // }
            PacketPtr blocked_pkt = blockedQueueForSecure.front();
            unsigned blocked_size = blocked_pkt->getSize();

            unsigned blocked_offset = blocked_pkt->getAddr() & (burst_size - 1);
            unsigned int blocked_pkt_count = divCeil(blocked_offset + blocked_size, burst_size);

            blockedQueueForSecure.pop_front();
            blockedNumForSecure -= blocked_pkt_count;

            bool isAccepted = recvTimingReqLogicForSecure(blocked_pkt, true);
            assert(isAccepted);  // should be always accepted at this time
        }

        delete pkt;

        tryRecyclePkt(auxPkt, true);

    } else if (pkt->securePType == 0x40) {
        /* readForWrite */

        PacketPtr readForCompress = pkt->preForSecure;
        PacketPtr aux_pkt = readForCompress->preForSecure;

        std::unordered_map<PPN, std::vector<uint8_t>> metaDataMap = readForCompress->metaDataMapForSecure;
        assert(aux_pkt->securePType == 0x1);
        assert(metaDataMap.size() == 1);

        auto iter = metaDataMap.begin();
        std::vector<uint8_t> metaData = iter->second;

        std::vector<uint8_t> dataToWrite(4096);
        if (((metaData[0] >> 6) & 0x1) == 0) {
            /* the page is uncompressed */
            memcpy(dataToWrite.data(), readForCompress->getPtr<uint8_t>(), 4096);
        } else {
            uint64_t cPageSize = parseMetaDataForSecure(metaData, 1);
            memcpy(dataToWrite.data(), readForCompress->getPtr<uint8_t>(), cPageSize);

            mem_intr->atomicRead(dataToWrite.data() + 2048, pkt->getAddr(), 2048);
        }

        Addr dram_addr = parseMetaDataForSecure(metaData, 0);

        PacketPtr writeForCompress = new Packet(aux_pkt);
        writeForCompress->configAsSecureWriteForCompress(aux_pkt, dram_addr, dataToWrite.data(), 4096);

        // printf("secure: create writeForCompress 0x%lx", writeForCompress);

        addToWriteQueueForSecure(writeForCompress, 64, mem_intr);

        if (!nextReqEvent.scheduled()) {
            DPRINTF(MemCtrl, "Request scheduled immediately\n");
            schedule(nextReqEvent, curTick());
        }

        tryRecyclePkt(readForCompress);
        tryRecyclePkt(pkt);

    } else {
        panic("wrong pkt type");
    }
}

void
MemCtrl::pruneBurstTick()
{
    auto it = burstTicks.begin();
    while (it != burstTicks.end()) {
        auto current_it = it++;
        if (curTick() > *current_it) {
            DPRINTF(MemCtrl, "Removing burstTick for %d\n", *current_it);
            burstTicks.erase(current_it);
        }
    }
}

Tick
MemCtrl::getBurstWindow(Tick cmd_tick)
{
    // get tick aligned to burst window
    Tick burst_offset = cmd_tick % commandWindow;
    return (cmd_tick - burst_offset);
}

Tick
MemCtrl::verifySingleCmd(Tick cmd_tick, Tick max_cmds_per_burst, bool row_cmd)
{
    // start with assumption that there is no contention on command bus
    Tick cmd_at = cmd_tick;

    // get tick aligned to burst window
    Tick burst_tick = getBurstWindow(cmd_tick);

    // verify that we have command bandwidth to issue the command
    // if not, iterate over next window(s) until slot found
    while (burstTicks.count(burst_tick) >= max_cmds_per_burst) {
        DPRINTF(MemCtrl, "Contention found on command bus at %d\n",
                burst_tick);
        burst_tick += commandWindow;
        cmd_at = burst_tick;
    }

    // add command into burst window and return corresponding Tick
    burstTicks.insert(burst_tick);
    return cmd_at;
}

Tick
MemCtrl::verifyMultiCmd(Tick cmd_tick, Tick max_cmds_per_burst,
                         Tick max_multi_cmd_split)
{
    // start with assumption that there is no contention on command bus
    Tick cmd_at = cmd_tick;

    // get tick aligned to burst window
    Tick burst_tick = getBurstWindow(cmd_tick);

    // Command timing requirements are from 2nd command
    // Start with assumption that 2nd command will issue at cmd_at and
    // find prior slot for 1st command to issue
    // Given a maximum latency of max_multi_cmd_split between the commands,
    // find the burst at the maximum latency prior to cmd_at
    Tick burst_offset = 0;
    Tick first_cmd_offset = cmd_tick % commandWindow;
    while (max_multi_cmd_split > (first_cmd_offset + burst_offset)) {
        burst_offset += commandWindow;
    }
    // get the earliest burst aligned address for first command
    // ensure that the time does not go negative
    Tick first_cmd_tick = burst_tick - std::min(burst_offset, burst_tick);

    // Can required commands issue?
    bool first_can_issue = false;
    bool second_can_issue = false;
    // verify that we have command bandwidth to issue the command(s)
    while (!first_can_issue || !second_can_issue) {
        bool same_burst = (burst_tick == first_cmd_tick);
        auto first_cmd_count = burstTicks.count(first_cmd_tick);
        auto second_cmd_count = same_burst ? first_cmd_count + 1 :
                                   burstTicks.count(burst_tick);

        first_can_issue = first_cmd_count < max_cmds_per_burst;
        second_can_issue = second_cmd_count < max_cmds_per_burst;

        if (!second_can_issue) {
            DPRINTF(MemCtrl, "Contention (cmd2) found on command bus at %d\n",
                    burst_tick);
            burst_tick += commandWindow;
            cmd_at = burst_tick;
        }

        // Verify max_multi_cmd_split isn't violated when command 2 is shifted
        // If commands initially were issued in same burst, they are
        // now in consecutive bursts and can still issue B2B
        bool gap_violated = !same_burst &&
             ((burst_tick - first_cmd_tick) > max_multi_cmd_split);

        if (!first_can_issue || (!second_can_issue && gap_violated)) {
            DPRINTF(MemCtrl, "Contention (cmd1) found on command bus at %d\n",
                    first_cmd_tick);
            first_cmd_tick += commandWindow;
        }
    }

    // Add command to burstTicks
    burstTicks.insert(burst_tick);
    burstTicks.insert(first_cmd_tick);

    return cmd_at;
}

bool
MemCtrl::inReadBusState(bool next_state, const MemInterface* mem_intr) const
{
    // check the bus state
    if (next_state) {
        // use busStateNext to get the state that will be used
        // for the next burst
        return (mem_intr->busStateNext == MemCtrl::READ);
    } else {
        return (mem_intr->busState == MemCtrl::READ);
    }
}

bool
MemCtrl::inWriteBusState(bool next_state, const MemInterface* mem_intr) const
{
    // check the bus state
    if (next_state) {
        // use busStateNext to get the state that will be used
        // for the next burst
        return (mem_intr->busStateNext == MemCtrl::WRITE);
    } else {
        return (mem_intr->busState == MemCtrl::WRITE);
    }
}

Tick
MemCtrl::doBurstAccess(MemPacket* mem_pkt, MemInterface* mem_intr)
{
    // first clean up the burstTick set, removing old entries
    // before adding new entries for next burst
    pruneBurstTick();

    // When was command issued?
    Tick cmd_at;

    // Issue the next burst and update bus state to reflect
    // when previous command was issued
    std::vector<MemPacketQueue>& queue = selQueue(mem_pkt->isRead());
    Tick nextBurstAt = mem_intr->nextBurstAt;
    // if (operationMode == "DyLeCT") {
    //     if (mem_pkt->pkt->DyLStatus == 1) {
    //         nextBurstAt = std::max(nextBurstAt, curTick() + compress_latency);
    //     } else if (mem_pkt->pkt->DyLStatus == 2) {
    //         nextBurstAt = std::max(nextBurstAt, curTick() + decompress_latency);
    //     }
    // }
    std::tie(cmd_at, mem_intr->nextBurstAt) =
            mem_intr->doBurstAccess(mem_pkt, mem_intr->nextBurstAt, queue);

    DPRINTF(MemCtrl, "Access to %#x, ready at %lld next burst at %lld.\n",
            mem_pkt->addr, mem_pkt->readyTime, mem_intr->nextBurstAt);

    // Update the minimum timing between the requests, this is a
    // conservative estimate of when we have to schedule the next
    // request to not introduce any unecessary bubbles. In most cases
    // we will wake up sooner than we have to.
    mem_intr->nextReqTime = mem_intr->nextBurstAt - mem_intr->commandOffset();

    // Update the common bus stats
    if (mem_pkt->isRead()) {
        ++(mem_intr->readsThisTime);
        // Update latency stats
        stats.requestorReadTotalLat[mem_pkt->requestorId()] +=
            mem_pkt->readyTime - mem_pkt->entryTime;
        stats.requestorReadBytes[mem_pkt->requestorId()] += mem_pkt->size;
    } else {
        ++(mem_intr->writesThisTime);
        stats.requestorWriteBytes[mem_pkt->requestorId()] += mem_pkt->size;
        stats.requestorWriteTotalLat[mem_pkt->requestorId()] +=
            mem_pkt->readyTime - mem_pkt->entryTime;
    }

    return cmd_at;
}

bool
MemCtrl::memBusy(MemInterface* mem_intr) {

    // check ranks for refresh/wakeup - uses busStateNext, so done after
    // turnaround decisions
    // Default to busy status and update based on interface specifics
    // Default state of unused interface is 'true'
    bool mem_busy = true;
    bool all_writes_nvm = mem_intr->numWritesQueued == mem_intr->writeQueueSize;
    bool read_queue_empty = mem_intr->readQueueSize == 0;
    mem_busy = mem_intr->isBusy(read_queue_empty, all_writes_nvm);
    if (mem_busy) {
        // if all ranks are refreshing wait for them to finish
        // and stall this state machine without taking any further
        // action, and do not schedule a new nextReqEvent
        return true;
    } else {
        return false;
    }
}

bool
MemCtrl::nvmWriteBlock(MemInterface* mem_intr) {

    bool all_writes_nvm = mem_intr->numWritesQueued == totalWriteQueueSize;
    return (mem_intr->writeRespQueueFull() && all_writes_nvm);
}

void
MemCtrl::nonDetermReads(MemInterface* mem_intr) {

    for (auto queue = readQueue.rbegin();
            queue != readQueue.rend(); ++queue) {
            // select non-deterministic NVM read to issue
            // assume that we have the command bandwidth to issue this along
            // with additional RD/WR burst with needed bank operations
            if (mem_intr->readsWaitingToIssue()) {
                // select non-deterministic NVM read to issue
                mem_intr->chooseRead(*queue);
            }
    }
}

void
MemCtrl::processNextReqEvent(MemInterface* mem_intr,
                        MemPacketQueue& resp_queue,
                        EventFunctionWrapper& resp_event,
                        EventFunctionWrapper& next_req_event,
                        bool& retry_wr_req) {
    if (operationMode == "DyLeCT") {
        std::vector<PacketPtr> keys_to_erase;
        for (const auto &kv: delayForDecompress) {
            if (curTick() >= kv.second) {
                afterDecompForDyL(kv.first, mem_intr);
                keys_to_erase.emplace_back(kv.first);
            }
        }

        for (PacketPtr key: keys_to_erase) {
            delayForDecompress.erase(key);
        }

        if(mem_intr->readQueueSize == 0 && !delayForDecompress.empty()) {
            Tick targetTick = 0;
            for (const auto &kv: delayForDecompress) {
                if (targetTick == 0) {
                    targetTick = kv.second;
                } else {
                    targetTick = std::min(targetTick, kv.second);
                }
            }

            if (!next_req_event.scheduled()) {
                schedule(next_req_event, std::max(mem_intr->nextReqTime, targetTick));
            }

            if (retry_wr_req && mem_intr->writeQueueSize < writeBufferSize) {
                panic("retry enter?");
                retry_wr_req = false;
                port.sendRetryReq();
            }
            return;
        }
    } else if (operationMode == "secure") {
        std::vector<PacketPtr> keys_to_erase;
        for (const auto &kv: delayByDecompressForSecure) {
            if (curTick() >= kv.second) {
                afterDecompForSecure(kv.first, mem_intr);
                keys_to_erase.emplace_back(kv.first);
            }
        }

        for (PacketPtr key: keys_to_erase) {
            delayByDecompressForSecure.erase(key);
        }

        if(mem_intr->readQueueSize == 0 && !delayByDecompressForSecure.empty()) {
            assert(!next_req_event.scheduled());
            Tick targetTick = 0;
            for (const auto &kv: delayByDecompressForSecure) {
                if (targetTick == 0) {
                    targetTick = kv.second;
                } else {
                    targetTick = std::min(targetTick, kv.second);
                }
            }
            schedule(next_req_event, std::max(mem_intr->nextReqTime, targetTick));

            if (retry_wr_req && mem_intr->writeQueueSize < writeBufferSize) {
                panic("retry enter?");
                retry_wr_req = false;
                port.sendRetryReq();
            }
            return;
        }
    }

    // transition is handled by QoS algorithm if enabled
    if (turnPolicy) {
        // select bus state - only done if QoS algorithms are in use
        busStateNext = selectNextBusState();
    }

    // detect bus state change
    bool switched_cmd_type = (mem_intr->busState != mem_intr->busStateNext);
    // record stats
    recordTurnaroundStats(mem_intr->busState, mem_intr->busStateNext);

    DPRINTF(MemCtrl, "QoS Turnarounds selected state %s %s\n",
            (mem_intr->busState==MemCtrl::READ)?"READ":"WRITE",
            switched_cmd_type?"[turnaround triggered]":"");
    if (isAddressCovered(0, 8, 1)) {
        // printf("QoS Turnarounds selected state %s %s\n",
        //     (mem_intr->busState==MemCtrl::READ)?"READ":"WRITE",
        //     switched_cmd_type?"[turnaround triggered]":"");
        // printf("the current waitQueue size is %d\n", waitQueue.size());
        // // printf("the current pkt in blocked queue is %d\n", blockPktQueue.size());
        // printf("the read size queue is %d\n", mem_intr->readQueueSize);
        // printf("the write queue size is %d\n", mem_intr->writeQueueSize);
        // printf("the blockedNum %d\n", blockedNum);

        // fflush(stdout);
    }

    if (curTick() - recvLastPkt >= 5000000000) {
        printf("QoS Turnarounds selected state %s %s\n",
            (mem_intr->busState==MemCtrl::READ)?"READ":"WRITE",
            switched_cmd_type?"[turnaround triggered]":"");
        printf("the current waitQueue size is %d\n", waitQueue.size());
        // printf("the current pkt in blocked queue is %d\n", blockPktQueue.size());
        printf("the read size queue is %d\n", mem_intr->readQueueSize);
        printf("the write queue size is %d\n", mem_intr->writeQueueSize);
        printf("the blockedNum %d\n", blockedNum);
        fflush(stdout);
        panic("shouldn't be this long!!!!");
    }

    if (switched_cmd_type) {
        if (mem_intr->busState == MemCtrl::READ) {
            DPRINTF(MemCtrl,
            "Switching to writes after %d reads with %d reads "
            "waiting\n", mem_intr->readsThisTime, mem_intr->readQueueSize);
            stats.rdPerTurnAround.sample(mem_intr->readsThisTime);
            mem_intr->readsThisTime = 0;
        } else {
            DPRINTF(MemCtrl,
            "Switching to reads after %d writes with %d writes "
            "waiting\n", mem_intr->writesThisTime, mem_intr->writeQueueSize);
            stats.wrPerTurnAround.sample(mem_intr->writesThisTime);
            mem_intr->writesThisTime = 0;
        }
    }

    if (drainState() == DrainState::Draining && !totalWriteQueueSize &&
        !totalReadQueueSize && respQEmpty() && allIntfDrained()) {

        DPRINTF(Drain, "MemCtrl controller done draining\n");
        signalDrainDone();
    }

    // updates current state
    mem_intr->busState = mem_intr->busStateNext;

    nonDetermReads(mem_intr);

    if (memBusy(mem_intr)) {
        return;
    }

    // when we get here it is either a read or a write
    if (mem_intr->busState == READ) {

        // track if we should switch or not
        bool switch_to_writes = false;

        if (mem_intr->readQueueSize == 0) {
            // In the case there is no read request to go next,
            // trigger writes if we have passed the low threshold (or
            // if we are draining)

            uint64_t referSize = mem_intr->writeQueueSize;
            if (operationMode == "DyLeCT" or operationMode == "secure") {
                referSize = std::max(expectWriteQueueSize, referSize);
                // printf("[1] expectWriteQueueSize is %d, referSize is %d\n", expectReadQueueSize, referSize);
                // printf("[1] writeLowThresold = %ld\n", writeLowThreshold);
                // writeLowThreshold = 0;

            }
            if (isAddressCovered(0, 8, 1)) {
                // printf("write queue size is %d\n", mem_intr->writeQueueSize);
                // printf("referSize is %d\n", referSize);
                // printf("writeLowThreshold %d\n", writeLowThreshold);
                // printf("If draining %d\n", drainState() == DrainState::Draining);
                // fflush(stdout);
            }
            if (!(mem_intr->writeQueueSize == 0) &&
                (drainState() == DrainState::Draining ||
                 referSize > writeLowThreshold || avoidDeadLockForCompr(mem_intr) )) {
                DPRINTF(MemCtrl,
                        "Switching to writes due to read queue empty\n");
                switch_to_writes = true;
            } else {
                // check if we are drained
                // not done draining until in PWR_IDLE state
                // ensuring all banks are closed and
                // have exited low power states
                if (drainState() == DrainState::Draining &&
                    respQEmpty() && allIntfDrained()) {

                    DPRINTF(Drain, "MemCtrl controller done draining\n");
                    signalDrainDone();
                }

                // nothing to do, not even any point in scheduling an
                // event for the next request
                return;
            }
        } else {
            bool read_found = false;
            MemPacketQueue::iterator to_read;
            uint8_t prio = numPriorities();

            for (auto queue = readQueue.rbegin();
                 queue != readQueue.rend(); ++queue) {

                prio--;

                DPRINTF(QOS,
                        "Checking READ queue [%d] priority [%d elements]\n",
                        prio, queue->size());

                // Figure out which read request goes next
                // If we are changing command type, incorporate the minimum
                // bus turnaround delay which will be rank to rank delay
                to_read = chooseNext((*queue), switched_cmd_type ?
                                     minWriteToReadDataGap() : 0, mem_intr);

                if (to_read != queue->end()) {
                    // candidate read found
                    read_found = true;
                    break;
                }
            }

            // if no read to an available rank is found then return
            // at this point. There could be writes to the available ranks
            // which are above the required threshold. However, to
            // avoid adding more complexity to the code, return and wait
            // for a refresh event to kick things into action again.
            if (!read_found) {
                DPRINTF(MemCtrl, "No Reads Found - exiting\n");
                return;
            }

            auto mem_pkt = *to_read;

            Tick cmd_at = doBurstAccess(mem_pkt, mem_intr);

            DPRINTF(MemCtrl,
            "Command for %#x, issued at %lld.\n", mem_pkt->addr, cmd_at);

            // sanity check
            assert(pktSizeCheck(mem_pkt, mem_intr));
            assert(mem_pkt->readyTime >= curTick());

            // log the response
            logResponse(MemCtrl::READ, (*to_read)->requestorId(),
                        mem_pkt->qosValue(), mem_pkt->getAddr(), 1,
                        mem_pkt->readyTime - mem_pkt->entryTime);

            if (operationMode == "DyLeCT" || operationMode == "secure") {
                if (mem_pkt->memoryAccess) {
                    assert(expectReadQueueSize > 0);
                    expectReadQueueSize--;
                }
            }

            mem_intr->readQueueSize--;

            // Insert into response queue. It will be sent back to the
            // requestor at its readyTime
            if (resp_queue.empty()) {
                assert(!resp_event.scheduled());
                schedule(resp_event, mem_pkt->readyTime);
            } else {
                assert(resp_queue.back()->readyTime <= mem_pkt->readyTime);
                assert(resp_event.scheduled());
            }

            resp_queue.push_back(mem_pkt);

            // we have so many writes that we have to transition
            // don't transition if the writeRespQueue is full and
            // there are no other writes that can issue
            // Also ensure that we've issued a minimum defined number
            // of reads before switching, or have emptied the readQ
            if ((mem_intr->writeQueueSize > writeHighThreshold) &&
               (mem_intr->readsThisTime >= minReadsPerSwitch ||
               mem_intr->readQueueSize == 0)
               && !(nvmWriteBlock(mem_intr))) {
                switch_to_writes = true;
            }

            // remove the request from the queue
            // the iterator is no longer valid .
            readQueue[mem_pkt->qosValue()].erase(to_read);
        }

        // switching to writes, either because the read queue is empty
        // and the writes have passed the low threshold (or we are
        // draining), or because the writes hit the hight threshold
        if (switch_to_writes) {
            // transition to writing
            mem_intr->busStateNext = WRITE;
        }
    } else {

        bool write_found = false;
        MemPacketQueue::iterator to_write;
        uint8_t prio = numPriorities();

        for (auto queue = writeQueue.rbegin();
             queue != writeQueue.rend(); ++queue) {

            prio--;

            DPRINTF(QOS,
                    "Checking WRITE queue [%d] priority [%d elements]\n",
                    prio, queue->size());

            // If we are changing command type, incorporate the minimum
            // bus turnaround delay
            to_write = chooseNext((*queue),
                    switched_cmd_type ? minReadToWriteDataGap() : 0, mem_intr);

            if (to_write != queue->end()) {
                write_found = true;
                break;
            }
        }

        // if there are no writes to a rank that is available to service
        // requests (i.e. rank is in refresh idle state) are found then
        // return. There could be reads to the available ranks. However, to
        // avoid adding more complexity to the code, return at this point and
        // wait for a refresh event to kick things into action again.
        if (!write_found) {
            DPRINTF(MemCtrl, "No Writes Found - exiting\n");
            return;
        }

        auto mem_pkt = *to_write;

        // sanity check
        assert(pktSizeCheck(mem_pkt, mem_intr));

        Tick cmd_at = doBurstAccess(mem_pkt, mem_intr);
        DPRINTF(MemCtrl,
        "Command for %#x, issued at %lld.\n", mem_pkt->addr, cmd_at);

        isInWriteQueue.erase(burstAlign(mem_pkt->addr, mem_intr));

        // log the response
        logResponse(MemCtrl::WRITE, mem_pkt->requestorId(),
                    mem_pkt->qosValue(), mem_pkt->getAddr(), 1,
                    mem_pkt->readyTime - mem_pkt->entryTime);

        if (operationMode == "DyLeCT" or operationMode == "secure") {
            if (mem_pkt->memoryAccess) {
                assert(expectWriteQueueSize > 0);
                expectWriteQueueSize--;
            }
        }

        mem_intr->writeQueueSize--;
        assert(mem_intr->writeQueueSize >= expectWriteQueueSize);

        // remove the request from the queue - the iterator is no longer valid
        writeQueue[mem_pkt->qosValue()].erase(to_write);

        delete mem_pkt;

        // If we emptied the write queue, or got sufficiently below the
        // threshold (using the minWritesPerSwitch as the hysteresis) and
        // are not draining, or we have reads waiting and have done enough
        // writes, then switch to reads.
        // If we are interfacing to NVM and have filled the writeRespQueue,
        // with only NVM writes in Q, then switch to reads
        bool below_threshold =
            mem_intr->writeQueueSize + minWritesPerSwitch < writeLowThreshold;

        if (mem_intr->writeQueueSize == 0 ||
            (below_threshold && drainState() != DrainState::Draining) ||
            (mem_intr->readQueueSize && mem_intr->writesThisTime >= minWritesPerSwitch) ||
            (mem_intr->readQueueSize && (nvmWriteBlock(mem_intr)))) {

            // turn the bus back around for reads again
            mem_intr->busStateNext = MemCtrl::READ;

            // note that the we switch back to reads also in the idle
            // case, which eventually will check for any draining and
            // also pause any further scheduling if there is really
            // nothing to do
        }
    }
    // It is possible that a refresh to another rank kicks things back into
    // action before reaching this point.
    if (!next_req_event.scheduled())
        schedule(next_req_event, std::max(mem_intr->nextReqTime, curTick()));

    if (retry_wr_req && mem_intr->writeQueueSize < writeBufferSize) {
        retry_wr_req = false;
        port.sendRetryReq();
    }
}

bool
MemCtrl::packetReady(MemPacket* pkt, MemInterface* mem_intr)
{
    return mem_intr->burstReady(pkt);
}

Tick
MemCtrl::minReadToWriteDataGap()
{
    return dram->minReadToWriteDataGap();
}

Tick
MemCtrl::minWriteToReadDataGap()
{
    return dram->minWriteToReadDataGap();
}

Addr
MemCtrl::burstAlign(Addr addr, MemInterface* mem_intr) const
{
    return (addr & ~(Addr(mem_intr->bytesPerBurst() - 1)));
}

bool
MemCtrl::pktSizeCheck(MemPacket* mem_pkt, MemInterface* mem_intr) const
{
    return (mem_pkt->size <= mem_intr->bytesPerBurst());
}


std::vector<uint8_t> MemCtrl::compressPage(const uint8_t* inputData, size_t inputSize) {
    std::vector<uint8_t> compresseData(COMPRESSED_BUFFER_SIZE);

    uLongf compressedSize = compresseData.size();
    int ret = compress(compresseData.data(), &compressedSize, inputData, inputSize);

    if (ret != Z_OK) {
        std::cerr << "Compression failed with error code: " << ret << std::endl;
        return {};
    }

    compresseData.resize(compressedSize);
    return compresseData;
}

std::vector<uint8_t> MemCtrl::decompressPage(const uint8_t* compresseData, size_t compressedSize) {
    std::vector<uint8_t> decompresseData(PAGE_SIZE); // buffer size 4KB
    uLongf decompressedSize = PAGE_SIZE;
    int ret = uncompress(decompresseData.data(), &decompressedSize, compresseData, compressedSize);

    if (ret != Z_OK) {
        std::cerr << "Decompression failed with error code: " << ret << std::endl;
        return {};
    }

    decompresseData.resize(decompressedSize);
    return decompresseData;
}

bool MemCtrl::compressColdPage(const PacketPtr& origin_pkt, MemInterface* mem_intr) {
    // DPRINTF(MemCtrl, "[compress?] The current freelist size is %lld\n", freeList.size());
    // printf("The current freelist size is %lld\n", freeList.size());

    // if (freeList.size() + potentialRecycle < freeListThreshold) {
    // if (freeList.size() + potentialRecycle < 64888) {
    assert(recencyList.size() <= recencyListThreshold + 1);
    // printf("recencyList.size() == %lld\n", recencyList.size());
    fflush(stdout);
    if (recencyList.size() > recencyListThreshold) {
        // DPRINTF(MemCtrl, "Ohh, we actually have to compress a cold page\n");
        // DPRINTF(MemCtrl, "The freeList size is %lld, the threshold is %lld\n", freeList.size(), freeListThreshold);
        // DPRINTF(MemCtrl, "The potential recycle is %d\n", potentialRecycle);
        origin_pkt->usedForComp++;
        assert(recencyList.size() > 0);

        // DPRINTF(MemCtrl, "[Recency List] the size of recency list is %lld\n", recencyList.size());
        // printf("step 1\n");
        PPN pageId = recencyList.back();
        assert(pagesInCompress.find(pageId) == pagesInCompress.end());
        assert(pagesInDecompress.find(pageId) == pagesInDecompress.end());
        // assert(pageInProcess.find(pageId) == pageInProcess.end());
        pagesInCompress.insert(pageId);
        if(isAddressCovered((pageId) << 12, origin_pkt->getSize(), 1)) {
            printf("select victim is page %d\n", pageId);
        }

        Addr coldCteAddr = startAddrForCTE + pageId * 8;
        Addr coldCteAddrAligned = (coldCteAddr >> 6) << 6;
        uint8_t coldLoc = (coldCteAddr >> 3) & ((1 << 3) - 1);
        // DPRINTF(MemCtrl, "The victim cold page is %lld, the cte aligned address is 0x%llx, the loc is %d\n", pageId, coldCteAddrAligned, coldLoc);

        // assume we could immediate get the CTE for this cold page
        std::vector<uint8_t> curCL(64);
        // printf("step3, coldCteAddrAligned 0x%lx\n", coldCteAddrAligned);
        mem_intr->atomicRead(curCL.data(), coldCteAddrAligned, 64);

        uint64_t curCTE = 0;
        uint32_t burst_size = dram->bytesPerBurst();
        // printf("step 4: %d\n", static_cast<unsigned int>(coldLoc));
        for (unsigned int i = coldLoc * 8; i < (coldLoc + 1) * 8; i++) {
            curCTE = (curCTE << 8) | curCL[i];
        }
        if(isAddressCovered((pageId << 12), origin_pkt->getSize(), 1)) {
            printf("curCTE is 0x%lx\n", curCTE);
        }

        // printf("step 5\n");
        Addr pageAddr = ((curCTE >> 32) & ((1ULL << 30) - 1)) << 12;

        // printf("page address 0x%lx\n", pageAddr);
        // DPRINTF(MemCtrl, "The address of the origin_pkt is 0x%llx\n", (uint64_t)origin_pkt);
        PacketPtr readUncompress = new Packet(origin_pkt);
        // printf("create new pkt for readUncompress 0x%lx\n", readUncompress);
        // DPRINTF(MemCtrl, "create a new packet for read Uncompress, the address is 0x%llx\n", (uint64_t)readUncompress);


        readUncompress->configAsReadUncompress(pageAddr, origin_pkt, pageId);
        unsigned ruc_offset = readUncompress->getAddr() & (burst_size - 1);
        unsigned int ruc_pkt_count = divCeil(ruc_offset + 4096, burst_size);
        // printf("the address of readUncompress is 0x%lx\n", reinterpret_cast<unsigned long>(readUncompress));
        // printf("the address of origin pkt is 0x%lx\n", reinterpret_cast<unsigned long>(origin_pkt));
        // printf("step 6\n");
        assert(ruc_pkt_count == 64);
        // DPRINTF(MemCtrl, "Before incre the potential recycle\n");
        potentialRecycle++;
        // DPRINTF(MemCtrl, "After incre the potential recycle, the number is %d\n", potentialRecycle);
        recencyList.pop_back();
        recencyMap.erase(pageId);

        bool sign = addToReadQueueForDyL(readUncompress, ruc_pkt_count, mem_intr);

        if (!sign) {
            // If we are not already scheduled to get a request out of the
            // queue, do so now
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Request scheduled immediately\n");
                schedule(nextReqEvent, curTick());
            }
        }

        // printf("step 7\n");
        return false;
    } else {
        return true;
    }
}

MemCtrl::CtrlStats::CtrlStats(MemCtrl &_ctrl)
    : statistics::Group(&_ctrl),
    ctrl(_ctrl),

    ADD_STAT(readReqs, statistics::units::Count::get(),
             "Number of read requests accepted"),
    ADD_STAT(writeReqs, statistics::units::Count::get(),
             "Number of write requests accepted"),

    ADD_STAT(readBursts, statistics::units::Count::get(),
             "Number of controller read bursts, including those serviced by "
             "the write queue"),
    ADD_STAT(writeBursts, statistics::units::Count::get(),
             "Number of controller write bursts, including those merged in "
             "the write queue"),
    ADD_STAT(servicedByWrQ, statistics::units::Count::get(),
             "Number of controller read bursts serviced by the write queue"),
    ADD_STAT(mergedWrBursts, statistics::units::Count::get(),
             "Number of controller write bursts merged with an existing one"),

    ADD_STAT(neitherReadNorWriteReqs, statistics::units::Count::get(),
             "Number of requests that are neither read nor write"),

    ADD_STAT(avgRdQLen, statistics::units::Rate<
                statistics::units::Count, statistics::units::Tick>::get(),
             "Average read queue length when enqueuing"),
    ADD_STAT(avgWrQLen, statistics::units::Rate<
                statistics::units::Count, statistics::units::Tick>::get(),
             "Average write queue length when enqueuing"),

    ADD_STAT(numRdRetry, statistics::units::Count::get(),
             "Number of times read queue was full causing retry"),
    ADD_STAT(numWrRetry, statistics::units::Count::get(),
             "Number of times write queue was full causing retry"),

    ADD_STAT(readPktSize, statistics::units::Count::get(),
             "Read request sizes (log2)"),
    ADD_STAT(writePktSize, statistics::units::Count::get(),
             "Write request sizes (log2)"),

    ADD_STAT(rdQLenPdf, statistics::units::Count::get(),
             "What read queue length does an incoming req see"),
    ADD_STAT(wrQLenPdf, statistics::units::Count::get(),
             "What write queue length does an incoming req see"),

    ADD_STAT(rdPerTurnAround, statistics::units::Count::get(),
             "Reads before turning the bus around for writes"),
    ADD_STAT(wrPerTurnAround, statistics::units::Count::get(),
             "Writes before turning the bus around for reads"),

    ADD_STAT(bytesReadWrQ, statistics::units::Byte::get(),
             "Total number of bytes read from write queue"),
    ADD_STAT(bytesReadSys, statistics::units::Byte::get(),
             "Total read bytes from the system interface side"),
    ADD_STAT(bytesWrittenSys, statistics::units::Byte::get(),
             "Total written bytes from the system interface side"),

    ADD_STAT(avgRdBWSys, statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Average system read bandwidth in Byte/s"),
    ADD_STAT(avgWrBWSys, statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Average system write bandwidth in Byte/s"),

    ADD_STAT(totGap, statistics::units::Tick::get(),
             "Total gap between requests"),
    ADD_STAT(avgGap, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
             "Average gap between requests"),

    ADD_STAT(requestorReadBytes, statistics::units::Byte::get(),
             "Per-requestor bytes read from memory"),
    ADD_STAT(requestorWriteBytes, statistics::units::Byte::get(),
             "Per-requestor bytes write to memory"),
    ADD_STAT(requestorReadRate, statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Per-requestor bytes read from memory rate"),
    ADD_STAT(requestorWriteRate, statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Per-requestor bytes write to memory rate"),
    ADD_STAT(requestorReadAccesses, statistics::units::Count::get(),
             "Per-requestor read serviced memory accesses"),
    ADD_STAT(requestorWriteAccesses, statistics::units::Count::get(),
             "Per-requestor write serviced memory accesses"),
    ADD_STAT(requestorReadTotalLat, statistics::units::Tick::get(),
             "Per-requestor read total memory access latency"),
    ADD_STAT(requestorWriteTotalLat, statistics::units::Tick::get(),
             "Per-requestor write total memory access latency"),
    ADD_STAT(requestorReadAvgLat, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
             "Per-requestor read average memory access latency"),
    ADD_STAT(requestorWriteAvgLat, statistics::units::Rate<
                statistics::units::Tick, statistics::units::Count>::get(),
             "Per-requestor write average memory access latency"),

    ADD_STAT(usedMemoryByte, statistics::units::Count::get(),
             "record the usage of physical memory (in Byte)")
{
}

void
MemCtrl::CtrlStats::regStats()
{
    using namespace statistics;

    assert(ctrl.system());
    const auto max_requestors = ctrl.system()->maxRequestors();

    avgRdQLen.precision(2);
    avgWrQLen.precision(2);

    readPktSize.init(ceilLog2(ctrl.system()->cacheLineSize()) + 1);
    writePktSize.init(ceilLog2(ctrl.system()->cacheLineSize()) + 1);

    rdQLenPdf.init(ctrl.readBufferSize);
    wrQLenPdf.init(ctrl.writeBufferSize);

    rdPerTurnAround
        .init(ctrl.readBufferSize)
        .flags(nozero);
    wrPerTurnAround
        .init(ctrl.writeBufferSize)
        .flags(nozero);

    avgRdBWSys.precision(8);
    avgWrBWSys.precision(8);
    avgGap.precision(2);

    // per-requestor bytes read and written to memory
    requestorReadBytes
        .init(max_requestors)
        .flags(nozero | nonan);

    requestorWriteBytes
        .init(max_requestors)
        .flags(nozero | nonan);

    // per-requestor bytes read and written to memory rate
    requestorReadRate
        .flags(nozero | nonan)
        .precision(12);

    requestorReadAccesses
        .init(max_requestors)
        .flags(nozero);

    requestorWriteAccesses
        .init(max_requestors)
        .flags(nozero);

    requestorReadTotalLat
        .init(max_requestors)
        .flags(nozero | nonan);

    requestorReadAvgLat
        .flags(nonan)
        .precision(2);

    requestorWriteRate
        .flags(nozero | nonan)
        .precision(12);

    requestorWriteTotalLat
        .init(max_requestors)
        .flags(nozero | nonan);

    requestorWriteAvgLat
        .flags(nonan)
        .precision(2);

    for (int i = 0; i < max_requestors; i++) {
        const std::string requestor = ctrl.system()->getRequestorName(i);
        requestorReadBytes.subname(i, requestor);
        requestorReadRate.subname(i, requestor);
        requestorWriteBytes.subname(i, requestor);
        requestorWriteRate.subname(i, requestor);
        requestorReadAccesses.subname(i, requestor);
        requestorWriteAccesses.subname(i, requestor);
        requestorReadTotalLat.subname(i, requestor);
        requestorReadAvgLat.subname(i, requestor);
        requestorWriteTotalLat.subname(i, requestor);
        requestorWriteAvgLat.subname(i, requestor);
    }

    // Formula stats
    avgRdBWSys = (bytesReadSys) / simSeconds;
    avgWrBWSys = (bytesWrittenSys) / simSeconds;

    avgGap = totGap / (readReqs + writeReqs);

    requestorReadRate = requestorReadBytes / simSeconds;
    requestorWriteRate = requestorWriteBytes / simSeconds;
    requestorReadAvgLat = requestorReadTotalLat / requestorReadAccesses;
    requestorWriteAvgLat = requestorWriteTotalLat / requestorWriteAccesses;

    usedMemoryByte
        .init(10)  // take at most 10 records
        .desc("record the memory consumption")
        .precision(0);

    for (int i = 0; i < 10; i++) {
        usedMemoryByte.subname(i, csprintf("Interval %d", i));
    }
}

void
MemCtrl::recvFunctional(PacketPtr pkt)
{
    // access_cnt += 1;
    recordMemConsumption();
    bool found = false;
    if (operationMode == "normal") {
        found = recvFunctionalLogic(pkt, dram);
    } else if (operationMode == "compresso") {
        found = recvFunctionalLogicForCompr(pkt, dram);
    } else if (operationMode == "DyLeCT") {
        found = recvFunctionalLogicForDyL(pkt, dram);
    } else if (operationMode == "new") {
        found = recvFunctionalLogicForNew(pkt, dram);
    } else if (operationMode == "secure") {
        found = recvFunctionalLogicForSecure(pkt, dram);
    } else {
        panic("unknown mode for memory controller");
    }

    panic_if(!found, "Can't handle address range for packet %s\n",
             pkt->print());
}

void
MemCtrl::recvMemBackdoorReq(const MemBackdoorReq &req,
        MemBackdoorPtr &backdoor)
{
    panic_if(!dram->getAddrRange().contains(req.range().start()),
            "Can't handle address range for backdoor %s.",
            req.range().to_string());

    dram->getBackdoor(backdoor);
}

bool
MemCtrl::recvFunctionalLogic(PacketPtr pkt, MemInterface* mem_intr)
{
    DPRINTF(MemCtrl, "recv Functional: %s 0x%x\n",
        pkt->cmdString(), pkt->getAddr());
    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)){
        printf("recv Functional: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
    }

    PPN page_num = (pkt->getAddr() >> 12);
    stat_page_used.insert(page_num);

    // printf("recv Functional: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
    if (mem_intr->getAddrRange().contains(pkt->getAddr())) {
        // rely on the abstract memory
        mem_intr->functionalAccess(pkt, access_cnt);
        return true;
    } else {
        return false;
    }
}

bool
MemCtrl::recvFunctionalLogicForCompr(PacketPtr pkt, MemInterface* mem_intr) {
    DPRINTF(MemCtrl, "recvFunction: %s 0x%x\n",
        pkt->cmdString(), pkt->getAddr());
    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)) {
        printf("recv Functional: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
    }

    if (mem_intr->getAddrRange().contains(pkt->getAddr())) {

        /* step 0: create an auxiliary packet for processing the pkt */
        PacketPtr auxPkt = new Packet(pkt);
        // copy the content
        auxPkt->allocateForMC();
        memcpy(auxPkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

        unsigned size = pkt->getSize();
        uint32_t burst_size = mem_intr->bytesPerBurst();

        unsigned offset = pkt->getAddr() & (burst_size - 1);
        unsigned int pkt_count = divCeil(offset + size, burst_size);

        /* Step 1: atomic read metadata from memory or mcache */
        Addr base_addr = pkt->getAddr();
        Addr addr = base_addr;

        for (unsigned int i = 0; i < pkt_count; i++) {
            PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

            if (auxPkt->comprMetaDataMap.find(ppn) == auxPkt->comprMetaDataMap.end()) {
                /* step 1.1: calculate the MPA for metadata */
                Addr memory_addr = ppn * 64;
                std::vector<uint8_t> metaData(64, 0);
                if (mcache.isExist(memory_addr)) {
                    metaData = mcache.find(memory_addr);
                } else {
                    mem_intr->atomicRead(metaData.data(), memory_addr, 64);
                }

                /* step 1.2 determine if it is an unallocated page */
                if (!isValidMetaData(metaData)) {
                    /* make room in the pagebuffer for this new page */
                    if (!hasBuffered) {
                        initialPageBuffer(ppn);
                    }
                    /*
                        if the pageBuffer already have data of another page,
                            write back to memory, update the metadata
                    */
                    if (pageNum != ppn) {
                        DPRINTF(MemCtrl, "need to flush the page %d\n", pageNum);

                        // DPRINTF(MemCtrl, "the origin metadata is: \n");
                        // for (int k = 0; k < 64; k++) {
                        //     printf("%02x",static_cast<unsigned>(mPageBuffer[k]));
                        // }
                        // printf("\n");

                        std::vector<uint8_t> compressedPage;
                        std::vector<uint8_t> mPageBuffer(64, 0);
                        mem_intr->atomicRead(mPageBuffer.data(), pageNum * 64, 64);

                        for (uint8_t u = 0; u < 64; u++) {
                            uint8_t type = getType(mPageBuffer, u);
                            uint8_t dataLen = sizeMap[type];
                            std::vector<uint8_t> compressedCL(dataLen, 0);
                            mem_intr->atomicRead(compressedCL.data(), pageBufferAddr + 64 * u, dataLen);
                            for (uint8_t v = 0; v < dataLen; v++) {
                                // compressedPage.emplace_back(pageBuffer[64 * u + v]);
                                compressedPage.emplace_back(compressedCL[v]);
                            }
                        }

                        uint64_t cur = 0;
                        while (cur < compressedPage.size()) {
                            assert(freeList.size() > 0);
                            Addr chunkAddr = freeList.front();
                            DPRINTF(MemCtrl, "Line %d, freeList pop\n", __LINE__);
                            freeList.pop_front();

                            int chunkIdx = cur / 512;
                            uint64_t MPFN = (chunkAddr >> 9);
                            for (int u = 3; u >= 0; u--) {   // 4B per chunk
                                uint8_t val = MPFN & 0xFF;
                                MPFN >>= 8;
                                mPageBuffer[2 + chunkIdx * 4 + u] = val;
                            }
                            if (cur + 512 < compressedPage.size()) {
                                mem_intr->atomicWrite(compressedPage, chunkAddr, 512, cur);
                                cur += 512;

                            } else {
                                mem_intr->atomicWrite(compressedPage, chunkAddr, compressedPage.size() - cur, cur);
                                break;
                            }
                        }
                        stat_used_bytes += compressedPage.size();

                        // store the size of compressedPage into the control block (using 12 bit)
                        uint64_t compressedSize = compressedPage.size();

                        mPageBuffer[1] = compressedSize & (0xFF);
                        mPageBuffer[0] = mPageBuffer[0] | ((compressedSize >> 8) & 0xF);

                        // DPRINTF(MemCtrl, "the final metadata is: \n");
                        // for (int k = 0; k < 64; k++) {
                        //     printf("%02x",static_cast<unsigned>(mPageBuffer[k]));

                        // }
                        // printf("\n");

                        /* write the mPageBuffer to mcache and memory */
                        Addr mPageBufferAddr = pageNum * 64;
                        // printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, pageNum, mPageBufferAddr);
                        mcache.add(mPageBufferAddr, mPageBuffer);
                        mem_intr->atomicWrite(mPageBuffer, mPageBufferAddr, 64, 0);
                        if (auxPkt->comprMetaDataMap.find(pageNum) != auxPkt->comprMetaDataMap.end()) {
                            auxPkt->comprMetaDataMap[pageNum] = mPageBuffer;
                        }

                        initialPageBuffer(ppn);
                    }

                    /* now the page buffer is ready to serve the incoming write request */
                }
                /* step 1.2: update the pkt's metaDataMap */
                if (ppn == pageNum) {
                    mem_intr->atomicRead(metaData.data(), ppn * 64, 64);
                    auxPkt->comprMetaDataMap[ppn] = metaData;
                } else {
                    auxPkt->comprMetaDataMap[ppn] = metaData;
                }
            }
            // Starting address of next memory pkt (aligned to burst boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }

        /* step 2: process the pkt based on isWrite or isRead*/
        if (auxPkt->isWrite()) {
            /* step 2.1: if the pkt is write */
            // DPRINTF(MemCtrl, "(F) Line %d: cur req is write \n", __LINE__);

            Addr addrAligned = (base_addr >> 6) << 6;
            uint64_t new_size = ((((base_addr + size) + (burst_size - 1)) >> 6) << 6) - addrAligned;

            assert(burst_size == 64);
            assert(new_size == (addr - addrAligned));


            if (auxPkt->cmd == MemCmd::SwapReq) {
                panic("should not enter this in Functional mode");
                // DPRINTF(MemCtrl, "Line %d: req's cmd is swapReq \n", __LINE__);
                // /* step 2.1.1 assert(pkt.size <= 64 && pkt is not cross the boundary)*/
                // assert(size <= 64 && (burst_size - (base_addr | (burst_size - 1)) <= size));

                // PPN ppn = (base_addr >> 12 & ((1ULL << 52) - 1));
                // std::vector<uint8_t> cacheLine(64, 0);

                // assert(auxPkt->metaDataSet.find(ppn) != auxPkt->metaDataSet.end());
                // std::vector<uint8_t> metaData = auxPkt->metaDataSet[ppn];
                // uint8_t cacheLineIdx = (base_addr >> 6) & 0x3F;
                // uint8_t type = getType(metaData, cacheLineIdx);
                // bool inInflate = false;
                // Addr real_addr = 0;

                // if (!isValidMetaData(metaData)) {
                //     assert(ppn == pageNum);
                //     type = getType(mPageBuffer, cacheLineIdx);
                //     for (unsigned int u = 0; u < sizeMap[type]; u++) {
                //         cacheLine[u] = pageBuffer[cacheLineIdx * 64 + u];
                //     }
                //     restoreData(cacheLine, type);
                // } else {
                //     std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);
                //     inInflate = cLStatus.first;
                //     real_addr = cLStatus.second;

                //     if (type != 0) {
                //         /* step 2.1.2 read the cacheLine from memory */
                //         mem_intr->atomicRead(cacheLine.data(), real_addr, sizeMap[type]);
                //     }

                //     /* step 2.1.3 decompress */
                //     restoreData(cacheLine, type);
                // }

                // /* step 2.1.4 the same as before, host_addr = cacheLine.data() + ofs */
                // assert(cacheLine.size() == burst_size);
                // uint8_t* uPtr = cacheLine.data() + offset;
                // if (pkt->isAtomicOp()) {
                //     if (mem_intr->hasValidHostMem()) {
                //         pkt->setData(uPtr);
                //         (*(pkt->getAtomicOp()))(uPtr);
                //     }
                // } else {
                //     std::vector<uint8_t> overwrite_val(pkt->getSize());
                //     uint64_t condition_val64;
                //     uint32_t condition_val32;

                //     panic_if(!mem_intr->hasValidHostMem(), "Swap only works if there is real memory " \
                //             "(i.e. null=False)");

                //     bool overwrite_mem = true;
                //     // keep a copy of our possible write value, and copy what is at the
                //     // memory address into the packet
                //     pkt->writeData(&overwrite_val[0]);
                //     pkt->setData(uPtr);

                //     if (pkt->req->isCondSwap()) {
                //         if (pkt->getSize() == sizeof(uint64_t)) {
                //             assert(uPtr == cacheLine.data());
                //             condition_val64 = pkt->req->getExtraData();
                //             overwrite_mem = !std::memcmp(&condition_val64, uPtr,
                //                                         sizeof(uint64_t));
                //         } else if (pkt->getSize() == sizeof(uint32_t)) {
                //             condition_val32 = (uint32_t)pkt->req->getExtraData();
                //             overwrite_mem = !std::memcmp(&condition_val32, uPtr,
                //                                         sizeof(uint32_t));
                //         } else
                //             panic("Invalid size for conditional read/write\n");
                //     }

                //     if (overwrite_mem) {
                //         std::memcpy(uPtr, &overwrite_val[0], pkt->getSize());
                //     }
                // }

                // /*step 2.1.5 recompress the cacheline */
                // std::vector<uint8_t> compressed = compress(cacheLine);

                // if (!isValidMetaData(metaData)) {
                //     /*write back to pageBuffer and update the mPageBuffer if necessary */
                //     bool isCompressed = false;
                //     if (isAllZero(cacheLine)) {
                //         /* set the mPageBuffer entry to be 0 */
                //         setType(mPageBuffer, cacheLineIdx, 0);
                //     } else {
                //         if (compressed.size() <= 8) {
                //             /* set the mPageBuffer entry to be 0b1*/
                //             setType(mPageBuffer, cacheLineIdx, 0b01);
                //             isCompressed = true;
                //         } else if (compressed.size() <= 32) {
                //             setType(mPageBuffer, cacheLineIdx, 0b10);
                //             isCompressed = true;
                //         } else {
                //             /* set to be 0b11 */
                //             setType(mPageBuffer, cacheLineIdx, 0b11);
                //         }
                //     }
                //     if (isCompressed) {
                //         for (int u = 0; u < compressed.size(); u++) {
                //             pageBuffer[cacheLineIdx * 64 + u] = compressed[u];
                //         }

                //     } else {
                //         for (int u = 0; u < 64; u++) {
                //             pageBuffer[cacheLineIdx * 64 + u] = cacheLine[u];
                //         }
                //     }
                // } else {
                //     /* step 2.1.6 deal with potential overflow/underflow */
                //     updateMetaData(compressed, metaData, cacheLineIdx, inInflate, mem_intr);
                //     auxPkt->metaDataSet[ppn] = metaData;
                //     Addr metadata_addr = ppn * 64;
                //     mcache.add(metadata_addr, metaData);
                //     mem_intr->atomicWrite(metaData, metadata_addr, 64, 0);
                // }

                // assert(new_size == 64);
                // auxPkt->setAddr(addrAligned);
                // auxPkt->setSizeForMC(new_size);
                // auxPkt->allocateForMC();
                // auxPkt->setDataForMC(cacheLine.data(), 0, new_size);
            } else {
                addr = base_addr;
                std::vector<uint8_t> newData(new_size, 0);

                /* process the pkt one by one */
                for (unsigned int i = 0; i < pkt_count; i++) {
                    PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

                    // Addr mAddr = ppn * 64;
                    uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

                    assert(auxPkt->comprMetaDataMap.find(ppn) != auxPkt->comprMetaDataMap.end());
                    std::vector<uint8_t> metaData = auxPkt->comprMetaDataMap[ppn];

                    std::vector<uint8_t> cacheLine(64, 0);
                    // printf("the pageNum is %d, the metadata is :\n", ppn);
                    // for (int k = 0; k < 64; k++) {
                    //     printf("%02x",static_cast<unsigned>(metaData[k]));

                    // }
                    // printf("\n");

                    if (pageNum == ppn) {
                        // DPRINTF(MemCtrl, "(F) Line %d: need to modify the pageBuffer \n", __LINE__);

                        std::vector<uint8_t> mPageBuffer(64, 0);
                        mem_intr->atomicRead(mPageBuffer.data(), ppn * 64, 64);

                        uint8_t type = getType(mPageBuffer, cacheLineIdx);

                        mem_intr->atomicRead(cacheLine.data(), pageBufferAddr + 64 * cacheLineIdx, sizeMap[type]);

                        // for (unsigned int j = 0; j < sizeMap[type]; j++) {
                        //     cacheLine[j] = pageBuffer[64 * cacheLineIdx + j];
                        // }

                        restoreData(cacheLine, type);
                        assert(cacheLine.size() == 64);

                        /* write the data */
                        uint64_t ofs = addr - base_addr;
                        uint8_t loc = addr & 0x3F;
                        size_t writeSize = std::min(64UL - loc, size - ofs);

                        auxPkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);
                        // DPRINTF(MemCtrl, "Line %d: finish write the data \n", __LINE__);

                        std::vector<uint8_t> compressed = compressForCompr(cacheLine);

                        // DPRINTF(MemCtrl, "Line %d: finish recompress the data \n", __LINE__);

                        /*write back to pageBuffer and update the mPageBuffer if necessary */
                        if (isAllZero(cacheLine)) {
                            /* set the mPageBuffer entry to be 0 */
                            setType(mPageBuffer, cacheLineIdx, 0);
                        } else {
                            if (compressed.size() <= 8) {
                                /* set the mPageBuffer entry to be 0b1*/
                                setType(mPageBuffer, cacheLineIdx, 0b01);
                            } else if (compressed.size() <= 32) {
                                setType(mPageBuffer, cacheLineIdx, 0b10);
                            } else {
                                /* set to be 0b11 */
                                setType(mPageBuffer, cacheLineIdx, 0b11);
                            }
                        }
                        auxPkt->comprMetaDataMap[ppn] = mPageBuffer;
                        mem_intr->atomicWrite(mPageBuffer, ppn * 64, 64);
                    } else {
                        uint8_t type = getType(metaData, cacheLineIdx);
                        DPRINTF(MemCtrl, "(F) Line %d: the type of cacheline is %d\n", __LINE__, static_cast<unsigned int>(type));
                        std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);
                        bool inInflate = cLStatus.first;
                        Addr real_addr = cLStatus.second;

                        if (inInflate) {
                            mem_intr->atomicRead(cacheLine.data(), real_addr, 64);
                            type = 0b11;
                        } else {
                            if (type != 0) {
                                mem_intr->atomicRead(cacheLine.data(), real_addr, sizeMap[type]);
                            }
                        }

                        DPRINTF(MemCtrl, "(F) Line %d: finish read the cacheline from memory \n", __LINE__);

                        // for (int u = 0; u < 64; u++) {
                        //     if (u % 8 == 0) {
                        //         printf("\n");
                        //     }
                        //     printf("0x%02x, ", static_cast<uint8_t>(cacheLine[u]));
                        // }
                        // printf("\n");

                        restoreData(cacheLine, type);

                        DPRINTF(MemCtrl, "(F) Line %d: finish restore the cacheline \n", __LINE__);

                        // printf("the restore the data is :\n");
                        // for (int as = 0; as < cacheLine.size(); as++) {
                        //     if (as % 8 == 0) {
                        //         printf("\n");
                        //     }
                        //     printf("%02x, ", static_cast<unsigned int>(cacheLine[as]));
                        // }
                        // printf("\n");

                        /* write the data */
                        uint64_t ofs = addr - base_addr;
                        uint8_t loc = addr & 0x3F;
                        size_t writeSize = std::min(64UL - loc, size - ofs);
                        // DPRINTF(MemCtrl, "(F) Line %d: start to write dat, the ofs is %lld, the loc is %d, the writeSize is %ld\n", __LINE__, ofs, static_cast<unsigned int>(loc), writeSize);
                        auxPkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

                        // printf("after write the data is :\n");
                        // for (int as = 0; as < cacheLine.size(); as++) {
                        //     if (as % 8 == 0) {
                        //         printf("\n");
                        //     }
                        //     printf("%02x, ", static_cast<unsigned int>(cacheLine[as]));
                        // }
                        // printf("\n");

                        std::vector<uint8_t> compressed = compressForCompr(cacheLine);
                        if (compressed.size() > 32) {
                            assert(compressed.size() == 64);
                        }

                        // DPRINTF(MemCtrl, "Line %d: finish compress, the size of compressed is %d\n", __LINE__, compressed.size());
                        // for (int as = 0; as < compressed.size(); as++) {
                        //     if (as % 8 == 0) {
                        //         printf("\n");
                        //     }
                        //     printf("%02x, ", static_cast<unsigned int>(compressed[as]));
                        // }
                        // printf("\n");

                        /* deal with potential overflow/underflow */
                        bool success = updateMetaData(compressed, metaData, cacheLineIdx, inInflate, mem_intr);

                        if (!success) {
                            recompressAtomic(cacheLine, ppn, cacheLineIdx, metaData, mem_intr);
                            /* update the metadataSet in auxPkt */
                            assert(mcache.isExist(ppn * 64));
                            auxPkt->comprMetaDataMap[ppn] = mcache.find(ppn * 64);
                        } else {

                            auxPkt->comprMetaDataMap[ppn] = metaData;
                            Addr metadata_addr = ppn * 64;
                            mcache.add(metadata_addr, metaData);
                            mem_intr->atomicWrite(metaData, metadata_addr, 64, 0);
                        }
                    }
                    for (unsigned int j = 0; j < 64; j++) {
                        newData[i * 64 + j] = cacheLine[j];
                    }
                    // Starting address of next memory pkt (aligned to burst boundary)
                    addr = (addr | (burst_size - 1)) + 1;
                }

                assert(new_size % 64 == 0);
                auxPkt->setAddr(addrAligned);
                auxPkt->setSizeForMC(new_size);
                auxPkt->allocateForMC();
                auxPkt->setDataForMC(newData.data(), 0, new_size);
            }
        } else {
            /* step 2.2: if the pkt is read, access the auxPkt directly */
            assert(auxPkt->isRead());
            DPRINTF(MemCtrl, "Line %d: cur req is read \n", __LINE__);
        }
        // do the actual memory access and turn the packet into a response
        // rely on the abstract memory
        mem_intr->comprFunctionalAccess(auxPkt, burst_size, pageNum, pageBufferAddr);
        // mem_intr->comprFunctionalAccess(auxPkt, burst_size, pageNum, pageBuffer, mPageBuffer);

        delete auxPkt;
        return true;
    } else {
        return false;
    }

}

bool
MemCtrl::recvFunctionalLogicForDyL(PacketPtr pkt, MemInterface* mem_intr) {
    pkt->DyLBackup = pkt->getAddr();

    if (pkt->DyLPType != 0x100 && isAddressCovered(pkt->getAddr(), 0, 0)) {
        printf("\n\n**************\n\n");
        printf("curTick is %ld\n", curTick());
        printf("recv Functional: %s 0x%x\n",
            pkt->cmdString().c_str(), pkt->getAddr());
    }


    /*
        In the timing mode, the memory controller could receive functional pkt while processing the timing pkts
        If currently there are pkts being processed or the memory controller is blocked,
            functional requests require special handling.

        If the type of functional pkt is write,
            add to inProcessWritePkt
        If it is a read request (which requires an immediate response):
            1. read the cte from memory
            2. Read the corresponding page based on that CTE (since CTE and page updates always occur atomically)
            3. Check for collisions with any ongoing write operations.
                If a collision is detected, update the read content with data from the colliding writes.
    */

    if (pktInProcess != 0 || blockedForDyL) {
        /* should not re-enter this block */
        assert(pkt->DyLPType != 0x100);
        if (isAddressCovered(pkt->getAddr(), 0, 1))  {
            printf("the function request is block, pktInProcess %d, blockedForDyL %d\n", pktInProcess, blockedForDyL);
        }

        if (mem_intr->getAddrRange().contains(pkt->getAddr())) {
            functionalBlockedForDyL = true;
            PacketPtr auxPkt = new Packet(pkt, false, true);
            auxPkt->DyLBackup = pkt->getAddr();
            auxPkt->DyLPType = 0x100;
            auxPkt->allocateForMC();
            memcpy(auxPkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

            if (isAddressCovered(pkt->getAddr(), 0, 1))  {
                printf("originally the aux pkt value is: \n");
                printf("the auxPkt address is 0x%lx\n", auxPkt);
                for (int i = 0; i < pkt->getSize(); i++) {
                    printf("%02x ", static_cast<unsigned int>(auxPkt->getPtr<uint8_t>()[i]));
                }
                printf("\n");
            }


            if (pkt->isWrite()) {
                inProcessWritePkt.emplace_back(auxPkt);
            } else {
                std::vector<uint8_t> read_data(pkt->getSize(), 0);

                PPN ppn = (pkt->getAddr()) >> 12;
                Addr cteAddr = startAddrForCTE + ppn * 8;
                Addr cteAddrAligned = (cteAddr >> 6) << 6;
                uint8_t loc = (cteAddr >> 3) & ((1 << 3) - 1);
                assert (loc < 8);

                std::vector<uint8_t> cacheLine(64, 0);
                mem_intr->atomicRead(cacheLine.data(), cteAddrAligned, 64);

                uint64_t cte = 0;
                for (unsigned int i = loc * 8; i < (loc + 1) * 8; i++) {
                    cte = (cte << 8) | cacheLine[i];
                }
                if (isAddressCovered(pkt->getAddr(), 0, 1)) {
                    printf("[Functional Blocked] the cte is 0x%lx\n", cte);
                }
                if (((cte >> 63) & 0x1) != 0) {
                    if (((cte >> 62) & 0x1) == 1) {
                        // the page is compressed
                        /* read the dram address and the page size from CTE */
                        Addr addr = ((cte >> 10) & ((1ULL << 40) - 1)) << 8;
                        uint64_t pageSize = ((cte >> 50) & ((1ULL << 12) - 1)) + 1;   // size range [1, 2kiB]
                        std::vector<uint8_t> pageBuffer(pageSize, 0);
                        mem_intr->atomicRead(pageBuffer.data(), addr, pageSize);
                        std::vector<uint8_t> dPage = decompressPage(pageBuffer.data(), pageSize);
                        assert(dPage.size() == 4096);
                        assert(freeList.size() > 0);


                        uint64_t offset = (pkt->getAddr() & ((1ULL << 12) - 1));
                        memcpy(read_data.data(), dPage.data() + offset, pkt->getSize());
                    } else {
                        // tranlate the new address
                        Addr newAddr = ((cte >> 32) & ((1ULL << 30) - 1)) << 12;
                        Addr realAddr = newAddr | (pkt->getAddr() & ((1ULL << 12) - 1));
                        if (isAddressCovered(pkt->getAddr(), 0, 1)) {
                            printf("[FB] the new address is 0x%lx\n", newAddr);
                            printf("[FB] the real address is 0x%lx\n", realAddr);
                        }
                        mem_intr->atomicRead(read_data.data(), realAddr, pkt->getSize());
                    }
                }

                memcpy(pkt->getPtr<uint8_t>(), read_data.data(), pkt->getSize());

                /* check for if collision with current write */
                for (const auto& writePkt: inProcessWritePkt) {
                    Addr writePktStart = writePkt->getAddr();
                    Addr writePktEnd = writePktStart + writePkt->getSize();
                    Addr pktStart = pkt->getAddr();
                    Addr pktEnd = pktStart + pkt->getSize();
                    if ((writePktStart < pktEnd) && (pktStart < writePktEnd)) {
                        printf("Func. read collide with write\n");
                        Addr overlap_start = std::max(writePktStart, pktStart);
                        Addr overlap_end = std::min(writePktEnd, pktEnd);
                        size_t overlap_len = overlap_end - overlap_start;
                        size_t pkt_offset = overlap_start - pktStart;
                        size_t write_pkt_offset = overlap_start - writePktStart;
                        std::memcpy(
                            pkt->getPtr<uint8_t>() + pkt_offset,
                            writePkt->getPtr<uint8_t>() + write_pkt_offset,
                            overlap_len
                        );
                    }
                }
            }
            mem_intr->functionalAccessForDyL(pkt, 1);

            functionalBlockedQueueForDyL.push_back(auxPkt);
            return true;
        } else {
            return false;
        }
    }

    DPRINTF(MemCtrl, "recvFunction: %s 0x%x\n",
        pkt->cmdString(), pkt->getAddr());

    if (pkt->DyLPType == 0x100 && isAddressCovered(pkt->getAddr(), 0, 0)) {
        printf("[F] cur Processing pkt is: %s 0x%x\n",
            pkt->cmdString().c_str(), pkt->getAddr());
        printf("[F] the pkt address is 0x%lx\n", pkt);
        for (int i = 0; i < pkt->getSize(); i++) {
            printf("%02x ", static_cast<unsigned int>(pkt->getPtr<uint8_t>()[i]));
        }
        printf("\n");
    }


    /*
        stage 2: 
        read CTE from memory (functional requests will not have effect on mcache)
    */
    PPN ppn = (pkt->getAddr()) >> 12;
    Addr cteAddr = startAddrForCTE + ppn * 8;
    Addr cteAddrAligned = (cteAddr >> 6) << 6;
    uint8_t loc = (cteAddr >> 3) & ((1 << 3) - 1);
    assert (loc < 8);

    std::vector<uint8_t> cacheLine(64, 0);
    mem_intr->atomicRead(cacheLine.data(), cteAddrAligned, 64);
    uint64_t cte = 0;
    for (unsigned int i = loc * 8; i < (loc + 1) * 8; i++) {
        cte = (cte << 8) | cacheLine[i];
    }

    // printf("functional after stage 2: cte is 0x%lx\n", cte);

    /*
        stage 3: update the receny List
            if the page is marked incompressible, add it back to the recencyList with 1% (from TMCC p5)
    */

    auto it = recencyMap.find(ppn);
    if (it != recencyMap.end()) {
        recencyList.erase(it->second);
    }

    if (incompressiblePages.find(ppn) != incompressiblePages.end()) {
        if(fakeOnePercentChance()) {
        // if(onePercentChance()) {
            recencyList.push_front(ppn);
            recencyMap[ppn] = recencyList.begin();
            incompressiblePages.erase(ppn);
        }
    } else {
        recencyList.push_front(ppn);
        recencyMap[ppn] = recencyList.begin();
    }

    /*
        stage 4: interpret the CTE
            if the cte is invalid <=> the page is visited for the first time
                allocate the memory
            else if the page is compressed:
                decompress the page
            else:
                parse the cte for the dram address
    */

    Addr addr = 0;
    if (((cte >> 63) & 0x1) == 0) {   // the cte is invalid
        addr = freeList.front();
        freeList.pop_front();
        stat_used_bytes += 4096;

        std::vector<uint8_t> zeroPage(4096, 0);
        mem_intr->atomicWrite(zeroPage, addr, 4096, 0);

        // update the CTE
        cte = (1ULL << 63) | (0ULL << 62) | (((addr >> 12) & ((1ULL << 30) - 1)) << 32);

        /* update the metacache entry */
        uint64_t cteVal = cte;
        for (int i = (loc * 8 + 7); i >= loc * 8; i--) {
            cacheLine[i] =  cteVal & ((1 << 8) - 1);
            cteVal = cteVal >> 8;
        }

    } else {
        if ((cte & (1ULL << 62)) == 0){   // the OS physical page is uncompressed
            addr = ((cte >> 32) & ((1ULL << 30) - 1)) << 12;
        } else {
            DPRINTF(MemCtrl, "Functional Opps, we read/write physical page which is compressed\n");
            uint64_t compressedSize = ((cte >> 50) & ((1ULL << 12) - 1)) + 1;   // size range [1, 2kiB]

            std::vector<uint8_t> pageBuffer(compressedSize, 0);
            Addr oldAddr = ((cte >> 10) & ((1ULL << 40) - 1)) << 8;

            /* find a location for the page*/
            addr = freeList.front();
            freeList.pop_front();
            stat_used_bytes += 4096;

            // read the data into the pageBuffer
            mem_intr->atomicRead(pageBuffer.data(), oldAddr, compressedSize);

            // Decompress and copy the data to the new page
            std::vector<uint8_t> decompressedPage = decompressPage(pageBuffer.data(), compressedSize);

            mem_intr->atomicWrite(decompressedPage, addr, decompressedPage.size());
            if (compressedSize <= 256) {
                smallFreeList.push_back(oldAddr);
                stat_used_bytes -= 256;
            } else if (compressedSize <= 1024) {
                moderateFreeList.push_back(oldAddr);
                stat_used_bytes -= 1024;
            } else {
                assert(compressedSize <= 2048);
                largeFreeList.push_back(oldAddr);
                stat_used_bytes -= 2048;
            }

           /* update the metacache entry */
            cte = (1ULL << 63) | (0ULL << 62) | (((addr >> 12) & ((1ULL << 30) - 1)) << 32);

            uint64_t cteVal = cte;
            for (int i = (loc * 8 + 7); i >= loc * 8; i--) {
                cacheLine[i] = cteVal & ((1 << 8) - 1);
                cteVal = cteVal >> 8;
            }
        }
    }

    mem_intr->atomicWrite(cacheLine, cteAddrAligned, cacheLine.size());

    if (mcache.isExist(cteAddrAligned)) {
        mcache.updateIfExist(cteAddrAligned, cacheLine);
    }


    /*
        stage 5:
            try to compress the cold page if the current memory usage exceeds the threshold.
            will stop when the memory usage fall back or recency list becomes empty
    */

    while (stat_used_bytes > memoryUsageThreshold && recencyList.size() > 1) {
        PPN coldPageId = recencyList.back();
        recencyList.pop_back();
        recencyMap.erase(coldPageId);
        
        /* read the cold page from memory*/
        std::vector<uint8_t> pageForCompress(4096, 0);

        Addr coldCteAddr = startAddrForCTE + coldPageId * 8;
        Addr coldCteAddrAligned = (coldCteAddr >> 6) << 6;
        int8_t coldLoc = (coldCteAddr >> 3) & ((1 << 3) - 1);

        std::vector<uint8_t> cteCL(64, 0);
        mem_intr->atomicRead(cteCL.data(), coldCteAddrAligned, 64);

        uint64_t oldCTE = 0;
        for (unsigned int i = coldLoc * 8; i < (coldLoc + 1) * 8; i++) {
            oldCTE = (oldCTE << 8) | cteCL[i];
        }
        uint64_t pagePtr = ((oldCTE >> 32) & ((1ULL << 30) - 1)) << 12;
        uint64_t newCTE = 0;

        assert(((oldCTE >> 62) & 0x1) == 0);
        mem_intr->atomicRead(pageForCompress.data(), pagePtr, 4096);
        std::vector<uint8_t> compressedPage = compressPage(pageForCompress.data(), 4096);

        uint64_t cSize = compressedPage.size();

        if (cSize > 2048) {
            incompressiblePages.emplace(coldPageId);
            continue;
        }

        freeList.push_back(pagePtr);
        // printf("Line %d, freeList push back 0x%lx\n", __LINE__, pagePtr);
        stat_used_bytes -= 4096;
            
        Addr newAddr = 0;
        if (cSize <= 256) {
            if (smallFreeList.size() > 0) {
                newAddr = smallFreeList.front();
                smallFreeList.pop_front();
            } else {
                newAddr = freeList.front();
                freeList.pop_front();
                for (int i = 1; i < 16; i++) {
                    smallFreeList.push_back(newAddr | (i << 8));
                }
            }
            stat_used_bytes += 256;
        } else if (cSize <= 1024) {
            if (moderateFreeList.size() > 0) {
                newAddr = moderateFreeList.front();
                moderateFreeList.pop_front();
            } else {
                newAddr = freeList.front();
                freeList.pop_front();
                for (int i = 1; i < 4; i++) {
                    moderateFreeList.push_back(newAddr | (i << 10));
                }
            }
            stat_used_bytes += 1024;
        } else {
            if (largeFreeList.size() > 0) {
                newAddr = largeFreeList.front();
                largeFreeList.pop_front();
            } else {
                newAddr = freeList.front();
                freeList.pop_front();
                largeFreeList.push_back(newAddr | (1 << 11));
            }
            stat_used_bytes += 2048;
        }

        // copy the compressed data into the space at newAddr
        mem_intr->atomicWrite(compressedPage, newAddr, compressedPage.size());
    
        // update the CTE (uncompressed to compressed)
        newCTE = (1ULL << 63) | (1ULL << 62) | (((cSize - 1) & ((1ULL << 12) - 1)) << 50) | ((newAddr >> 8) << 10);

        // update CTE in memory
        for (int i = 8 * coldLoc + 7; i >= 8 * coldLoc; i--) {
            cteCL[i] = newCTE & ((1 << 8) - 1);
            newCTE = newCTE >> 8;
        }
        mem_intr->atomicWrite(cteCL, coldCteAddrAligned, cteCL.size());

        if (coldCteAddrAligned == cteAddrAligned) {
            // printf("collision\n");
            memcpy(cacheLine.data(), cteCL.data(), 64);
            for (int i = loc * 8; i < (loc + 1) * 8; i++) {
                cte = (cte << 8) | cacheLine[i];
            }
        }

        mcache.updateIfExist(coldCteAddrAligned, cteCL);
    }

    /*
        stage 6: process the pkt
            translate the physical address to dram address
            read/write the data from/to memory
    */

    // tranlate the new address

    // printf("stage 6: cte is 0x%lx\n", cte);
    Addr pageAddr = ((cte >> 32) & ((1ULL << 30) - 1)) << 12;
    Addr realAddr = pageAddr | (pkt->getAddr() & ((1ULL << 12) - 1));

    if (mem_intr->getAddrRange().contains(pkt->getAddr())) {

        pkt->setAddr(realAddr);
        // rely on the abstract memory

        if (pkt->DyLPType == 0x100) {
            if (pkt->isWrite()) {
                assert(std::find(inProcessWritePkt.begin(), inProcessWritePkt.end(), pkt) != inProcessWritePkt.end());
                inProcessWritePkt.remove(pkt);
            }
            mem_intr->functionalAccessForDyL(pkt, 2);
            delete pkt;
        } else {
            // printf("pkt address is 0x%lx\n", pkt->getAddr());
            mem_intr->functionalAccessForDyL(pkt, 0);
        }
        return true;
    } else {
        assert(pkt->DyLPType != 0x100);
        return false;
    }

    // // if the CTE is not valid, allocate from the freeList and update the CTE
    // Addr dramAddr = 0;
    // if (((cte >> 63) & 0x1) == 0) {
    //     // printf("[F] CTE is invalid\n");
    //     dramAddr = freeList.front();
    //     freeList.pop_front();
    //     stat_used_bytes += 4096;

    //     std::vector<uint8_t> zeroPage(4096, 0);
    //     mem_intr->atomicWrite(zeroPage, dramAddr, 4096, 0);

    //     // update the CTE;
    //     cte = (1ULL << 63) | (0ULL << 62) | (((dramAddr >> 12) & ((1ULL << 30) - 1)) << 32);
    //     uint64_t val = cte;
    //     for (int i = loc * 8 + 7; i >= loc * 8; i--) {
    //         // DPRINTF(MemCtrl, "[i = %d]\n", i);
    //         cacheLine[i] = val & ((1ULL << 8) - 1);
    //         val = val >> 8;
    //     }
    //     // DPRINTF(MemCtrl, "The address is 0x%llx, the loc = %d\n", cteAddrAligned, loc);
    //     // DPRINTF(MemCtrl, "the cte is 0x%lx\n", cte);

    //     mcache.add(cteAddrAligned, cacheLine);

    //     if (coverageTestMC(cteAddrAligned, 0x198662, cacheLine.size())) {
    //         printf("Line %d: the addr is 0x%lx, size is %d\n", __LINE__, cteAddrAligned, cacheLine.size());
    //     }

    //     mem_intr->atomicWrite(cacheLine, cteAddrAligned, cacheLine.size());
    //     // recencyList.push_front(ppn);
    // }

    // auto it = recencyMap.find(ppn);
    // if (it != recencyMap.end()) {
    //     recencyList.erase(it->second);
    // }

    // if (incompressiblePages.find(ppn) != incompressiblePages.end()) {
    //     if(onePercentChance()) {
    //         recencyList.push_front(ppn);
    //         recencyMap[ppn] = recencyList.begin();
    //         incompressiblePages.erase(ppn);
    //     }
    // } else {
    //     recencyList.push_front(ppn);
    //     recencyMap[ppn] = recencyList.begin();
    // }

    // if (recencyList.size() > recencyListThreshold) {

    //     assert(pktInProcess == 0 && !blockedForDyL);
    //     assert(recencyList.size() == recencyListThreshold + 1);

    //     for (int cnt = 0; cnt < recencyListThreshold + 1; cnt++) {
    //         PPN coldPageId = recencyList.back();
    //         recencyList.pop_back();
    //         recencyMap.erase(coldPageId);

    //         if (isAddressCovered(pkt->getAddr(), 0, 1)) {
    //             printf("need to compress\n");
    //             printf("the cold page is %d\n", coldPageId);
    //         }

    //         std::vector<uint8_t> pageForCompress(4096, 0);

    //         Addr coldCteAddr = startAddrForCTE + coldPageId * 8;
    //         Addr coldCteAddrAligned = (coldCteAddr >> 6) << 6;
    //         int8_t coldLoc = (coldCteAddr >> 3) & ((1 << 3) - 1);

    //         std::vector<uint8_t> cteCL(64, 0);
    //         mem_intr->atomicRead(cteCL.data(), coldCteAddrAligned, 64);

    //         uint64_t oldCTE = 0;
    //         for (unsigned int i = coldLoc * 8; i < (coldLoc + 1) * 8; i++) {
    //             oldCTE = (oldCTE << 8) | cteCL[i];
    //         }
    //         uint64_t pagePtr = ((oldCTE >> 32) & ((1ULL << 30) - 1)) << 12;
    //         uint64_t newCTE = 0;
    //         assert(((oldCTE >> 62) & 0x1) == 0);
    //         mem_intr->atomicRead(pageForCompress.data(), pagePtr, 4096);
    //         std::vector<uint8_t> compressedPage = compressPage(pageForCompress.data(), 4096);

    //         uint64_t cSize = compressedPage.size();

    //         Addr newAddr = 0;
    //         //  printf("try to compress the page, cSize is %d\n", cSize);
    //         if (cSize <= 2048) {
    //             if (coverageTestMC(pagePtr, 0, 4096)) {
    //                 printf("give back freed page, the page number is %d, address is 0x%lx\n", (coldPageId), pagePtr);
    //                 if (findSameElem(pagePtr)) {
    //                     printf("line %d: the address is 0x%lx\n", __LINE__, pagePtr);
    //                     panic("duplicate?");
    //                 }
    //             }
    //             freeList.push_back(pagePtr);
    //             assert(stat_used_bytes >= 4096);
    //             stat_used_bytes -= 4096;
    //             if (cSize <= 256) {
    //                 if (smallFreeList.size() > 0) {
    //                     newAddr = smallFreeList.front();
    //                     smallFreeList.pop_front();
    //                 } else {
    //                     newAddr = freeList.front();
    //                     freeList.pop_front();
    //                     for (int i = 1; i < 16; i++) {
    //                         smallFreeList.push_back(newAddr | (i << 8));
    //                     }
    //                 }
    //                 stat_used_bytes += 256;
    //             } else if (cSize <= 1024) {
    //                 if (moderateFreeList.size() > 0) {
    //                     newAddr = moderateFreeList.front();
    //                     moderateFreeList.pop_front();
    //                 } else {
    //                     newAddr = freeList.front();
    //                     freeList.pop_front();
    //                     for (int i = 1; i < 4; i++) {
    //                         moderateFreeList.push_back(newAddr | (i << 10));
    //                     }
    //                 }
    //                 stat_used_bytes += 1024;
    //             } else {
    //                 if (largeFreeList.size() > 0) {
    //                     newAddr = largeFreeList.front();
    //                     largeFreeList.pop_front();
    //                 } else {
    //                     newAddr = freeList.front();
    //                     freeList.pop_front();
    //                     largeFreeList.push_back(newAddr | (1 << 11));
    //                 }
    //                 stat_used_bytes += 2048;
    //             }
    //             //  DPRINTF(MemCtrl, "new address is 0x%llx\n", newAddr);
    //             // copy the compressed data into the space at newAddr

    //             if (coverageTestMC(newAddr, 0x198662, compressedPage.size())) {
    //                 printf("Line %d: the addr is 0x%lx, size is %d\n", __LINE__, newAddr, compressedPage.size());
    //             }
    //             mem_intr->atomicWrite(compressedPage, newAddr, compressedPage.size());
    //             // update the CTE (uncompressed to compressed)
    //             newCTE = (1ULL << 63) | (1ULL << 62) | (((cSize - 1) & ((1ULL << 12) - 1)) << 50) | ((newAddr >> 8) << 10);

    //             if (coverageTestMC(pagePtr, 0, 4096)) {
    //                 printf("Line %d: new cte is %lx\n", __LINE__, newCTE);
    //             }
    //             //  DPRINTF(MemCtrl, "The new CTE is 0x%llx\n", newCTE);
    //             // update CTE in memory
    //             for (int i = 8 * coldLoc + 7; i >= 8 * coldLoc; i--) {
    //                 cteCL[i] = newCTE & ((1 << 8) - 1);
    //                 newCTE = newCTE >> 8;
    //             }
    //             //  DPRINTF(MemCtrl, "update the curCL\n");

    //             if (coverageTestMC(coldCteAddrAligned, 0x198662, cteCL.size())) {
    //                 printf("Line %d: the addr is 0x%lx, size is %d\n", __LINE__, coldCteAddrAligned, cteCL.size());
    //             }

    //             mem_intr->atomicWrite(cteCL, coldCteAddrAligned, cteCL.size());

    //             if (coldCteAddrAligned == cteAddrAligned) {
    //                 memcpy(cacheLine.data(), cteCL.data(), 64);
    //             }

    //             mcache.updateIfExist(coldCteAddrAligned, cteCL);

    //             break;
    //         } else {
    //             incompressiblePages.emplace(coldPageId);
    //         }

    //         if (cSize <= 2048) {
    //             panic("should never reach this when page is compressible");
    //         }

    //     }
    // }

    // // printf("the cte for current page %d is 0x%lx\n", ppn, cte);
    // if (((cte >> 62) & 0x1) == 1) {
    //     assert(pagesInCompress.find(ppn) == pagesInCompress.end());
    //     assert(pagesInDecompress.find(ppn) == pagesInDecompress.end());
    //     // the page is compressed
    //     /* read the dram address and the page size from CTE */
    //     Addr addr = ((cte >> 10) & ((1ULL << 40) - 1)) << 8;
    //     uint64_t pageSize = ((cte >> 50) & ((1ULL << 12) - 1)) + 1;   // size range [1, 2kiB]
    //     std::vector<uint8_t> pageBuffer(pageSize, 0);
    //     mem_intr->atomicRead(pageBuffer.data(), addr, pageSize);
    //     if (pageSize <= 256) {
    //         smallFreeList.push_back(addr);
    //         stat_used_bytes -= 256;
    //     } else if (pageSize <= 1024) {
    //         moderateFreeList.push_back(addr);
    //         stat_used_bytes -= 1024;
    //     } else {
    //         assert(pageSize <= 2048);
    //         largeFreeList.push_back(addr);
    //         stat_used_bytes -= 2048;
    //     }
    //     std::vector<uint8_t> dPage = decompressPage(pageBuffer.data(), pageSize);
    //     assert(dPage.size() == 4096);
    //     assert(freeList.size() > 0);

    //     /* find a location for the page*/
    //     Addr newAddr = freeList.front();
    //     freeList.pop_front();

    //     stat_used_bytes += 4096;

    //     if (coverageTestMC(newAddr, 0x198662, 4096)) {
    //         printf("Line %d: the addr is 0x%lx, size is %d\n", __LINE__, newAddr, 4096);
    //     }

    //     mem_intr->atomicWrite(dPage, newAddr, 4096, 0);
    //     cte = (1ULL << 63) | (0ULL << 62) | (((newAddr >> 12) & ((1ULL << 30) - 1)) << 32);
    //     // printf("[F] because the page is compressed, the new cte is 0x%lx\n", cte);

    //     uint64_t val = cte;
    //     for (int i = loc * 8 + 7; i >= loc * 8; i--) {
    //         // DPRINTF(MemCtrl, "[i = %d]\n", i);
    //         cacheLine[i] = val & ((1ULL << 8) - 1);
    //         val = val >> 8;
    //     }

    //     if (coverageTestMC(cteAddrAligned, 0x198662, cacheLine.size())) {
    //         printf("Line %d: the addr is 0x%lx, size is %d\n", __LINE__, cteAddrAligned, cacheLine.size());
    //     }

    //     mem_intr->atomicWrite(cacheLine, cteAddrAligned, cacheLine.size());
    //     if (mcache.isExist(cteAddrAligned)) {
    //         mcache.updateIfExist(cteAddrAligned, cacheLine);
    //     }
    // }
    // // tranlate the new address
    // Addr newAddr = ((cte >> 32) & ((1ULL << 30) - 1)) << 12;
    // Addr realAddr = newAddr | (pkt->getAddr() & ((1ULL << 12) - 1));

    // if (isAddressCovered(pkt->getAddr(), 0, 0)) {
    //     printf("[F] the new address is 0x%lx\n", newAddr);
    //     printf("[F] the real address is 0x%lx\n", realAddr);
    // }


    // if (mem_intr->getAddrRange().contains(pkt->getAddr())) {

    //     pkt->setAddr(realAddr);
    //     // rely on the abstract memory

    //     if (pkt->DyLPType == 0x100) {
    //         if (pkt->isWrite()) {
    //             assert(std::find(inProcessWritePkt.begin(), inProcessWritePkt.end(), pkt) != inProcessWritePkt.end());
    //             inProcessWritePkt.remove(pkt);
    //         }
    //         if (isAddressCovered(pkt->DyLBackup, 0, 0)) {
    //             printf("[F] the pkt type is 0x100\n");
    //             printf("[F] is the pkt write ?\n", pkt->isWrite());
    //         }
    //         mem_intr->functionalAccessForDyL(pkt, 2);
    //         delete pkt;
    //     } else {
    //         mem_intr->functionalAccessForDyL(pkt, 0);
    //     }
    //     return true;
    // } else {
    //     assert(pkt->DyLPType != 0x100);
    //     return false;
    // }
}

bool
MemCtrl::recvFunctionalLogicForNew(PacketPtr pkt, MemInterface* mem_intr, bool hasBlocked) {
    if (hasBlocked) {
        assert(pkt->newPType == 0x100);
        unsigned size = pkt->getSize();
        uint32_t burst_size = mem_intr->bytesPerBurst();

        unsigned offset = pkt->getAddr() & (burst_size - 1);
        unsigned int pkt_count = divCeil(offset + size, burst_size);

        /* Step 1: atomic read metadata from memory or mcache */
        Addr base_addr = pkt->getAddr();
        Addr addr = base_addr;

        for (unsigned int i = 0; i < pkt_count; i++) {
            PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

            if (pkt->newfunctionMetaDataMap.find(ppn) == pkt->newfunctionMetaDataMap.end()) {
                /* step 1.1: calculate the MPA for metadata */
                Addr memory_addr = ppn * 64;
                std::vector<uint8_t> metaData(64, 0);
                if (mcache.isExist(memory_addr)) {
                    metaData = mcache.find(memory_addr);
                } else {
                    mem_intr->atomicRead(metaData.data(), memory_addr, 64);
                }

                if (!isValidMetaData(metaData)) {
                    metaData = originMetaData;
                    new_allocateBlock(metaData, 1);
                }

                pkt->newfunctionMetaDataMap[ppn] = metaData;
            }
            // Starting address of next memory pkt (aligned to burst boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }

        /* step 2: process the pkt based on isWrite or isRead */

        if (pkt->isWrite()) {

            Addr addrAligned = (base_addr >> 6) << 6;
            uint64_t new_size = ((((base_addr + size) + (burst_size - 1)) >> 6) << 6) - addrAligned;

            assert(burst_size == 64);
            assert(new_size == (addr - addrAligned));
            assert(new_size == pkt_count * burst_size);

            if (pkt->cmd == MemCmd::SwapReq) {
                panic("not support yet");
            } else {
                addr = base_addr;
                std::vector<uint8_t> newData(new_size, 0);

                /* process the pkt one by one */
                for (unsigned int i = 0; i < pkt_count; i++) {
                    PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

                    uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

                    assert(pkt->newfunctionMetaDataMap.find(ppn) != pkt->newfunctionMetaDataMap.end());
                    std::vector<uint8_t> metaData = pkt->newfunctionMetaDataMap[ppn];


                    /* has to read the old cacheline (compressed form) from memory */
                    std::vector<uint8_t> cacheLine(64, 0);

                    uint8_t type = new_getType(metaData, cacheLineIdx);

                    // printf("the type is %d\n", static_cast<unsigned int>(type));

                    std::vector<uint64_t> translationRes(3, 0);

                    translationRes[0] = zeroAddr;


                    if(new_getCoverage(metaData) <= cacheLineIdx) {
                        assert(type == 0);
                    } else {
                        translationRes = new_addressTranslation(metaData, cacheLineIdx);
                    }

                    if (type >= 0b100) {
                        mem_intr->atomicRead(cacheLine.data(), translationRes[0], 1);
                        uint8_t overflowIdx = cacheLine[0];
                        Addr overflow_addr = calOverflowAddr(metaData, overflowIdx);
                        mem_intr->atomicRead(cacheLine.data(), overflow_addr, 64);
                    } else {
                        assert(translationRes[2] < sizeMap[type]);
                        if (translationRes[2] == 0) {
                            mem_intr->atomicRead(cacheLine.data(), translationRes[0], sizeMap[type]);
                        } else {
                            uint64_t prefixLen = sizeMap[type] - translationRes[2];
                            mem_intr->atomicRead(cacheLine.data(), translationRes[0], prefixLen);
                            mem_intr->atomicRead(cacheLine.data() + prefixLen, translationRes[1], translationRes[2]);
                        }

                    }

                    /* restore the data to its uncompressed form */
                    new_restoreData(cacheLine, type);

                    /* write the data */
                    uint64_t ofs = addr - base_addr;
                    uint8_t loc = addr & 0x3F;
                    size_t writeSize = std::min(64UL - loc, size - ofs);
                    // DPRINTF(MemCtrl, "(F) Line %d: start to write dat, the ofs is %lld, the loc is %d, the writeSize is %ld\n", __LINE__, ofs, static_cast<unsigned int>(loc), writeSize);
                    pkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

                    /* update the metadata according to the new value of cacheline */
                    new_updateMetaData(cacheLine, metaData, cacheLineIdx, mem_intr);

                    pkt->newfunctionMetaDataMap[ppn] = metaData;
                    Addr metadata_addr = ppn * 64;
                    mcache.add(metadata_addr, metaData);

                    mem_intr->atomicWrite(metaData, metadata_addr, 64, 0);

                    for (unsigned int j = 0; j < 64; j++) {
                        newData[i * 64 + j] = cacheLine[j];
                    }
                    // Starting address of next memory pkt (aligned to burst boundary)
                    addr = (addr | (burst_size - 1)) + 1;
                }


                assert(new_size % 64 == 0);
                pkt->setAddr(addrAligned);
                pkt->setSizeForMC(new_size);
                pkt->allocateForMC();
                pkt->setDataForMC(newData.data(), 0, new_size);
                mem_intr->functionalAccessForNew(pkt, burst_size, zeroAddr, 2);
            }
            assert(std::find(inProcessWritePkt.begin(), inProcessWritePkt.end(), pkt) != inProcessWritePkt.end());
            inProcessWritePkt.remove(pkt);

        } else {
            /* do nothing */
        }
        delete pkt;
    } else {
        DPRINTF(MemCtrl, "recv Functional: %s 0x%x\n",
            pkt->cmdString(), pkt->getAddr());
        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)){
            printf("recv Functional: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
        }

        if (blockedForNew) {
            PacketPtr auxPkt = new Packet(pkt);
            // copy the content
            auxPkt->newPType = 0x100;
            auxPkt->allocateForMC();
            memcpy(auxPkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());
            assert(!hasBlocked);
            if (mem_intr->getAddrRange().contains(pkt->getAddr())) {
                waitQueueForNew.emplace_back(std::make_pair(auxPkt, 0));
                unsigned size = pkt->getSize();
                uint32_t burst_size = mem_intr->bytesPerBurst();

                unsigned offset = pkt->getAddr() & (burst_size - 1);
                unsigned int pkt_count = divCeil(offset + size, burst_size);

                /* Step 1: atomic read metadata from memory or mcache */
                Addr base_addr = pkt->getAddr();
                Addr addr = base_addr;

                if (pkt->isRead()) {

                    for (unsigned int i = 0; i < pkt_count; i++) {
                        PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

                        if (pkt->newfunctionMetaDataMap.find(ppn) == pkt->newfunctionMetaDataMap.end()) {
                            /* step 1.1: calculate the MPA for metadata */
                            Addr memory_addr = ppn * 64;
                            std::vector<uint8_t> metaData(64, 0);
                            if (mcache.isExist(memory_addr)) {
                                metaData = mcache.find(memory_addr);
                            } else {
                                mem_intr->atomicRead(metaData.data(), memory_addr, 64);
                            }

                            if (!isValidMetaData(metaData)) {
                                metaData = originMetaData;
                                new_allocateBlock(metaData, 1);
                            }

                            pkt->newfunctionMetaDataMap[ppn] = metaData;
                        }
                        // Starting address of next memory pkt (aligned to burst boundary)
                        addr = (addr | (burst_size - 1)) + 1;
                    }

                    /* step 2: process the pkt  */
                    /* step 2.2: if the pkt is read, access the auxPkt directly */
                    mem_intr->functionalAccessForNew(pkt, burst_size, zeroAddr, 1);

                    /* check for if collision with current write */
                    for (const auto& writePkt: inProcessWritePkt) {
                        Addr writePktStart = writePkt->getAddr();
                        Addr writePktEnd = writePktStart + writePkt->getSize();
                        Addr pktStart = pkt->getAddr();
                        Addr pktEnd = pktStart + pkt->getSize();
                        if ((writePktStart < pktEnd) && (pktStart < writePktEnd)) {
                            printf("Func. read collide with write\n");
                            Addr overlap_start = std::max(writePktStart, pktStart);
                            Addr overlap_end = std::min(writePktEnd, pktEnd);
                            size_t overlap_len = overlap_end - overlap_start;
                            size_t pkt_offset = overlap_start - pktStart;
                            size_t write_pkt_offset = overlap_start - writePktStart;
                            std::memcpy(
                                pkt->getPtr<uint8_t>() + pkt_offset,
                                writePkt->getPtr<uint8_t>() + write_pkt_offset,
                                overlap_len
                            );
                        }
                    }
                } else {
                    assert(pkt->isWrite());
                    inProcessWritePkt.emplace_back(auxPkt);
                    mem_intr->functionalAccessForNew(pkt, burst_size, zeroAddr, 1);
                }
                return true;
            } else {
                return false;
            }
        } else {
            if (mem_intr->getAddrRange().contains(pkt->getAddr())) {
                /* Step 0: create an auxPkt for write */
                PacketPtr aux_pkt = new Packet(pkt);
                aux_pkt->allocateForMC();
                memcpy(aux_pkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

                unsigned size = aux_pkt->getSize();
                uint32_t burst_size = mem_intr->bytesPerBurst();

                unsigned offset = aux_pkt->getAddr() & (burst_size - 1);
                unsigned int pkt_count = divCeil(offset + size, burst_size);

                /* Step 1: atomic read metadata from memory or mcache */
                Addr base_addr = aux_pkt->getAddr();
                Addr addr = base_addr;

                for (unsigned int i = 0; i < pkt_count; i++) {
                    PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

                    if (aux_pkt->newfunctionMetaDataMap.find(ppn) == aux_pkt->newfunctionMetaDataMap.end()) {
                        /* step 1.1: calculate the MPA for metadata */
                        Addr memory_addr = ppn * 64;
                        std::vector<uint8_t> metaData(64, 0);
                        if (mcache.isExist(memory_addr)) {
                            metaData = mcache.find(memory_addr);
                        } else {
                            mem_intr->atomicRead(metaData.data(), memory_addr, 64);
                        }

                        if (!isValidMetaData(metaData)) {
                            metaData = originMetaData;
                            new_allocateBlock(metaData, 1);
                        }

                        aux_pkt->newfunctionMetaDataMap[ppn] = metaData;
                    }
                    // Starting address of next memory pkt (aligned to burst boundary)
                    addr = (addr | (burst_size - 1)) + 1;
                }

                /* step 2: process the pkt based on isWrite or isRead */

                if (pkt->isWrite()) {

                    Addr addrAligned = (base_addr >> 6) << 6;
                    uint64_t new_size = ((((base_addr + size) + (burst_size - 1)) >> 6) << 6) - addrAligned;

                    assert(burst_size == 64);
                    assert(new_size == (addr - addrAligned));
                    assert(new_size == pkt_count * burst_size);

                    if (pkt->cmd == MemCmd::SwapReq) {
                        panic("not support yet");
                    } else {
                        addr = base_addr;
                        std::vector<uint8_t> newData(new_size, 0);

                        /* process the pkt one by one */
                        for (unsigned int i = 0; i < pkt_count; i++) {
                            PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

                            uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

                            assert(aux_pkt->newfunctionMetaDataMap.find(ppn) != aux_pkt->newfunctionMetaDataMap.end());
                            std::vector<uint8_t> metaData = aux_pkt->newfunctionMetaDataMap[ppn];


                            /* has to read the old cacheline (compressed form) from memory */
                            std::vector<uint8_t> cacheLine(64, 0);

                            uint8_t type = new_getType(metaData, cacheLineIdx);

                            // printf("the type is %d\n", static_cast<unsigned int>(type));

                            std::vector<uint64_t> translationRes(3, 0);

                            translationRes[0] = zeroAddr;


                            if(new_getCoverage(metaData) <= cacheLineIdx) {
                                assert(type == 0);
                            } else {
                                translationRes = new_addressTranslation(metaData, cacheLineIdx);
                            }

                            if (type >= 0b100) {
                                mem_intr->atomicRead(cacheLine.data(), translationRes[0], 1);
                                uint8_t overflowIdx = cacheLine[0];
                                Addr overflow_addr = calOverflowAddr(metaData, overflowIdx);
                                mem_intr->atomicRead(cacheLine.data(), overflow_addr, 64);
                            } else {
                                assert(translationRes[2] < sizeMap[type]);
                                if (translationRes[2] == 0) {
                                    mem_intr->atomicRead(cacheLine.data(), translationRes[0], sizeMap[type]);
                                } else {
                                    uint64_t prefixLen = sizeMap[type] - translationRes[2];
                                    mem_intr->atomicRead(cacheLine.data(), translationRes[0], prefixLen);
                                    mem_intr->atomicRead(cacheLine.data() + prefixLen, translationRes[1], translationRes[2]);
                                }

                            }

                            /* restore the data to its uncompressed form */
                            new_restoreData(cacheLine, type);

                            /* write the data */
                            uint64_t ofs = addr - base_addr;
                            uint8_t loc = addr & 0x3F;
                            size_t writeSize = std::min(64UL - loc, size - ofs);
                            // DPRINTF(MemCtrl, "(F) Line %d: start to write dat, the ofs is %lld, the loc is %d, the writeSize is %ld\n", __LINE__, ofs, static_cast<unsigned int>(loc), writeSize);
                            aux_pkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

                            /* update the metadata according to the new value of cacheline */
                            new_updateMetaData(cacheLine, metaData, cacheLineIdx, mem_intr);

                            aux_pkt->newfunctionMetaDataMap[ppn] = metaData;
                            Addr metadata_addr = ppn * 64;
                            mcache.add(metadata_addr, metaData);

                            mem_intr->atomicWrite(metaData, metadata_addr, 64, 0);

                            for (unsigned int j = 0; j < 64; j++) {
                                newData[i * 64 + j] = cacheLine[j];
                            }
                            // Starting address of next memory pkt (aligned to burst boundary)
                            addr = (addr | (burst_size - 1)) + 1;
                        }


                        assert(new_size % 64 == 0);
                        aux_pkt->setAddr(addrAligned);
                        aux_pkt->setSizeForMC(new_size);
                        aux_pkt->allocateForMC();
                        aux_pkt->setDataForMC(newData.data(), 0, new_size);
                    }

                    mem_intr->functionalAccessForNew(aux_pkt, burst_size, zeroAddr, 0);
                    delete aux_pkt;

                } else {
                    /* step 2.2: if the pkt is read, access the auxPkt directly */
                    assert(pkt->isRead());
                    mem_intr->functionalAccessForNew(aux_pkt, burst_size, zeroAddr, 0);
                    assert(aux_pkt->getSize() == pkt->getSize());
                    memcpy(pkt->getPtr<uint8_t>(), aux_pkt->getPtr<uint8_t>(), pkt->getSize());
                    delete aux_pkt;
                }
                return true;
            } else {
                return false;
            }
        }
    }

    // PacketPtr auxPkt = pkt;

    // if (!hasBlocked) {
    //     DPRINTF(MemCtrl, "recv Functional: %s 0x%x\n",
    //         pkt->cmdString(), pkt->getAddr());
    //     if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 0)){
    //         printf("recv Functional: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
    //     }
    //     auxPkt = new Packet(pkt);
    //     // copy the content
    //     auxPkt->allocateForMC();
    //     memcpy(auxPkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());
    // }

    // if (blockedForNew) {
    //     assert(!hasBlocked);
    //     if (mem_intr->getAddrRange().contains(pkt->getAddr())) {
    //         waitQueueForNew.emplace_back(std::make_pair(auxPkt, 0));
    //         return true;
    //     } else {
    //         return false;
    //     }
    // }

    // // printf("\n\n========recv new pkt============\n\n");

    // if (mem_intr->getAddrRange().contains(pkt->getAddr())) {
    //     /* step 0: create an auxiliary packet for processing the pkt */
    //     unsigned size = pkt->getSize();
    //     uint32_t burst_size = mem_intr->bytesPerBurst();

    //     unsigned offset = pkt->getAddr() & (burst_size - 1);
    //     unsigned int pkt_count = divCeil(offset + size, burst_size);

    //     /* Step 1: atomic read metadata from memory or mcache */
    //     Addr base_addr = pkt->getAddr();
    //     Addr addr = base_addr;

    //     for (unsigned int i = 0; i < pkt_count; i++) {
    //         PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

    //         if (auxPkt->newfunctionMetaDataMap.find(ppn) == auxPkt->newfunctionMetaDataMap.end()) {
    //             /* step 1.1: calculate the MPA for metadata */
    //             Addr memory_addr = ppn * 64;
    //             std::vector<uint8_t> metaData(64, 0);
    //             if (mcache.isExist(memory_addr)) {
    //                 metaData = mcache.find(memory_addr);
    //             } else {
    //                 mem_intr->atomicRead(metaData.data(), memory_addr, 64);
    //             }

    //             if (!isValidMetaData(metaData)) {
    //                 metaData = originMetaData;
    //                 new_allocateBlock(metaData, 1);
    //             }

    //             auxPkt->newfunctionMetaDataMap[ppn] = metaData;
    //         }
    //         // Starting address of next memory pkt (aligned to burst boundary)
    //         addr = (addr | (burst_size - 1)) + 1;
    //     }

    //     /* step 2: process the pkt based on isWrite or isRead */

    //     if (auxPkt->isWrite()) {
    //         Addr addrAligned = (base_addr >> 6) << 6;
    //         uint64_t new_size = ((((base_addr + size) + (burst_size - 1)) >> 6) << 6) - addrAligned;

    //         assert(burst_size == 64);
    //         assert(new_size == (addr - addrAligned));
    //         assert(new_size == pkt_count * burst_size);

    //         if (auxPkt->cmd == MemCmd::SwapReq) {
    //             panic("not support yet");
    //         } else {
    //             addr = base_addr;
    //             std::vector<uint8_t> newData(new_size, 0);

    //             /* process the pkt one by one */
    //             for (unsigned int i = 0; i < pkt_count; i++) {
    //                 PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

    //                 uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

    //                 assert(auxPkt->newfunctionMetaDataMap.find(ppn) != auxPkt->newfunctionMetaDataMap.end());
    //                 std::vector<uint8_t> metaData = auxPkt->newfunctionMetaDataMap[ppn];


    //                 /* has to read the old cacheline (compressed form) from memory */
    //                 std::vector<uint8_t> cacheLine(64, 0);

    //                 uint8_t type = new_getType(metaData, cacheLineIdx);

    //                 // printf("the type is %d\n", static_cast<unsigned int>(type));

    //                 std::vector<uint64_t> translationRes(3, 0);

    //                 translationRes[0] = zeroAddr;


    //                 if(new_getCoverage(metaData) <= cacheLineIdx) {
    //                     assert(type == 0);
    //                 } else {
    //                     translationRes = new_addressTranslation(metaData, cacheLineIdx);
    //                 }

    //                 if (type >= 0b100) {
    //                     mem_intr->atomicRead(cacheLine.data(), translationRes[0], 1);
    //                     uint8_t overflowIdx = cacheLine[0];
    //                     Addr overflow_addr = calOverflowAddr(metaData, overflowIdx);
    //                     mem_intr->atomicRead(cacheLine.data(), overflow_addr, 64);
    //                 } else {
    //                     assert(translationRes[2] < sizeMap[type]);
    //                     if (translationRes[2] == 0) {
    //                         mem_intr->atomicRead(cacheLine.data(), translationRes[0], sizeMap[type]);
    //                     } else {
    //                         uint64_t prefixLen = sizeMap[type] - translationRes[2];
    //                         mem_intr->atomicRead(cacheLine.data(), translationRes[0], prefixLen);
    //                         mem_intr->atomicRead(cacheLine.data() + prefixLen, translationRes[1], translationRes[2]);
    //                     }

    //                 }

    //                 /* restore the data to its uncompressed form */
    //                 new_restoreData(cacheLine, type);

    //                 /* write the data */
    //                 uint64_t ofs = addr - base_addr;
    //                 uint8_t loc = addr & 0x3F;
    //                 size_t writeSize = std::min(64UL - loc, size - ofs);
    //                 // DPRINTF(MemCtrl, "(F) Line %d: start to write dat, the ofs is %lld, the loc is %d, the writeSize is %ld\n", __LINE__, ofs, static_cast<unsigned int>(loc), writeSize);
    //                 auxPkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

    //                 /* update the metadata according to the new value of cacheline */
    //                 new_updateMetaData(cacheLine, metaData, cacheLineIdx, mem_intr);

    //                 auxPkt->newfunctionMetaDataMap[ppn] = metaData;
    //                 Addr metadata_addr = ppn * 64;
    //                 mcache.add(metadata_addr, metaData);

    //                 mem_intr->atomicWrite(metaData, metadata_addr, 64, 0);

    //                 for (unsigned int j = 0; j < 64; j++) {
    //                     newData[i * 64 + j] = cacheLine[j];
    //                 }
    //                 // Starting address of next memory pkt (aligned to burst boundary)
    //                 addr = (addr | (burst_size - 1)) + 1;
    //             }


    //             assert(new_size % 64 == 0);
    //             auxPkt->setAddr(addrAligned);
    //             auxPkt->setSizeForMC(new_size);
    //             auxPkt->allocateForMC();
    //             auxPkt->setDataForMC(newData.data(), 0, new_size);

    //             // if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
    //                 // printf("[Functional Write] the update data in auxPkt is:");
    //                 // for (int i = 0; i < new_size; i++) {
    //                 //     if (i % 8 == 0) {
    //                 //         printf("\n");
    //                 //     }
    //                 //     printf("%02x ", static_cast<unsigned int>(*(auxPkt->getPtr<uint8_t>() + i)));
    //                 // }
    //                 // printf("\n");
    //             // }
    //         }

    //     } else {
    //         /* step 2.2: if the pkt is read, access the auxPkt directly */
    //         assert(auxPkt->isRead());
    //     }

    //     mem_intr->functionalAccessForNew(auxPkt, burst_size, zeroAddr);
    //     delete auxPkt;
    //     return true;
    // } else {
    //     delete auxPkt;
    //     return false;
    // }
}

bool
MemCtrl::recvFunctionalLogicForSecure(PacketPtr pkt, MemInterface* mem_intr) {
    DPRINTF(MemCtrl, "recv Functional: %s 0x%x\n",
        pkt->cmdString(), pkt->getAddr());

    if (isAddressCovered(pkt->getAddr(), 0, 0)) {
        printf("\n\n=====================\n\n");
        printf("recv Functional: %s 0x%x\n",
            pkt->cmdString().c_str(), pkt->getAddr());
    }

    /* read metadata from dram */
    PPN ppn = (pkt->getAddr() >> 12);
    Addr mAddr = startAddrForSecureMetaData + ppn * 8;
    std::vector<uint8_t> metaData(8);
    mem_intr->atomicRead(metaData.data(), mAddr, 8);

    Addr oldAddr = parseMetaDataForSecure(metaData, 0);
    bool updateForRead = true;

    PacketPtr auxPkt = new Packet(pkt);
    auxPkt->configAsSecureAuxPkt(pkt, oldAddr, pkt->getSize());

    bool hasUpdateMetaData = false;

    if(metaData[0] < (0x1 << 7)) {
        /* the metaData is invalid now */
        // printf("the metadata is invalid now\n");
        initialMetaDataForSecure(metaData);
        hasUpdateMetaData = true;
        // printf("the metadata is \n");

        // for (int i = 0; i < metaData.size(); i++) {
        //     printf("%lx ", metaData[i]);
        // }

        oldAddr = parseMetaDataForSecure(metaData, 0);

    }

    if (isAddressCovered(pkt->getAddr(), 8, 0)) {
        printf("the metadata is \n");
        for (int i = 0; i < 8; i++) {
            printf("%02x ", metaData[i]);
        }
        printf("\n");
    }

    if ((metaData[0] >> 6) & 0x1 == 1) {
        /* the page is compressed now */

        uint64_t cPageSize = parseMetaDataForSecure(metaData, 1);
        std::vector<uint8_t> cPage(cPageSize);
        mem_intr->atomicRead(cPage.data(), oldAddr, cPageSize);

        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
            printf("Functional: the page is compressed now\n");
            printf("the ppn is %d\n", ppn);
            printf("oldAddr is 0x%lx\n", oldAddr);
            printf("the cSize is %d\n", cPageSize);
        }

        std::vector<uint8_t> dPage = decompressPage(cPage.data(), cPageSize);
        assert(dPage.size() == 4096);

        uint64_t ofs = pkt->getAddr() & ((0x1 << 12) - 1);
        assert(ofs + pkt->getSize() <= 4096);
        if(pkt->isWrite()) {
            memcpy(dPage.data() + ofs, pkt->getPtr<uint8_t>(), pkt->getSize());
        } else {
            assert(pkt->isRead());
            memcpy(pkt->getPtr<uint8_t>(), dPage.data() + ofs, pkt->getSize());
            updateForRead = false;
        }

        cPage = compressPage(dPage.data(), 4096);
        cPageSize = cPage.size();

        if (cPage.size() <= 2048) {
            /* use the origin space */

            if (pkt->isWrite()) {
                auxPkt->setSizeForMC(cPageSize);
                auxPkt->allocateForMC();
                memcpy(auxPkt->getPtr<uint8_t>(), cPage.data(), cPageSize);
                auxPkt->setAddr(oldAddr);
            }

            for (int i = 2; i >= 1; i--) {
                metaData[i] = cPageSize & 0xFF;
                cPageSize = cPageSize >> 8;
            }
        } else {
            /* the page could not be compressed any more */
            Addr newAddr = allocateChunkForSecure(1);

            if (pkt->isWrite()) {
                auxPkt->setSizeForMC(4096);
                auxPkt->allocateForMC();
                memcpy(auxPkt->getPtr<uint8_t>(), dPage.data(), 4096);
                auxPkt->setAddr(newAddr);
            }

            /* update the metaData */
            newAddr >>= 11;

            metaData[0] = 0x80;
            for (int i = 6; i >= 3; i--) {
                metaData[i] = newAddr & 0xFF;
                newAddr >>= 8;
            }

            recycleChunkForSecure(oldAddr, 0);
        }

        hasUpdateMetaData = true;
    } else {
        Addr dram_addr = oldAddr | (pkt->getAddr() & ((0x1 << 12) - 1));
        auxPkt->setAddr(dram_addr);
    }

    if (hasUpdateMetaData) {
        for (auto& v: processPktListForSecure) {
            if (v->metaDataMapForSecure.find(ppn) != v->metaDataMapForSecure.end()) {
                v->metaDataMapForSecure[ppn] = metaData;
            }
        }

        std::vector<uint8_t> metaDataEntry(64, 0);
        memcpy(metaDataEntry.data(), metaData.data(), 8);
        if (mcache.isExist(mAddr)) {
            mcache.add(mAddr, metaDataEntry);
        }

        mem_intr->atomicWrite(metaData, mAddr, 8);
    }

    if (mem_intr->getAddrRange().contains(pkt->getAddr())) {
        // rely on the abstract memory

        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
            printf("the dram addr is 0x%lx\n", auxPkt->getAddr());
        }

        mem_intr->functionalAccessForSecure(auxPkt, access_cnt, updateForRead);
        delete auxPkt;
        return true;
    } else {
        delete auxPkt;
        return false;
    }

}

Port &
MemCtrl::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return qos::MemCtrl::getPort(if_name, idx);
    } else {
        return port;
    }
}

bool
MemCtrl::allIntfDrained() const
{
   // DRAM: ensure dram is in power down and refresh IDLE states
   // NVM: No outstanding NVM writes
   // NVM: All other queues verified as needed with calling logic
   return dram->allRanksDrained();
}

DrainState
MemCtrl::drain()
{
    // if there is anything in any of our internal queues, keep track
    // of that as well
    if (totalWriteQueueSize || totalReadQueueSize || !respQEmpty() ||
          !allIntfDrained()) {
        DPRINTF(Drain, "Memory controller not drained, write: %d, read: %d,"
                " resp: %d\n", totalWriteQueueSize, totalReadQueueSize,
                respQueue.size());

        // the only queue that is not drained automatically over time
        // is the write queue, thus kick things into action if needed
        if (totalWriteQueueSize && !nextReqEvent.scheduled()) {
            DPRINTF(Drain,"Scheduling nextReqEvent from drain\n");
            schedule(nextReqEvent, curTick());
        }

        dram->drainRanks();

        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

void
MemCtrl::drainResume()
{
    if (!isTimingMode && system()->isTimingMode()) {
        // if we switched to timing mode, kick things into action,
        // and behave as if we restored from a checkpoint
        startup();
        dram->startup();
    } else if (isTimingMode && !system()->isTimingMode()) {
        // if we switch from timing mode, stop the refresh events to
        // not cause issues with KVM
        dram->suspend();
    }

    // update the mode
    isTimingMode = system()->isTimingMode();
}

AddrRangeList
MemCtrl::getAddrRanges()
{
    AddrRangeList range;
    range.push_back(dram->getAddrRange());
    return range;
}

MemCtrl::MemoryPort::
MemoryPort(const std::string& name, MemCtrl& _ctrl)
    : QueuedResponsePort(name, queue), queue(_ctrl, *this, true),
      ctrl(_ctrl)
{ }

AddrRangeList
MemCtrl::MemoryPort::getAddrRanges() const
{
    return ctrl.getAddrRanges();
}

void
MemCtrl::MemoryPort::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(ctrl.name());

    if (!queue.trySatisfyFunctional(pkt)) {
        // Default implementation of SimpleTimingPort::recvFunctional()
        // calls recvAtomic() and throws away the latency; we can save a
        // little here by just not calculating the latency.
        ctrl.recvFunctional(pkt);
    } else {
        // The packet's request is satisfied by the queue, but queue
        // does not call makeResponse.
        // Here, change the packet to the corresponding response
        pkt->makeResponse();
    }

    pkt->popLabel();
}

void
MemCtrl::MemoryPort::recvMemBackdoorReq(const MemBackdoorReq &req,
        MemBackdoorPtr &backdoor)
{
    ctrl.recvMemBackdoorReq(req, backdoor);
}

Tick
MemCtrl::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return ctrl.recvAtomic(pkt);
}

Tick
MemCtrl::MemoryPort::recvAtomicBackdoor(
        PacketPtr pkt, MemBackdoorPtr &backdoor)
{
    return ctrl.recvAtomicBackdoor(pkt, backdoor);
}

bool
MemCtrl::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    // pass it to the memory controller
    return ctrl.recvTimingReq(pkt);
}

void
MemCtrl::MemoryPort::disableSanityCheck()
{
    queue.disableSanityCheck();
}


/* ==== specially for compresso ==== */
uint8_t MemCtrl::getType(const std::vector<uint8_t>& metaData, const uint8_t& index) {
    int startPos = (2 + 32) * 8 + index * 2;
    int loc = startPos / 8;
    int ofs = startPos % 8;
    uint8_t type = 0b11 & (metaData[loc] >> (6 - ofs));
    return type;
}

void MemCtrl::setType(std::vector<uint8_t>& metaData, const uint8_t& index, const uint8_t& type) {
    assert(type < 4);
    int startPos = (2 + 32) * 8 + index * 2;
    int loc = startPos / 8;
    int ofs = startPos % 8;
    metaData[loc] = (metaData[loc] & (~(0b11 << (6 - ofs)))) |  (type << (6 - ofs));
}


uint8_t MemCtrl::new_getType(const std::vector<uint8_t>& metaData, const uint8_t& index) {
    uint8_t type = 0;

    int startPos = 40 * 8 + index;
    int loc = startPos / 8;
    int ofs = startPos % 8;
    uint8_t prec = 0b1 & (metaData[loc] >> (7 - ofs));
    type = prec << 2;

    startPos = 48 * 8 + index * 2;
    loc = startPos / 8;
    ofs = startPos % 8;
    uint8_t succ = 0b11 & (metaData[loc] >> (6 - ofs));
    type = type | succ;

    return type;
}

void MemCtrl::new_setType(std::vector<uint8_t>& metaData, const uint8_t& index, const uint8_t& type) {
    assert(type < 8);
    uint8_t prec = (type >> 2) & 0b1;
    uint8_t succ = type & 0b11;

    /* 1. update the first part */
    int startPos = 40 * 8 + index;
    int loc = startPos / 8;
    int ofs = startPos % 8;
    metaData[loc] = (metaData[loc] & (~(0b1 << (7 - ofs)))) | (prec << (7 - ofs));

    /* 2. update the successive part */
    startPos = (48) * 8 + index * 2;
    loc = startPos / 8;
    ofs = startPos % 8;
    metaData[loc] = (metaData[loc] & (~(0b11 << (6 - ofs)))) | (succ << (6 - ofs));
}

void MemCtrl::initialPageBuffer(const PPN& ppn) {
    /* mark the corresponding metadata as valid */
    std::vector<uint8_t> metaData(64, 0);
    metaData[0] = (0b1 << 7);

    for (uint64_t i = 1; i < 64; i++) {
        if (i >= 34 && i < 50) {
            /* set all the encodings as 0b11 (uncompressed) */
            metaData[i] = 0xFF;
        } else {
            metaData[i] = 0;
        }
    }
    assert(ppn < 33554432);
    pageNum = ppn;
    hasBuffered = true;

    // memset(pageBuffer.data(), 0, pageBuffer.size() * sizeof(uint8_t));
    std::vector<uint8_t> zeroPage(4096, 0);
    dram->atomicWrite(zeroPage, pageBufferAddr, 4096);
    dram->atomicWrite(metaData, ppn * 64, 64);
}

void
MemCtrl::restoreData(std::vector<uint8_t>& cacheLine, uint8_t type) {
//    printf("enter the restore data, the type is %d\n", static_cast<uint8_t>(type));
    if (type == 0b00) {
        for (int i = 0; i < cacheLine.size(); i++) {
            cacheLine[i] = 0;
        }
    } else if (type == 0b10 || type == 0b01) {
        decompressForCompr(cacheLine);
    } else {
        assert(type == 0b11);
        /* do nothing */
    }
}

void
MemCtrl::updateCacheLine(std::vector<uint8_t>& cacheLine, PacketPtr pkt, const Addr& addr) {
    uint8_t* pktData = pkt->getPtr<uint8_t>();

    unsigned int pktSize = pkt->getSize();

    assert(addr >= pkt->getAddr());
    unsigned int ofs = addr - pkt->getAddr();

    unsigned int startPos = addr & ((1 << 6) - 1);
    assert(startPos < 64 && ofs < pktSize);
    unsigned int updateLen = std::min(64 - startPos, pktSize - ofs);

    for (unsigned int i = 0; i < updateLen; i++) {
        cacheLine[startPos + i] = pktData[ofs + i];
    }
}

std::pair<bool, Addr>
MemCtrl::addressTranslation(const std::vector<uint8_t>& metaData, uint8_t index) {
    assert(metaData.size() == 64);
    uint64_t origin_size = ((metaData[0] & (0x0F)) << 8) | metaData[1];

    uint8_t valid_inflate_num = (metaData[63] & ((0b1 << 5) - 1));  // use last 5 bit to store the counter
    //TODOs: assert(valid_inflate_num <= 17);
    //TODOs: assert(origin_size + valid_inflate_num * 64 <= 4096);

    Addr addr = 0;
    bool in_inflate = false;
    uint8_t loc = 0;

    /* check the inflate room */
    for (uint8_t i = 0; i < valid_inflate_num; i++) {
        uint8_t iIdx = ((i * 6) / 8) + 50;
        uint8_t iLoc = (i * 6) % 8;
        uint8_t candi = 0;
        if (iLoc > 2) {
            int prefix = 8 - iLoc;
            int suffix = 6 - prefix;
            candi = (metaData[iIdx] & ((1 << prefix) - 1)) << suffix;
            candi = candi | ((metaData[iIdx + 1] >> (8 - suffix)) & ((1 << suffix) - 1));
        } else {
            candi = (metaData[iIdx] >> (2 - iLoc)) & 0x3F;
        }
        if (candi == index) {
            assert(in_inflate == false);
            in_inflate = true;
            loc = i;
        }
    }
    uint64_t sumSize = 0;
    if (in_inflate) {
        sumSize = (((origin_size + 0x3F) >> 6) << 6) + loc * 64;
    } else {
        for (uint8_t u = 0; u < index; u++) {
            uint8_t type = getType(metaData, u);
            sumSize += sizeMap[type];
        }
    }
    uint8_t chunkIdx = sumSize / 512;
    for (int u = 0; u < 4; u++){   // 4B per MPFN
        addr = (addr << 8) | (metaData[2 + 4 * chunkIdx + u]);
    }
    addr = (addr << 9) | (sumSize & 0x1FF);

    return std::make_pair(in_inflate, addr);
}

bool
MemCtrl::hasFreeInflateRoom(const std::vector<uint8_t>& metaData) {
    uint64_t origin_size = ((metaData[0] & (0x0F)) << 8) | metaData[1];
    uint8_t valid_inflate_num = (metaData[63] & ((0b1 << 5) - 1));  // use last 5 bit to store the counter
    uint64_t curSize = (((origin_size + 0x3F) >> 6) << 6) + valid_inflate_num * 64;
    if ((curSize + 64 <= 4096) && (valid_inflate_num < 16)){
        return true;
    } else {
        return false;
    }
}

Addr
MemCtrl::allocateInflateRoom(std::vector<uint8_t>& metaData, const uint8_t& index) {
    uint64_t origin_size = ((metaData[0] & (0x0F)) << 8) | metaData[1];
    uint8_t valid_inflate_num = (metaData[63] & ((0b1 << 5) - 1));  // use last 5 bit to store the counter
    assert(valid_inflate_num < 17);

    uint64_t curSize = (((origin_size + 0x3F) >> 6) << 6) + valid_inflate_num * 64;
    assert(curSize + 64 <= 4096);
    assert(curSize % 64 == 0);

    Addr addr = 0;
    uint8_t chunkIdx = curSize / 512;

    if (curSize % 512 != 0) {
        /* there are enough room in current chunk */
        DPRINTF(MemCtrl, "Line %d: there are enough room for a inflated cacheline\n", __LINE__);
        for (int u = 0; u < 4; u++) {
            addr = (addr << 8) | metaData[2 + 4 * chunkIdx + u];
        }
        addr = (addr << 9) | (curSize & 0x1FF);
        assert(addr % 64 == 0);
    } else {
        /* we have to allocate a new chunk */
        DPRINTF(MemCtrl, "Line %d: Opps, we have to allocate a new chunk\n", __LINE__);
        addr = freeList.front();
        DPRINTF(MemCtrl, "Line %d, freeList pop\n", __LINE__);
        freeList.pop_front();
        uint64_t MPFN = (addr >> 9);
        for (int u = 3; u >= 0; u--) {
            metaData[2 + 4 * chunkIdx + u] = MPFN & (0xFF);
            MPFN >>= 8;
        }
        stat_used_bytes += 512;
    }
    DPRINTF(MemCtrl, "Line %d: the new address is 0x%llx\n", __LINE__, addr);
    /* update the metaData for the inflation room */
    setInflateEntry(valid_inflate_num, metaData, index);

    valid_inflate_num++;
    metaData[63] = (metaData[63] & 0xE0) | (valid_inflate_num & 0x1F);
    return addr;
}

Addr
MemCtrl::moveForwardAtomic(std::vector<uint8_t>& metaData, const uint8_t& index, MemInterface* mem_intr, bool needPrint) {
    if (needPrint) {
        printf(" you enter the move forward atomic function, the index is %d\n", index);
    }

    uint64_t origin_size = ((metaData[0] & (0x0F)) << 8) | metaData[1];
    uint8_t valid_inflate_num = (metaData[63] & ((0b1 << 5) - 1));  // use last 5 bit to store the counter
    assert(valid_inflate_num < 17);

    uint64_t curSize = (((origin_size + 0x3F) >> 6) << 6) + valid_inflate_num * 64;
    assert(curSize <= 4096);

    uint8_t loc = 0xFF;

    /* check the inflate room */
    for (uint8_t i = 0; i < valid_inflate_num; i++) {
        uint8_t candi = getInflateEntry(i, metaData);
        if (candi == index) {
            assert(loc == 0xFF);
            loc = i;
        }
    }

    if (needPrint){
        printf("valid inflate num is %d\n", valid_inflate_num);
        printf("loc is %d\n", loc);
    }

    DPRINTF(MemCtrl, "Line %d: the index of target cacheline in inflate room is %d\n", __LINE__, static_cast<unsigned int>(loc));

    assert(loc < valid_inflate_num);

    uint8_t subseqN = valid_inflate_num - loc - 1;
    uint8_t curLoc = loc + 1;
    uint8_t chunkIdx = 0;

    if (needPrint) {
        printf("curLoc: %d\n", static_cast<uint8_t>(curLoc));
    }

    for (int u = 0; u < subseqN; u++) {
        uint64_t prevSize = (((origin_size + 0x3F) >> 6) << 6) + curLoc * 64;
        assert(prevSize % 512 <= 448);  // the cacheline in the overflow region should not across the chunks
        chunkIdx = prevSize / 512;

        uint64_t MPFN = 0;
        for (int v = 0; v < 4; v++) {
            MPFN = (MPFN << 8) | metaData[2 + 4 * chunkIdx + v];
        }
        uint64_t old_start_addr = (MPFN << 9) | (prevSize % 512);

        uint8_t new_chunk_idx = (prevSize - 64) / 512;
        MPFN = 0;
        for (int v = 0; v < 4; v++) {
            MPFN = (MPFN << 8) | metaData[2 + 4 * new_chunk_idx + v];
        }
        uint64_t new_start_addr = (MPFN << 9) | ((prevSize - 64) % 512);

        if (new_chunk_idx == chunkIdx) {
            assert(new_start_addr + 64 == old_start_addr);
        }

        if (needPrint) {
            printf("old start address is 0x%lx\n", reinterpret_cast<uint64_t>(old_start_addr));
            printf("new start address is 0x%lx\n", reinterpret_cast<uint64_t>(new_start_addr));
            uint8_t test_candi = getInflateEntry(curLoc, metaData);
            printf("idx is %d\n", static_cast<unsigned int>(test_candi));
        }

        std::vector<uint8_t> val(64, 0);
        mem_intr->atomicRead(val.data(), old_start_addr, 64);

        // printf("the moved data is: \n");
        // for (int i = 0; i < val.size(); i++) {
        //    printf("%02x, ", static_cast<unsigned int>(val[i]));
        // }
        // printf("\n");

        mem_intr->atomicWrite(val, new_start_addr, 64);

        uint8_t cLIdx = getInflateEntry(curLoc, metaData);
        setInflateEntry(curLoc - 1, metaData, cLIdx);
        curLoc++;
    }

    assert(curLoc == valid_inflate_num);

    /* zero out the last space */
    setInflateEntry(valid_inflate_num - 1, metaData, 0);

    valid_inflate_num--;
    metaData[63] = (metaData[63] & 0xE0) | (valid_inflate_num & 0x1F);

    // printf("the new metadata is :\n");
    // for (int k = 0; k < 64; k++) {
    //     printf("%02x",static_cast<unsigned>(metaData[k]));

    // }
    // printf("\n");

    Addr originAddr = 0;
    uint64_t sumSize = 0;
    for (uint8_t u = 0; u < index; u++) {
        uint8_t type = getType(metaData, u);
        sumSize += sizeMap[type];
    }
    assert(sumSize <= 4096);
    chunkIdx = sumSize / 512;
    for (int u = 0; u < 4; u++){   // 4B per MPFN
        originAddr = (originAddr << 8) | (metaData[2 + 4 * chunkIdx + u]);
    }
    originAddr = (originAddr << 9) | (sumSize & 0x1FF);

    return originAddr;
}

bool
MemCtrl::updateMetaData(std::vector<uint8_t>& compressed, std::vector<uint8_t>& metaData, uint8_t cacheLineIdx, bool inInflateRoom, MemInterface* mem_intr) {
    DPRINTF(MemCtrl, "Line %d: enter the update Meta data func.  \n", __LINE__);
    // DPRINTF(MemCtrl, "Line %d: the compressed size is %lld \n", __LINE__, compressed.size());
    DPRINTF(MemCtrl, "Line %d: if in inflate room %d \n", __LINE__, inInflateRoom);

    // printf("the old metadata is :\n");
    // for (int k = 0; k < 64; k++) {
    //     printf("%02x",static_cast<unsigned>(metaData[k]));

    // }
    // printf("\n");

    uint8_t type = getType(metaData, cacheLineIdx);

    DPRINTF(MemCtrl, "Line %d: the current process cache line id is %lld, sizeMap[type] is %d\n", __LINE__, cacheLineIdx, sizeMap[type]);

    if (inInflateRoom) {
        /* check if we could write back */
        if (compressed.size() <= sizeMap[type]) {
           DPRINTF(MemCtrl, "underflow, write back to the oirginal space\n");
           Addr originAddr = moveForwardAtomic(metaData, cacheLineIdx, mem_intr);
        } else {
            /* do nothing */
        }
    } else {
        if (compressed.size() <= sizeMap[type]) {
            /* if not overflow */
            /* do nothing */
        } else {
            DPRINTF(MemCtrl, "Line %d: the cacheline overflow \n", __LINE__);
            if (hasFreeInflateRoom(metaData)) {
                Addr inflatedAddr = allocateInflateRoom(metaData, cacheLineIdx);
                // printf("now the new metadata is :\n");\
                // for (int k = 0; k < 64; k++) {
                //     printf("%02x",static_cast<unsigned>(metaData[k]));
                // }
                // printf("\n");
            } else {
                /* deal with page overflow */
               return false;
            }
        }
    }
    // printf("the new metadata is :\n");
    // for (int k = 0; k < 64; k++) {
    //     printf("%02x",static_cast<unsigned>(metaData[k]));

    // }
    // printf("\n");
    return true;
}

void
MemCtrl::recompressAtomic(std::vector<uint8_t>& cacheLine, uint64_t pageNum, uint8_t cacheLineIdx, std::vector<uint8_t>& metaData, MemInterface* mem_intr){
   DPRINTF(MemCtrl, "LINE %d: enter the recompress atomic function\n", __LINE__);
   assert(cacheLine.size() == 64);

   /* step 1 initial a buffer for a uncompressed page */
   std::vector<uint8_t> buffer(4096, 0);

   /* step 2: write the uncompressed data to pageNum */
   for (unsigned int i = 0; i < 64; i++) {
       /* traverse each cacheLine */
       if (i == cacheLineIdx) {
           for (unsigned int j = 0; j < 64; j++) {
               buffer[i * 64 + j] = cacheLine[j];
           }
       } else {
           uint8_t type = getType(metaData, i);
           std::pair<bool, Addr> cLStatus = addressTranslation(metaData, i);


           if (cLStatus.first) {
               mem_intr->atomicRead(buffer.data() + i * 64, cLStatus.second, 64);
           } else {
               if (type == 0) {
                   continue;
               } else if (type == 3) {
                   mem_intr->atomicRead(buffer.data() + i * 64, cLStatus.second, 64);

               } else {
                   assert(type == 1 || type == 2);
                   std::vector<uint8_t> curCL(64, 0);
                   mem_intr->atomicRead(curCL.data(), cLStatus.second, sizeMap[type]);
                   decompressForCompr(curCL);
                   for (unsigned int j = 0; j < 64; j++) {
                       buffer[i * 64 + j] = curCL[j];
                   }
               }

           }
       }
   }

   /* step 3: give back the old chunks */
   uint64_t origin_size = ((metaData[0] & (0x0F)) << 8) | metaData[1];
   uint8_t valid_inflate_num = (metaData[63] & ((0b1 << 5) - 1));  // use last 5 bit to store the counter
   assert(valid_inflate_num <= 16);

   uint64_t curSize = (((origin_size + 0x3F) >> 6) << 6) + valid_inflate_num * 64;

   uint8_t chunkNum = (curSize + 0x1FF) / 512;

   for (unsigned int i = 0; i < chunkNum; i++) {
       Addr chunk_addr = 0;
       for (int u = 0; u < 4; u++){   // 4B per MPFN
           chunk_addr = (chunk_addr << 8) | (metaData[2 + 4 * i + u]);
       }
       chunk_addr <<= 9;
       freeList.push_back(chunk_addr);
       assert(stat_used_bytes >= 512);
       stat_used_bytes -= 512;
   }

   /* step 4: initialize */
   std::vector<uint8_t> newMetaData(64, 0);
   uint64_t size = 0;

   newMetaData[0] = (0b1 << 7);
   for (unsigned int i = 0; i < 64; i++) {
       std::vector<uint8_t> curCL(64, 0);
       for (unsigned int j = 0; j < 64; j++) {
           curCL[j] = buffer[i * 64 + j];
       }

       std::vector<uint8_t> compressedCL = compressForCompr(curCL);

       bool isCompressed = false;
       if (isAllZero(curCL)) {
           /* set the metadata entry to be 0 */
           DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
           setType(newMetaData, i, 0);
       } else {
           if (compressedCL.size() <= 8) {
               /* set the metadata entry to be 0b1*/
               DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
               setType(newMetaData, i, 0b01);
               for (unsigned int v = 0; v < compressedCL.size(); v++) {
                   buffer[size + v] = compressedCL[v];
               }
               size = size + 8;
           } else if (compressedCL.size() <= 32) {
                DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
                setType(newMetaData, i, 0b10);
                for (unsigned int v = 0; v < compressedCL.size(); v++) {
                    buffer[size + v] = compressedCL[v];
                }
               size = size + 32;
           } else {
                assert(compressedCL.size() == 64);
               /* set to be 0b11 */
               DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
               setType(newMetaData, i, 0b11);
               for (unsigned int v = 0; v < 64; v++) {
                   buffer[size + v] = curCL[v];
               }
               size = size + 64;
           }
       }

   }
   uint64_t cur = 0;
   while (cur < size) {
       assert(freeList.size() > 0);
       Addr chunkAddr = freeList.front();
       DPRINTF(MemCtrl, "Line %d, freeList pop\n", __LINE__);
       freeList.pop_front();
       stat_used_bytes += 512;

       int chunkIdx = cur / 512;

       uint64_t MPFN = (chunkAddr >> 9);
       for (int u = 3; u >= 0; u--) {   // 4B per chunk
           uint8_t val = MPFN & 0xFF;
           MPFN >>= 8;
           newMetaData[2 + chunkIdx * 4 + u] = val;
       }
       if (cur + 512 < size) {
           mem_intr->atomicWrite(buffer, chunkAddr, 512, cur);
           cur += 512;
       } else {
           mem_intr->atomicWrite(buffer, chunkAddr, size - cur, cur);
           break;
       }
   }


   // store the size of compressedPage into the control block (using 12 bit)
   newMetaData[1] = size & (0xFF);
   newMetaData[0] = newMetaData[0] | ((size >> 8) & 0xF);

//    printf("the pageNum is %d, the metadata is :\n", pageNum);
//    printf("the metadata is :\n");
//    for (int k = 0; k < 64; k++) {
//        printf("%02x",static_cast<unsigned>(newMetaData[k]));

//    }
//    printf("\n");

   /* write the metadata to mcache and memory */
   Addr metadata_addr = pageNum * 64;
//    printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, pageNum, metadata_addr);
   mcache.add(metadata_addr, newMetaData);
   mem_intr->atomicWrite(newMetaData, metadata_addr, 64, 0);

   return;
}

std::vector<uint8_t>
MemCtrl::recompressTiming(PacketPtr writeForCompress) {
    assert(writeForCompress->getSize() == 4096);
    const Addr base_addr = writeForCompress->getAddr();
    assert((base_addr & 0xFFF) == 0);
    PPN ppn = (base_addr >> 12 & ((1ULL << 52) - 1));

    assert(writeForCompress->comprMetaDataMap.find(ppn) != writeForCompress->comprMetaDataMap.end());
    std::vector<uint8_t> old_metaData = writeForCompress->comprMetaDataMap[ppn];
    std::vector<uint8_t> metaData(64, 0);
    metaData[0] = (0b1 << 7);

    PacketPtr backup_pkt = writeForCompress->comprBackup;
    assert(backup_pkt->getPType() == 0x2);

    Addr backup_addr = backup_pkt->getAddr();
    uint64_t backup_size = backup_pkt->getSize();

    assert(backup_addr >= writeForCompress->getAddr() && (backup_addr + backup_size <= writeForCompress->getAddr() + 4096));

    Addr writeFrom = std::max(backup_addr, writeForCompress->getAddr());
    uint64_t writeSize = backup_size + backup_addr - writeFrom;
    assert(writeSize <= backup_size);
    uint8_t* copy_from = backup_pkt->getPtr<uint8_t>() + writeFrom - backup_addr;

    // uint8_t* mytest_Start = writeForCompress->getPtr<uint8_t>();

    // printf("backup_addr is %lx\n", backup_addr);
    // Addr backup_addr_aligned = (backup_addr >> 6) << 6;
    // uint32_t ofs_My_test = backup_addr_aligned - writeForCompress->getAddr();
    // printf("before write: \n");
    // for (int qw = 0; qw < 64; qw++) {
    //     if (qw % 8 == 0) {
    //         printf("\n");
    //     }
    //     printf("%02x, ", static_cast<unsigned int>(mytest_Start[qw + ofs_My_test]));
    // }
    // printf("\n");

    writeForCompress->setDataForMC(copy_from, writeFrom - writeForCompress->getAddr(), writeSize);

    // printf("after write: \n");
    // for (int qw = 0; qw < 64; qw++) {
    //     if (qw % 8 == 0) {
    //         printf("\n");
    //     }
    //     printf("%02x, ", static_cast<unsigned int>(mytest_Start[qw + ofs_My_test]));

    // }
    // printf("\n");

    uint8_t* page = writeForCompress->getPtr<uint8_t>();
    uint64_t size = 0;

    for (unsigned int i = 0; i < 64; i++) {
        std::vector<uint8_t> curCL(64, 0);
        for (unsigned int j = 0; j < 64; j++) {
            curCL[j] = page[i * 64 + j];
        }

        std::vector<uint8_t> compressedCL = compressForCompr(curCL);

        bool isCompressed = false;
        if (isAllZero(curCL)) {
            /* set the metadata entry to be 0 */
            DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
            setType(metaData, i, 0);
        } else {
            if (compressedCL.size() <= 8) {
                /* set the metadata entry to be 0b1*/
                DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
                setType(metaData, i, 0b01);
                size = size + 8;
            } else if (compressedCL.size() <= 32) {
                DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
                setType(metaData, i, 0b10);
                size = size + 32;
            } else {
                assert(compressedCL.size() == 64);
                /* set to be 0b11 */
                DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
                setType(metaData, i, 0b11);
                size = size + 64;
            }
        }

    }
    /* give back the old pages */
    uint64_t origin_size = ((old_metaData[0] & (0x0F)) << 8) | old_metaData[1];
    uint8_t valid_inflate_num = (old_metaData[63] & ((0b1 << 5) - 1));  // use last 5 bit to store the counter
    assert(valid_inflate_num <= 16);

    uint64_t curSize = (((origin_size + 0x3F) >> 6) << 6) + valid_inflate_num * 64;

    uint8_t chunkNum = (curSize + 0x1FF) / 512;

    for (unsigned int i = 0; i < chunkNum; i++) {
        Addr chunk_addr = 0;
        for (int u = 0; u < 4; u++){   // 4B per MPFN
            chunk_addr = (chunk_addr << 8) | (old_metaData[2 + 4 * i + u]);
        }

        chunk_addr <<= 9;
        freeList.emplace_back(chunk_addr);
        stat_used_bytes -= 512;
    }

    /* allocate new chunks to the page */
    uint64_t cur = 0;
    while (cur < size) {
        assert(freeList.size() > 0);
        Addr chunkAddr = freeList.front();
        DPRINTF(MemCtrl, "Line %d, freeList pop\n", __LINE__);
        freeList.pop_front();

        int chunkIdx = cur / 512;

        uint64_t MPFN = (chunkAddr >> 9);
        for (int u = 3; u >= 0; u--) {   // 4B per chunk
            uint8_t val = MPFN & 0xFF;
            MPFN >>= 8;
            metaData[2 + chunkIdx * 4 + u] = val;
        }
        if (cur + 512 < size) {
            cur += 512;
        } else {
            break;
        }
    }

    stat_used_bytes += size;

    // store the size of compressedPage into the control block (using 12 bit)
    metaData[1] = size & (0xFF);
    metaData[0] = metaData[0] | ((size >> 8) & 0xF);

    return metaData;
}

std::vector<uint8_t>
MemCtrl::compressForCompr(const std::vector<uint8_t>& cacheLine) {
    assert(cacheLine.size() == 64);
    std::pair<uint64_t, std::vector<uint16_t>> transformed = BDXTransform(cacheLine);
    uint64_t base = transformed.first;
    std::vector<uint8_t> compressed = compressC(transformed.second);
    for (int i = 0; i < 4; i++) {
        uint8_t val = base & 0xFF;
        base >>= 8;
        compressed.insert(compressed.begin(),val);
    }
    if (compressed.size() > 32) {
        return cacheLine;
    }
    return compressed;
}

void
MemCtrl::decompressForCompr(std::vector<uint8_t>& data) {
    uint64_t base = 0;
    for (int i = 0; i < 4; i++) {
        base = (base << 8) | data[0];
        data.erase(data.begin());
    }
    // for (int u = 0; u < data.size(); u++) {
    //    if (u % 8 == 0) {
    //        printf("\n");
    //    }
    //    printf("0x%02x, ", static_cast<uint8_t>(data[u]));
    // }

    std::vector<uint16_t> interm = decompressC(data);
    data = BDXRecover(base, interm);
}

std::pair<uint64_t, std::vector<uint16_t>>
MemCtrl::BDXTransform(const std::vector<uint8_t>& origin) {
    assert(origin.size() == 64);
    /* group into 32 bits * 16 */
    std::vector<uint64_t> data(16);
    for (int i = 0; i < 16; i++) {
        data[i] = 0;
        for (int j = 0; j < 4; j++) {
            data[i] = (data[i] << 8) | (origin[i * 4 + j]);
        }
    }

    // step 1: subtract. the 32th bit shows the plus or minus 0(+) 1(-)
    for (int i = 15; i >= 1; i--) {
        if (data[i] < data[i - 1]) {
            data[i] = (1ULL << 32) | (data[i - 1] - data[i]);
        } else {
            data[i] = (data[i] - data[i - 1]);
        }
    }

    // step 2: change orientation;
    // transform the 33 bit * 15 matrix to 15 bit * 33 matrix
    std::vector<uint16_t> DBX(33, 0);
    for (int i = 0; i < 33; i++) {
        for (int j = 1; j < 16; j++) {
            DBX[i] = DBX[i] | (((data[j] >> i) & 0x1) << (j - 1));
        }
    }

    // step 3: XOR the neighbor
    for (int i = 32; i >= 1; i--) {
        DBX[i] = DBX[i] ^ DBX[i - 1];
    }
    uint64_t base = data[0];

    return make_pair(base, DBX);
}

std::vector<uint8_t>
MemCtrl::BDXRecover(const uint64_t& base, std::vector<uint16_t>& DBX) {
    assert(DBX.size() == 33);
    // step 1: XOR
    for (int i = 1; i < 33; i++) {
        DBX[i] = DBX[i] ^ DBX[i - 1];
    }

    // step 2: reverse orientation
    // change the 15 bit * 33 matrix back to 33 bit * 15 matrix
    std::vector<uint64_t> data(16);
    for (int i = 1; i < 16; i++) {
        for (int j = 0; j < 33; j++) {
            uint64_t temp = (DBX[j] >> (i - 1)) & 0x1ULL;
            data[i] = data[i] | (temp << j);
        }
    }

    // step 3: add the value;
    data[0] = base;
    for (int i = 1; i < 16; i++) {
        bool sign = (((data[i] >> 32) & 0x1) == 0);
        if (sign) {
            data[i] = data[i - 1] + (data[i] & ((1ULL << 32) - 1));
        } else {
            data[i] = data[i - 1] - (data[i] & ((1ULL << 32) - 1));
        }
    }

    std::vector<uint8_t> res(64);
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 4; j++) {
            res[i * 4 + j] = (data[i] >> ((3 - j) * 8)) & ((1 << 8) - 1);
        }
    }
    return res;
}

/*
    val: 0b________
    idx: how many bits that already used
    len: the code length
    code: the corresponding pattern

*/

void
MemCtrl::supply(uint16_t code, int len, uint8_t& val, int& idx, std::vector<uint8_t>& compressed) {
    assert((code & (~((1 << len) - 1))) == 0);
    while (len) {
        if (idx + len > 8) {
            int front = 8 - idx;
            len -= front;
            val = val | (code >> len);
            compressed.emplace_back(val);
            idx = 0;
            val = 0;
        } else {
            val = val | (code << (8 - len - idx));
            idx += len;
            if (idx == 8) {
                compressed.emplace_back(val);
                val = 0;
                idx = 0;
            }
            break;
        }
    }

}

std::vector<uint8_t>
MemCtrl::compressC(const std::vector<uint16_t>& inputData) {
    assert(inputData.size() == 33);
    int idx = 0;
    int cnt = 0;
    uint8_t val = 0;
    std::vector<uint8_t> compressed;
    for (int i = 0; i < 33; i++) {;
        assert(inputData[i] < (1 << 15));
        if (inputData[i] == 0) {
            cnt++;
        } else {
            if (cnt != 0) {
                if (cnt == 1) {
                    uint16_t code = 0b001;
                    supply(code, 3, val, idx, compressed);
                } else {
                    uint16_t code = (0b1 << 5) | (cnt - 2);
                    supply(code, 7, val, idx, compressed);
                }
                cnt = 0;
            }
            // try to match the pattern
            if (inputData[i] == ((1 << 15) - 1)){
                uint16_t code = 0b0;
                supply(code, 5, val, idx, compressed);

            } else {
                if (i > 0 && inputData[i] == inputData[i-1]) {
                    uint16_t code = 0b1;
                    supply(code, 5, val, idx, compressed);
                } else {
                    bool consecutive = false;
                    int startPos = -1;
                    int oneCnt = 0;
                    int oneIdx = 0;
                    uint16_t cur = inputData[i];
                    uint16_t mask = (1 << 15);
                    while ((mask != 0) && ((~mask) != 0)) {
                        if ((cur & mask) != 0) {
                            if (startPos == -1) {
                                startPos = oneIdx;
                            } else if (startPos + 1 == oneIdx) {
                                consecutive = true;
                            }
                            oneCnt++;
                        }
                        mask >>= 1;
                        oneIdx++;
                    }
                    if (oneCnt == 1) {
                        assert(startPos >= 0);
                        uint16_t code = (0b11 << 4) | (startPos);
                        supply(code, 9, val, idx, compressed);
                    } else if (consecutive && oneCnt == 2) {
                        assert(startPos >= 0);
                        uint16_t code = (0b10 << 4) | (startPos);
                        supply(code, 9, val, idx, compressed);
                    } else {
                        uint16_t code = (0b1 << 15) | inputData[i];
                        supply(code, 16, val, idx, compressed);
                    }

                }
            }
        }
    }
    if (cnt != 0) {
        if (cnt == 1) {
            uint16_t code = 0b001;
            supply(code, 3, val, idx, compressed);
        } else {
            uint16_t code = (0b1 << 5) | (cnt - 2);
            supply(code, 7, val, idx, compressed);
        }
    }
    if (idx != 0) {
        compressed.emplace_back(val);
    }
    return compressed;
}


/*
    start from the compresseData[idx].ofs, retrieve the len length string of bits from the compresseData
    compresseData[idx]: 0b_ _ _ _ _ _ _ _ 0b_ _ _ _ _ _ _ _
                          0 1 2 3 4 5 6 7   0'1'2'3'4'5'6'7'
    update the ofs and idx meanwhile
*/
uint16_t
MemCtrl::dismantle(int len, int& idx, int& ofs, const std::vector<uint8_t>& compresseData) {
    uint16_t val = 0;
    if (len == 16) {
        ofs++;
        if (ofs >= 8) {
            ofs -= 8;
            idx++;
        }
        len--;
        assert(idx < compresseData.size());
        while(len) {
            if((len + ofs) > 8) {
                int partLen = 8 - ofs;
                len -= partLen;
                val = val | ((compresseData[idx] & ((1 << partLen) - 1)) << len);
                idx++;
                ofs = 0;
            } else {
                ofs += len;
                val = val | ((compresseData[idx] >> (8 - ofs)) & ((1 << len) - 1));
                if (ofs >= 8) {
                    ofs = 0;
                    idx++;
                }
                break;
            }
        }
    } else if (len == 7) {
        ofs += 2;
        if (ofs >= 8) {
            ofs -= 8;
            idx++;
        }
        len -= 2;
        if ((len + ofs) > 8) {
            int partLen = 8 - ofs;
            len -= partLen;
            val = val | ((compresseData[idx] & ((1 << partLen) - 1)) << len);
            idx++;
            ofs = len;
            /* use the [0, len) for the rest part */
            val = val | ((compresseData[idx] >> (8 - len)) & ((1 << len) - 1));
        } else {
            ofs += len;
            val = (compresseData[idx] >> (8 - ofs)) & ((1 << len) - 1);
            if (ofs >= 8) {
                ofs -= 8;
                idx++;
            }
        }
        val = val + 2;
    } else if (len == 3) {
        val = 0;
        ofs += 3;
        if (ofs >= 8) {
            ofs -= 8;
            idx++;
        }
    } else if (len == 5) {
        ofs += 4;
        if (ofs >= 8) {
            ofs -= 8;
            idx++;
        }
        bool isAllOne = ((compresseData[idx] >> (7 - ofs) & 0x1) == 0x0);
        if (isAllOne) {
            val = (1 << 15) - 1;
        } else {
            val = 0;
        }
        ofs++;
        if (ofs >= 8) {
            ofs -= 8;
            idx++;
        }
    } else {
        assert(len == 9);
        ofs += 4;
        if (ofs >= 8) {
            ofs -= 8;
            idx++;
        }
        bool isConsecutive = ((compresseData[idx] >> (7 - ofs) & 0x1) == 0x0);
        uint8_t startPos = 0;
        ofs++;
        if (ofs >= 8) {
            ofs -= 8;
            idx++;
        }
        len = 4;
        if (ofs + len > 8) {
            int partLen = 8 - ofs;
            len -= partLen;

            startPos = startPos | ((compresseData[idx] & ((1 << partLen) - 1)) << len);
            idx++;
            ofs = len;
            /* use the [0, len) for the rest part */
            startPos = startPos | ((compresseData[idx] >> (8 - len)) & ((1 << len) - 1));
        } else {
            ofs += len;
            startPos = (compresseData[idx] >> (8 - ofs)) & ((1 << len) - 1);
            if (ofs >= 8) {
                ofs -= 8;
                idx++;
            }
        }
        if (isConsecutive) {
            assert(startPos < 15);
            val = 0b11 << (14 - startPos);
        } else {
            val = 0b1 << (15 - startPos);
        }
    }
    return val;
}

bool
MemCtrl::checkStatus(const int& idx, const int& ofs, const std::vector<uint8_t>& compresseData) {
    uint8_t sign = (compresseData[idx] >> (7 - ofs)) & 0x1;
    return (sign == 0x1);
}

void
MemCtrl::interpret(int& idx, int& ofs, const std::vector<uint8_t>& compresseData, std::vector<uint16_t>& decompressed) {
    bool isCompressed = checkStatus(idx, ofs, compresseData);
    if (isCompressed) {
        uint16_t val = dismantle(16, idx, ofs, compresseData);
        decompressed.emplace_back(val);
    } else {
        int curIdx = idx;
        int curOfs = ofs + 1;
        if (curOfs >= 8) {
            curOfs = 0;
            curIdx++;
        }
        if (checkStatus(curIdx, curOfs, compresseData)){
            uint16_t runLength = dismantle(7, idx, ofs, compresseData);
            for (int i = 0; i < runLength; i++) {
                decompressed.emplace_back(0);
            }
        } else {
            curOfs++;
            if (curOfs >= 8) {
                curOfs = 0;
                curIdx++;
            }
            if (checkStatus(curIdx, curOfs, compresseData)) {
                // cout << "decompress type B" << endl;
                uint16_t val = dismantle(3, idx, ofs, compresseData);
                assert(val == 0);
                decompressed.emplace_back(val);
            } else {
                curOfs++;
                if (curOfs >= 8) {
                    curOfs = 0;
                    curIdx++;
                }
                if (checkStatus(curIdx, curOfs, compresseData)) {
                    uint16_t val = dismantle(9, idx, ofs, compresseData);
                    decompressed.emplace_back(val);
                } else {
                    uint16_t val = dismantle(5, idx, ofs, compresseData);
                    if (val == 0) {
                        assert(decompressed.size() != 0);
                        val = decompressed.back();
                    } else {
                        assert(val == ((1 << 15) - 1));
                    }
                    decompressed.emplace_back(val);
                }
            }
        }

    }
}

std::vector<uint16_t>
MemCtrl::decompressC(const std::vector<uint8_t>& compresseData){
    std::vector<uint16_t> decompressed;
    int idx = 0;
    int ofs = 0;
    while (decompressed.size() < 33) {
        interpret(idx, ofs, compresseData, decompressed);
   }
//    printf("the size of decompressed is %ld\n", decompressed.size());
   fflush(stdout);
   assert(decompressed.size() == 33);
   return decompressed;
}


void
MemCtrl::setInflateEntry(uint8_t index, std::vector<uint8_t>& metaData, uint8_t cacheLineIdx){
    assert(index < 17);

    DPRINTF(MemCtrl, "You enter the setInflateEntry\n");

    // printf("the original metadata is :\n");
    // for (int k = 0; k < 64; k++) {
    //     printf("%02x",static_cast<unsigned>(metaData[k]));

    // }
    // printf("\n");

    uint8_t iIdx = ((index * 6) / 8) + 50;
    uint8_t iLoc = (index * 6) % 8;

    if (iLoc > 2) {
        uint8_t prefix = 8 - iLoc;
        uint8_t suffix = 6 - prefix;
        /* zero out the bits */
        metaData[iIdx] = metaData[iIdx] & (0xFF << prefix);
        /* update */
        uint8_t mask = (1 << prefix) - 1;
        metaData[iIdx] = metaData[iIdx] | ((cacheLineIdx >> suffix) & mask);
        assert(iIdx < 63);
        metaData[iIdx + 1] &= ((1 << (8 - suffix)) - 1);
        metaData[iIdx + 1] = metaData[iIdx + 1] | ((cacheLineIdx & ((1 << suffix) - 1)) << (8 - suffix));
    } else {
        uint8_t zeroMask = ~(0x3F << (2 - iLoc));
        metaData[iIdx] = metaData[iIdx] & zeroMask;
        metaData[iIdx] = metaData[iIdx] | ((cacheLineIdx & 0x3F) << (2 - iLoc));
    }


    // printf("the new/w metadata is :\n");
    // for (int k = 0; k < 64; k++) {
    //     printf("%02x",static_cast<unsigned>(metaData[k]));

    // }
    // printf("\n");
}

uint8_t
MemCtrl::getInflateEntry(uint8_t index, const std::vector<uint8_t>& metaData) {
    uint8_t cacheLineIdx = 0;

    uint8_t iIdx = ((index * 6) / 8) + 50;
    uint8_t iLoc = (index * 6) % 8;

    if (iLoc > 2) {
        int prefix = 8 - iLoc;
        int suffix = 6 - prefix;
        cacheLineIdx = (metaData[iIdx] & ((1 << prefix) - 1)) << suffix;
        cacheLineIdx = cacheLineIdx | ((metaData[iIdx + 1] >> (8 - suffix)) & ((1 << suffix) - 1));
    } else {
        cacheLineIdx = (metaData[iIdx] >> (2 - iLoc)) & 0x3F;
    }

    return cacheLineIdx;
}

bool
MemCtrl::isEligible(PacketPtr pkt, bool ignoreRead) {
    if (curTick() - recvLastPkt >= 5000000000) {
        printf("the size of waitQueue is %d\n", waitQueue.size());
        printf("the current pkt is 0x%lx\n", reinterpret_cast<unsigned long>(pkt));
        panic("check isEligible should not be this long!");
    }
    auto targetPkt = waitQueue.find(pkt);
    assert(targetPkt != waitQueue.end());

    bool res = true;
    for (auto it = waitQueue.begin(); it != targetPkt; it++) {
        PacketPtr curPkt = *it;
        if (hasOverLap(curPkt->comprMetaDataMap, pkt->comprMetaDataMap)) {
            res = false;
            break;
        }
    }
    return res;
}


bool
MemCtrl::hasOverLap(std::unordered_map<uint64_t, std::vector<uint8_t>>& map1, std::unordered_map<uint64_t, std::vector<uint8_t>>& map2) {
    for (const auto& pair : map1) {
        if (map2.find(pair.first) != map2.end()) {
            return true;
        }
    }
    return false;
}

void
MemCtrl::updateSubseqMetaData(PacketPtr pkt, PPN ppn) {

    std::set<PacketPtr>::iterator it = waitQueue.find(pkt);
    DPRINTF(MemCtrl, "the address of pkt is %lx\n", pkt);
    assert(it != waitQueue.end());
    it++;
    assert(pkt->comprMetaDataMap.find(ppn) != pkt->comprMetaDataMap.end());
    std::vector<uint8_t> metaData = pkt->comprMetaDataMap[ppn];

    for (; it != waitQueue.end(); it++) {
        PacketPtr curPkt = *it;
        if (curPkt->comprMetaDataMap.find(ppn) != curPkt->comprMetaDataMap.end()) {
            curPkt->comprMetaDataMap[ppn] = metaData;
        }
    }
    // for (const auto& pkt: to_remove) {
    //     waitQueue.erase(pkt);
    //     pkt->comprMetaDataMap[ppn] = metaData;
    //     waitQueue.insert(pkt);
    // }
}

void
MemCtrl::checkForReadyPkt(){
    recordForCheckReady = true;
    for (const auto& wait_pkt: waitQueue) {
        if (wait_pkt->comprIsProc == false && wait_pkt->comprIsReady) {
            assignToQueue(wait_pkt);     // during this calling, wait_pkt might be finished and new pkt might need to be added
        }
    }

    if (to_delete_pkt.size() != 0) {
        for (const auto& pkt: to_delete_pkt) {
            assert(waitQueue.find(pkt) != waitQueue.end());
            waitQueue.erase(pkt);
            assert(pkt->ref_cnt > 0);
            pkt->ref_cnt--;
            uint32_t pType = pkt->getPType();
            if (pType == 0x2) {
                if (pkt->ref_cnt == 0) {
                    delete pkt;
                }
            } else if (pType == 0x4) {
                /* read metadata */
                panic("should not see this type of pkt in waitQueue");
            } else if (pType == 0x8) {
                /* write metadata */
                panic("should not see this type of pkt in waitQueue");
            } else if (pType == 0x10) {
                if (pkt->ref_cnt == 0) {
                    assert(pkt->comprBackup->ref_cnt > 1);
                    pkt->comprBackup->ref_cnt--;
                    delete pkt;
                }
            } else if (pType == 0x20) {
                PacketPtr backup_pkt = pkt->comprBackup;
                 if (backup_pkt->getPType() == 0x4) {
                    if (pkt->ref_cnt == 0) {
                        assert(backup_pkt->ref_cnt > 0);
                        backup_pkt->ref_cnt--;
                        if (backup_pkt->ref_cnt == 0) {
                            assert(backup_pkt->comprBackup->ref_cnt > 0);
                            backup_pkt->comprBackup->ref_cnt--;
                            if (backup_pkt->comprBackup->ref_cnt == 0) {
                                delete backup_pkt->comprBackup;
                            }
                            delete backup_pkt;
                        }
                        delete pkt;
                    }
                 } else {
                    assert(backup_pkt->getPType() == 0x2);
                    if (pkt->ref_cnt == 0) {
                        assert(backup_pkt->ref_cnt > 0);
                        backup_pkt->ref_cnt--;
                        if (backup_pkt->ref_cnt == 0) {
                            delete backup_pkt;
                        }
                        delete pkt;
                    }
                 }
            }
        }
        for (const auto& pkt: to_add_pkt) {
            waitQueue.insert(pkt);
            pkt->ref_cnt++;
        }
        to_delete_pkt.clear();
        to_add_pkt.clear();
        checkForReadyPkt();
    } else {
        assert(to_add_pkt.size() == 0);
        recordForCheckReady = false;
        return;
    }
}

void
MemCtrl::prepareMetaData(PacketPtr pkt){
    unsigned size = pkt->getSize();
    uint32_t burst_size = dram->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    assert(pkt->getPType() == 0x2);  // the pkt type should be auxiliary
    uint64_t pageCnt = 0;
    uint64_t entryCnt = 0;

    Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;

    for (unsigned int i = 0 ; i < pkt_count; i++) {
        PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

        // printf("iterate the pkt, check if metadata for ppn is available %d\n", ppn);

        if (pkt->comprMetaDataMap.find(ppn) == pkt->comprMetaDataMap.end()) {
            // printf("not found in metadata map\n");
            pageCnt++;
            /* step 1.1: calculate the MPA for metadata */
            Addr memory_addr = ppn * 64;

            std::vector<uint8_t> metaData(64, 0);

            if (pageNum == ppn) {
                /* hit in the page buffer */
                dram->atomicRead(metaData.data(), ppn * 64, 64);
                pkt->comprMetaDataMap[ppn] = metaData;
                entryCnt++;
            } else {
                if (mcache.isExist(memory_addr)) {
                    /* hit in the metadata cache */
                    pkt->comprMetaDataMap[ppn] = mcache.find(memory_addr);
                    // printf("the metadata is :\n");
                    // for (int k = 0; k < 64; k++) {
                    //     printf("%02x",static_cast<unsigned>(pkt->comprMetaDataMap[ppn][k]));

                    // }
                    // printf("\n");

                    entryCnt++;
                } else {
                    // create a readMetaData pkt and add to read queue
                    // printf("have to create a readMetaData \n");
                    PacketPtr readMetaData = new Packet(pkt);
                    pkt->ref_cnt++;
                    DPRINTF(MemCtrl, "(pkt) Line %d, the readMetaData pkt %lx\n", __LINE__, readMetaData);

                    readMetaData->configAsReadM(memory_addr);
                    if (!addToReadQueueForCompr(readMetaData, 1, dram)) {
                        // If we are not already scheduled to get a request out of the
                        // queue, do so now
                        if (!nextReqEvent.scheduled()) {
                            DPRINTF(MemCtrl, "Request scheduled immediately\n");
                            schedule(nextReqEvent, curTick());
                        }
                    }
                    pkt->comprMetaDataMap[ppn] = metaData;  /* right now just set the metadata to zero */
                }
            }
        }
        // Starting address of next memory pkt (aligned to burst boundary)
        addr = (addr | (burst_size - 1)) + 1;
    }
    assert(pageCnt == pkt->comprMetaDataMap.size());

    if (pageCnt == entryCnt) {
        /* auxPkt is ready for process */
        pkt->comprIsReady = true;
        assignToQueue(pkt, true);
    } else {
        pkt->setEntryCnt(entryCnt);
    }

}

void
MemCtrl::assignToQueue(PacketPtr pkt, bool recordStats) {
    if (!isEligible(pkt)) {
        return;
    }
    assert(pkt->comprIsProc == false);
    pkt->comprIsProc = true;
    unsigned size = pkt->getSize();
    uint32_t burst_size = dram->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    if (pkt->isWrite()) {
        DPRINTF(MemCtrl, "Line %d, enter the addToWriteQueueForCompr function\n", __LINE__);
        if (addToWriteQueueForCompr(pkt, pkt_count, dram)) {
            // If we are not already scheduled to get a request out of the
            // queue, do so now
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Request scheduled immediately\n");
                schedule(nextReqEvent, curTick());
            }
        }
        if (recordStats) {
            stats.writeReqs++;
            stats.bytesWrittenSys += size;
        }
    } else {
        assert(pkt->isRead());
        DPRINTF(MemCtrl, "Line %d, enter the addToReadQueueForCompr function\n", __LINE__);
        if (!addToReadQueueForCompr(pkt, pkt_count, dram)) {
            // If we are not already scheduled to get a request out of the
            // queue, do so now
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Request scheduled immediately\n");
                schedule(nextReqEvent, curTick());
            }
        }
        if (recordStats) {
            stats.readReqs++;
            stats.bytesReadSys += size;
        }
    }
}

/* ==== end for compresso ====*/

/* ==== start for DyLeCT ==== */
void
MemCtrl::afterDecompForDyL(PacketPtr pkt, MemInterface* mem_intr) {

    /* find a location for the page*/
    Addr newAddr = freeList.front();
    freeList.pop_front();
    stat_used_bytes += 4096;

    if (coverageTestMC(newAddr, 0, 4096)) {
        printf("allocate the new address 0x%lx\n", newAddr);
        printf("the page is %ld\n", (pkt->getAddr() >> 12));
        if (findSameElem(newAddr)) {
            panic("duplicated!!!!!!!!");
        }
    }

    uint32_t burst_size = dram->bytesPerBurst();

    PPN ppn = (pkt->getAddr()) >> 12;
    Addr cteAddr = startAddrForCTE + ppn * 8;
    Addr cteAddrAligned = (cteAddr >> 6) << 6;
    uint8_t loc = (cteAddr >> 3) & ((1 << 3) - 1);


    assert(decompressedPage.find(pkt) != decompressedPage.end());
    std::vector<uint8_t> dPage = decompressedPage[pkt];
    decompressedPage.erase(pkt);

    /* update the CTE and cache if necessary */
    /* step 1: generate a write packet for update the CTE */
    PacketPtr writeCTE = new Packet(pkt);
    // printf("create new pkt for writeCTE 2 0x%lx\n", writeCTE);
    // DPRINTF(MemCtrl, "Line %d: create a new packet for writeCTE, the address is 0x%llx\n", __LINE__, (uint64_t)writeCTE);

    writeCTE->configAsWriteCTE(cteAddr, pkt, 8);
    uint8_t* dataPtr = writeCTE->getPtr<uint8_t>();
    uint64_t newCTE = (1ULL << 63) | (0ULL << 62) | (((newAddr >> 12) & ((1ULL << 30) - 1)) << 32);
    if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {
        printf("readForDecompress: the new cte for page %d is 0x%lx\n", ppn, newCTE);
    }

    for (int i = 7; i >= 0; i--){
        dataPtr[i] = newCTE & 0xFF;
        newCTE = newCTE >> 8;
    }
    /* update the cache if necessary */
    if (mcache.isExist(cteAddrAligned)) {
        std::vector<uint8_t> cacheLine = mcache.find(cteAddrAligned);
        for (unsigned int i = 0; i < 8; i++) {
            cacheLine[loc * 8 + i] = dataPtr[i];
        }
        // printf("Line %d, update the cache\n", __LINE__);
        // printf("the align address is 0x%lx\n", cteAddrAligned);
        // printf("the loc is %d\n", loc);
        // for (int qw = 0; qw < 8; qw++) {
        //     uint64_t forTest = 0;
        //     for (int zx = 0; zx < 8; zx++) {
        //         forTest = (forTest << 8) | (cacheLine[qw *8 + zx] & 0xFF);
        //     }
        //     printf("cte is 0x%lx\n", forTest);
        // }
        mcache.updateIfExist(cteAddrAligned, cacheLine);
    }
    unsigned wcte_offset = writeCTE->getAddr() & (burst_size - 1);
    unsigned int wcte_pkt_count = divCeil(wcte_offset + writeCTE->getSize(), burst_size);
    assert(wcte_pkt_count == 1);
    addToWriteQueueForDyL(writeCTE, wcte_pkt_count, mem_intr);

    /* write back the uncompressed page to memory */
    PacketPtr writeUncompress = new Packet(pkt);
    // printf("create new pkt for writeUncompress 0x%lx\n", writeUncompress);

    // DPRINTF(MemCtrl, "Line %d: create a new packet for write Uncompress, the address is 0x%llx\n", __LINE__, (uint64_t)writeUncompress);
    writeUncompress->configAsWriteUncompress(newAddr, pkt, dPage);

    if (isAddressCovered(pkt->getAddr(), 0, 1)) {
        printf("the new address is 0x%lx\n", newAddr);
        printf("create a new pkt for writeUncompress: the address of pkt is 0x%lx\n", reinterpret_cast<unsigned long>(writeUncompress));
        printf("the original pkt address is 0x%lx\n", reinterpret_cast<unsigned long>(pkt));
    }
    if (coverageTestMC(newAddr, 0, 4096)) {
        printf("the new address is 0x%lx\n", newAddr);
        printf("create a new pkt for writeUncompress: the address of pkt is 0x%lx\n", reinterpret_cast<unsigned long>(writeUncompress));
        printf("the original pkt address is 0x%lx\n", reinterpret_cast<unsigned long>(pkt));
    }

    unsigned wuc_offset = writeUncompress->getAddr() & (burst_size - 1);
    unsigned int wuc_pkt_count = divCeil(wuc_offset + 4096, burst_size);
    addToWriteQueueForDyL(writeUncompress, wuc_pkt_count, mem_intr);

    if (!nextReqEvent.scheduled()) {
        DPRINTF(MemCtrl, "Line %d: Request scheduled immediately\n", __LINE__);
        schedule(nextReqEvent, curTick());
    }
}


bool
MemCtrl::findSameElem(Addr addr) {
    for (auto const& ca: freeList) {
        if (ca == addr) {
            return true;
        }
    }
    return false;
}

/* ==== end for DyLeCT ==== */

/* ==== start for new architecture ==== */

uint8_t
MemCtrl::new_getCoverage(const std::vector<uint8_t>& metaData) {
    uint8_t cover = metaData[0] & 0x7F;
    return cover;
}


/*
 * according to the index (1 - 9), allocate the free block and update the metaData
*/
void
MemCtrl::new_allocateBlock(std::vector<uint8_t>& metaData, uint8_t index) {
    assert(index >= 1 && index <= 9);
    uint64_t block_size = pageSizeMap[index] - pageSizeMap[index - 1];
    uint64_t block_id = 0;
    if (block_size == 512) {
        if (fineGrainedFreeList.size() == 0) {
            Addr chunk_addr = freeList.front();
            freeList.pop_front();
            block_id = chunk_addr >> 9;
            fineGrainedFreeList.push_back(chunk_addr + 512);
        } else {
            block_id = (fineGrainedFreeList.front() >> 9);
            fineGrainedFreeList.pop_front();
        }
        stat_used_bytes += 512;
        compress_used += 512;
    } else if (block_size == 1024) {
        block_id = (freeList.front() >> 9);
        freeList.pop_front();
        stat_used_bytes += 1024;
        compress_used += 1024;
    } else {
        panic("invalid block size");
    }

    for (int m = 3; m >= 0; m--) {
        metaData[4 * index + m] = block_id & 0xFF;
        block_id >>= 8;
    }
}

/*
    Givem the metadata and the target cacheLineIdx, return a vector containing three elements
    res[0]: the starting address of the original space allocated for this cacheline
    res[1] (only valid for those cachelines that across two blocks): the starting address of the second part
    res[2]: the size of the second part
*/

std::vector<uint64_t>
MemCtrl::new_addressTranslation(const std::vector<uint8_t>& metaData, uint8_t cachelineIdx) {
    // printf("address Translation, the metaData is: \n");
    // for (int k = 0; k < 64; k++) {
    //     printf("%02x",static_cast<unsigned>(metaData[k]));
    // }
    // printf("\n");
    std::vector<uint64_t> res(3, 0);
    assert(metaData.size() == 64);
    assert(new_getCoverage(metaData) > cachelineIdx);

    uint64_t startLoc = 0;

    for (uint8_t u = 0; u < cachelineIdx; u++) {
        uint8_t type = new_getType(metaData, u);
        startLoc += sizeMap[(type & 0b11)];
    }
    assert(startLoc < pageSizeMap[9]);

    Addr addr = 0;
    auto it = std::upper_bound(pageSizeMap.begin(), pageSizeMap.end(), startLoc);
    uint64_t chunkIdx = it - pageSizeMap.begin();
    assert(chunkIdx >= 1);
    // printf("chunkIdx is %d\n", chunkIdx);

    for (int u = 0; u < 4; u++){   // 4B per MPFN
        addr = (addr << 8) | (metaData[4 * chunkIdx + u]);
    }

    // printf("the aligned adddress is 0x%lx\n", addr);

    uint64_t leftover = startLoc - pageSizeMap[chunkIdx - 1];

    uint64_t chunkSize = pageSizeMap[chunkIdx] - pageSizeMap[chunkIdx - 1];
    assert(leftover < chunkSize);
    if (chunkSize == 512) {
        addr = (addr << 9) | (leftover & 0x1FF);
    } else {
        assert(chunkSize == 1024);
        addr = (addr << 9) | (leftover & 0x3FF);
    }

    res[0] = addr;
    uint8_t cur_type = new_getType(metaData, cachelineIdx);
    if (startLoc + sizeMap[cur_type & 0b11] > pageSizeMap[chunkIdx]) {
        assert(chunkIdx < 9);
        uint64_t suffixLen = startLoc + sizeMap[cur_type & 0b11] - pageSizeMap[chunkIdx];
        Addr new_block_addr = 0;
        for (int u = 0; u < 4; u++){   // 4B per MPFN
            new_block_addr = (new_block_addr << 8) | (metaData[4 * chunkIdx + 4 + u]);
        }
        res[1] = new_block_addr << 9;
        res[2] = suffixLen;
    }

    return res;
}

Addr
MemCtrl::calOverflowAddr(const std::vector<uint8_t>& metaData, uint8_t overflowIdx) {
    uint64_t offset = metaData[2] * 64 + overflowIdx * 64;
    assert(offset < pageSizeMap[9]);
    Addr addr = 0;
    auto it = std::upper_bound(pageSizeMap.begin(), pageSizeMap.end(), offset);
    uint64_t chunkIdx = it - pageSizeMap.begin();
    assert(chunkIdx >= 1);

    for (int u = 0; u < 4; u++){   // 4B per MPFN
        assert((4 * chunkIdx + u) < 64);
        addr = (addr << 8) | (metaData[4 * chunkIdx + u]);
    }

    uint64_t leftover = offset - pageSizeMap[chunkIdx - 1];
    uint64_t chunkSize = pageSizeMap[chunkIdx] - pageSizeMap[chunkIdx - 1];
    assert(leftover < chunkSize);
    assert(offset + 64 <= pageSizeMap[chunkIdx]);
    if (chunkSize == 512) {
        addr = (addr << 9) | (leftover & 0x1FF);
    } else {
        assert(chunkSize == 1024);
        addr = (addr << 9) | (leftover & 0x3FF);
    }
    return addr;
}

void
MemCtrl::new_restoreData(std::vector<uint8_t>& cacheLine, uint8_t type) {
    if (type >= 0b011) {
        return;
    } else if (type == 0b000) {
        for (int i = 0; i < cacheLine.size(); i++) {
            cacheLine[i] = 0;
        }
    } else {
        decompressForNew(cacheLine);
    }
}

void
MemCtrl::decompressForNew(std::vector<uint8_t>& data) {
    uint64_t base = 0;
    for (int i = 0; i < 4; i++) {
        base = (base << 8) | data[0];
        data.erase(data.begin());
    }

    std::vector<uint16_t> interm = decompressC(data);
    data = BDXRecover(base, interm);
}

std::vector<uint8_t>
MemCtrl::compressForNew(const std::vector<uint8_t>& cacheLine) {
    assert(cacheLine.size() == 64);
    if (isAllZero(cacheLine)) {
        std::vector<uint8_t> oneByte = {0};
        return oneByte;
    }
    std::pair<uint64_t, std::vector<uint16_t>> transformed = BDXTransform(cacheLine);
    uint64_t base = transformed.first;
    std::vector<uint8_t> compressed = compressC(transformed.second);
    for (int i = 0; i < 4; i++) {
        uint8_t val = base & 0xFF;
        base >>= 8;
        compressed.insert(compressed.begin(),val);
    }
    if (compressed.size() > 44) {
        return cacheLine;
    }
    return compressed;
}

void
MemCtrl::new_updateMetaData(const std::vector<uint8_t>& cacheLine, std::vector<uint8_t>& metaData, uint8_t cacheLineIdx, MemInterface* mem_intr) {
    assert(cacheLine.size() == 64);
    /* compress the data */
    std::vector<uint8_t> compressed_data = compressForNew(cacheLine);

    /* get the so-far coverage of metadata */
    uint8_t coverage = new_getCoverage(metaData);
    uint8_t type = new_getType(metaData, cacheLineIdx);

    if (cacheLineIdx >= coverage) {
        /* if the coverage of metadata doesn't contain the current cacheline */
        assert(type == 0);
        if (isAllZero(cacheLine)) {
            /* do nothing */
        } else {
            if (compressed_data.size() <= 22) {
                new_setType(metaData, cacheLineIdx, 0b001);
            } else if (compressed_data.size() <= 44) {
                new_setType(metaData, cacheLineIdx, 0b010);
            } else {
                new_setType(metaData, cacheLineIdx, 0b011);
            }
        }
        uint64_t sum_size = 0;
        for (int i = 0; i <= cacheLineIdx; i++) {
            sum_size += sizeMap[(new_getType(metaData, i) & 0b11)];
        }

        if (cacheLineIdx == 63) {
            uint64_t startOverflowRegion = ((sum_size + 63) >> 6) << 6;
            assert(startOverflowRegion >= sum_size);
            metaData[2] = (startOverflowRegion / 64);
        }

        uint8_t block_num = metaData[1];
        assert(block_num >= 1);

        // printf("coverage not satisfy, the sum_size is %d\n", sum_size);

        if (sum_size > pageSizeMap[block_num]) {
            assert(block_num < 9);
            assert(sum_size <= pageSizeMap[block_num + 1]);
            new_allocateBlock(metaData, block_num + 1);
            metaData[1] = block_num + 1;
        }
        /* update the coverage to the new boundary */
        // printf("the old metaData[0] is %d\n", static_cast<unsigned int>(metaData[0]));
        metaData[0] = 0x80 | ((cacheLineIdx + 1) & 0x7F);
        // printf("the old metaData[0] is %d\n", static_cast<unsigned int>(metaData[0]));

    } else {
        if (type < 0b100 && compressed_data.size() > sizeMap[type]) {
            /* overflow */
            // printf("overflow, the cacheLineIdx is %d\n", static_cast<uint8_t>(cacheLineIdx));
            // printf("coverage: %d\n", coverage);
            if (coverage < 64) {
                metaData[0] = 0xC0;

                uint64_t sum_size = 0;
                for (int i = 0; i < 64; i++) {
                    uint8_t cur_type = new_getType(metaData, i);
                    sum_size += sizeMap[(cur_type & 0b11)];
                    // printf("index %d, size is %d, sum_size is %ld\n", i, static_cast<unsigned int>(sizeMap[type & 0b11]), sum_size);
                }

                uint64_t startOverflowRegion = ((sum_size + 63) >> 6) << 6;
                assert(startOverflowRegion >= sum_size);
                metaData[2] = (startOverflowRegion / 64);

                assert(metaData[3] == 0);
            }
            uint8_t new_type = type | 0b100;

            uint8_t overflow_num = metaData[3];
            assert(overflow_num < 64);

            new_setType(metaData, cacheLineIdx, new_type);

            std::vector<uint64_t> translationRes = new_addressTranslation(metaData, cacheLineIdx);
            std::vector<uint8_t> sign = {overflow_num};

            // printf("write overflow num is %d, address is 0x%lx\n", static_cast<unsigned int>(sign[0]), translationRes[0]);
            mem_intr->atomicWrite(sign, translationRes[0], 1);

            overflow_num++;
            metaData[3] = overflow_num;

            uint8_t block_num = metaData[1];
            assert(block_num >= 1);

            uint64_t page_size = metaData[2] * 64 + overflow_num * 64;
            // printf("the metaData[2] is %d\n", metaData[2]);
            // printf("the overflow_num is %d\n", overflow_num);
            // printf("pageSizeMap[block_num] is %ld\n", pageSizeMap[block_num]);
            // fflush(stdout);


            if (page_size > pageSizeMap[block_num]) {
                assert(block_num < 9);
                assert(page_size <= pageSizeMap[block_num + 1]);
                new_allocateBlock(metaData, block_num + 1);
                metaData[1] = block_num + 1;
            }
        }

    }
}

void
MemCtrl::prepareMetaDataForNew(PacketPtr pkt, MemInterface* mem_intr) {
    // printf("enter the prepareMetaDataForNew\n");

    unsigned size = pkt->getSize();
    uint32_t burst_size = dram->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    assert(pkt_count == 1);
    assert(pkt->newPType == 0x4);   // pkt is subPkt

    Addr addr = pkt->getAddr();

    assert(addr == pkt->new_origin);

    PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));

    /* step 1.1: calculate the MPA for metadata */
    Addr memory_addr = ppn * 64;
    std::vector<uint8_t> metaData(64, 0);

    if (mcache.isExist(memory_addr)) {

        pkt->newMetaData = mcache.find(memory_addr);

        if (isAddressCovered(pkt->new_origin, pkt->getSize(), 1)) {
            printf("hit in metadata cache, ppn %d\n", ppn);
            printf("the metaData is :\n");
            for (int k = 0; k < 64; k++) {
                printf("%02x",static_cast<unsigned>(pkt->newMetaData[k]));
            }
            printf("\n");
        }
        /*
            update the metadata when necessary, processing the sub-pkt so that
            pkt->getAddr() return the mpa address,
            the size also should be in alignment with the compressed form
         */
        updateMetaDataForNew(pkt, mem_intr);

        /* update the metadata in mcache & memory */
        std::vector<uint8_t> new_metaData = pkt->newMetaData;
        mcache.add(memory_addr, new_metaData);
        mem_intr->atomicWrite(new_metaData, memory_addr, 64);

        assignToQueueForNew(pkt);
    } else {
        // create a readMetaData pkt and add to read queue
        // printf("have to create a readMetaData \n");
        PacketPtr readMetaData = new Packet(pkt);
        DPRINTF(MemCtrl, "(pkt) Line %d, the readMetaData pkt %lx\n", __LINE__, readMetaData);

        readMetaData->configAsReadMetaDataForNew(pkt, memory_addr);
        if (!addToReadQueueForNew(readMetaData, 1, dram)) {
            // If we are not already scheduled to get a request out of the
            // queue, do so now
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Request scheduled immediately\n");
                schedule(nextReqEvent, curTick());
            }
        }
    }
}

void
MemCtrl::assignToQueueForNew(PacketPtr pkt) {
    unsigned size = pkt->getSize();
    uint32_t burst_size = dram->bytesPerBurst();

    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);
    if (pkt->newPType == 0x04) {
        pkt_count = 1;
    }

    if (pkt->isWrite()) {
        // not any more
        // // the pkt is burst-size aligned
        // assert(pkt->getSize() % 64 == 0);
        // assert(pkt->getAddr() % 64 == 0);

        /* now the pkt is burst-size aligned */
        addToWriteQueueForNew(pkt, pkt_count, dram);
        // If we are not already scheduled to get a request out of the
        // queue, do so now
        if (!nextReqEvent.scheduled()) {
            DPRINTF(MemCtrl, "Request scheduled immediately\n");
            schedule(nextReqEvent, curTick());
        }
    } else {
        assert(pkt->isRead());
        if (!addToReadQueueForNew(pkt, pkt_count, dram)) {
            // If we are not already scheduled to get a request out of the
            // queue, do so now
            if (!nextReqEvent.scheduled()) {
                DPRINTF(MemCtrl, "Request scheduled immediately\n");
                schedule(nextReqEvent, curTick());
            }
        }
    }
}

void
MemCtrl::issueWriteCmdForNew(PacketPtr pkt, Addr addr, unsigned size, MemInterface* mem_intr, bool recordStat) {
    if (recordStat) {
        stats.writePktSize[ceilLog2(size)]++;
        stats.writeBursts++;
        stats.requestorWriteAccesses[pkt->requestorId()]++;
    }

    // see if we can merge with an existing item in the write
    // queue and keep track of whether we have merged or not
    bool merged = isInWriteQueue.find(burstAlign(addr, mem_intr)) !=
        isInWriteQueue.end();

    // if the item was not merged we need to create a new write
    // and enqueue it
    if (!merged) {
        MemPacket* mem_pkt;

        mem_pkt = mem_intr->decodePacket(pkt, addr, size, false,
                                                mem_intr->pseudoChannel);
        // printf("Line %d, the address of mem_pkt is 0x%lx\n", __LINE__, reinterpret_cast<unsigned long>(mem_pkt));
        // Default readyTime to Max if nvm interface;
        //will be reset once read is issued
        mem_pkt->readyTime = MaxTick;

        mem_intr->setupRank(mem_pkt->rank, false);

        // assert(totalWriteQueueSize < writeBufferSize);
        // stats.wrQLenPdf[totalWriteQueueSize]++;

        DPRINTF(MemCtrl, "Adding to write queue\n");

        writeQueue[mem_pkt->qosValue()].push_back(mem_pkt);
        isInWriteQueue.insert(burstAlign(addr, mem_intr));

        // log packet
        logRequest(MemCtrl::WRITE, pkt->requestorId(),
                    pkt->qosValue(), mem_pkt->addr, 1);

        mem_intr->writeQueueSize++;

        // assert(totalWriteQueueSize == isInWriteQueue.size());

        if (recordStat) {
            // Update stats
            stats.avgWrQLen = totalWriteQueueSize;
        }

    } else {
        DPRINTF(MemCtrl,
                "Merging write burst with existing queue entry\n");

        // keep track of the fact that this burst effectively
        // disappeared as it was merged with an existing one
        if (recordStat) {
            stats.mergedWrBursts++;
        }
    }
}

void
MemCtrl::updateMetaDataForNew(PacketPtr pkt, MemInterface* mem_intr) {

    std::vector<uint8_t>& metaData = pkt->newMetaData;

    /* the pkt may not be burst-size aligned
       in that case, should make it aligned
    */
    /* read the data first */
    Addr origin_addr = pkt->new_origin;    // the address in OSPA space
    uint8_t cacheLineIdx = (origin_addr >> 6) & 0x3F;

    uint8_t type = new_getType(metaData, cacheLineIdx);
    std::vector<uint64_t> translationRes(3, 0);

    translationRes[0] = zeroAddr;
    uint8_t real_size = 64;

    if (new_getCoverage(metaData) <= cacheLineIdx) {
        assert(type == 0);
    } else {
        translationRes = new_addressTranslation(metaData, cacheLineIdx);
    }

    if (pkt->isWrite()) {
        // printf("type is %d, translationRes[0] 0x%lx, translationRes[1] 0x%lx, translationRes[2] 0x%lx\n", static_cast<unsigned int>(type), translationRes[0], translationRes[1], translationRes[2]);
        if (type >= 0b100) {
            uint8_t overflowIdx = 0;
            mem_intr->atomicRead(&overflowIdx, translationRes[0], 1);
            translationRes[0] = calOverflowAddr(metaData, overflowIdx);
        } else {
            real_size = sizeMap[type];
        }

        if (isAddressCovered(origin_addr, pkt->getSize(),1)) {
            printf("the type is %d\n", static_cast<unsigned int>(type));
            printf("write the mpa address is 0x%lx\n", translationRes[0]);
        }


        std::vector<uint8_t> cacheLine(64, 0);

        if (type >= 0b100 || translationRes[2] == 0) {
            mem_intr->atomicRead(cacheLine.data(), translationRes[0], real_size);
        } else {
            assert(sizeMap[type] > translationRes[2]);
            uint64_t prefixLen = sizeMap[type] - translationRes[2];
            mem_intr->atomicRead(cacheLine.data(), translationRes[0], prefixLen);
            mem_intr->atomicRead(cacheLine.data() + prefixLen, translationRes[1], translationRes[2]);
        }

        new_restoreData(cacheLine, type);

        if (isAddressCovered(origin_addr, pkt->new_backup->getSize(), 1)) {
            printf("the old type is %d\n", static_cast<unsigned int>(type));
            printf("the old data is:");
            for (int i = 0; i < cacheLine.size(); i++) {
                if (i % 8 == 0) {
                    printf("\n");
                }
                printf("%02x ", static_cast<unsigned int>(cacheLine[i]));

            }
            printf("\n");
        }

        /* write the new data */
        uint8_t offset = origin_addr & 0x3F;
        memcpy(cacheLine.data() + offset, pkt->getPtr<uint8_t>(), pkt->getSize());

        if (isAddressCovered(origin_addr, pkt->new_backup->getSize(), 1)) {
            printf("the new data is:");
            for (int i = 0; i < cacheLine.size(); i++) {
                if (i % 8 == 0) {
                    printf("\n");
                }
                printf("%02x ", static_cast<unsigned int>(cacheLine[i]));

            }
            printf("\n");
        }

        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {

           printf("the old metaData is \n");
            for (int k = 0; k < 64; k++) {
                printf("%02x",static_cast<unsigned>(metaData[k]));
            }
            printf("\n");
        }


        new_updateMetaData(cacheLine, metaData, cacheLineIdx, mem_intr);

        if (isAddressCovered(pkt->getAddr(), pkt->getSize(), 1)) {

           printf("the new metaData is \n");
            for (int k = 0; k < 64; k++) {
                printf("%02x",static_cast<unsigned>(metaData[k]));
            }
            printf("\n");
        }

        assert(new_getCoverage(metaData) > cacheLineIdx);

        std::vector<uint8_t> new_cacheLine = cacheLine;

        uint8_t new_type = new_getType(metaData, cacheLineIdx);

        if (new_type < 0b11) {
            new_cacheLine = compressForNew(cacheLine);
        } else {
            assert(new_cacheLine.size() == 64);
        }

        /* if the cacheline becomes all-zero while the type is not (underflow), the data is stored in compressed form rather than using one byte to hold place */
        if (new_type != 0 && new_cacheLine.size() == 1) {
            new_cacheLine = {0, 0, 0, 0, 126};
        }

        if (isAddressCovered(origin_addr, pkt->new_backup->getSize(), 1)) {
            printf("the new type is %d\n", static_cast<unsigned int>(new_type));
            printf("the compressed data is:");
            for (int i = 0; i < new_cacheLine.size(); i++) {
                if (i % 8 == 0) {
                    printf("\n");
                }
                printf("%02x ", static_cast<unsigned int>(new_cacheLine[i]));

            }
            printf("\n");
        }

        if(new_cacheLine.size() > 44) {
            assert(new_cacheLine.size() == 64);
        }

        pkt->setSizeForMC(new_cacheLine.size());
        pkt->allocateForMC();
        pkt->setDataForMC(new_cacheLine.data(), 0, new_cacheLine.size());

        std::vector<uint64_t> new_res = new_addressTranslation(metaData, cacheLineIdx);
        pkt->setAddr(new_res[0]); // translate the address from OSPA to MPA space
        pkt->newBlockAddr = new_res[1];
        if (new_type < 0b100) {
            assert(sizeMap[new_type] > new_res[2]);
            uint64_t prefixLen = sizeMap[new_type] - new_res[2];
            if (new_cacheLine.size() > prefixLen) {
                pkt->suffixLen = new_cacheLine.size() - prefixLen;
            } else {
                pkt->suffixLen = 0;
            }
        }
    } else {
        if (type < 0b100) {
            real_size = sizeMap[type];
        }
        pkt->setSizeForMC(real_size);
        pkt->allocateForMC();

        pkt->setAddr(translationRes[0]); // translate the address from OSPA to MPA space

        if (isAddressCovered(origin_addr, pkt->getSize(),1)) {
            printf("read the original mpa address is 0x%lx\n", translationRes[0]);
        }

        if (type < 0b100) {
            pkt->newBlockAddr = translationRes[1];
            pkt->suffixLen = translationRes[2];
        }
    }
}


/*
    select a target page and recompress
*/
void
MemCtrl::readForRecompress(PacketPtr pkt, MemInterface* mem_intr) {
    PPN target_page = 0;
    /* TODO: maintain a overflowPages list */
    // if (overflowPages.empty()) {
    //     // printf("choose a overflowPage\n");
    //     target_page = mcache.chooseTarget() / 64;
    // } else {
    //     target_page = overflowPages.front();
    //     overflowPages.pop_front();
    // }
    target_page = mcache.chooseTarget() / 64;

    // printf("target_page is %d\n", target_page);
    std::vector<uint8_t> metaData(64, 0);
    mem_intr->atomicRead(metaData.data(), target_page * 64, 64);

    uint8_t num_blocks = metaData[1];
    // printf("num_blocks is %d\n", num_blocks);
    PacketPtr readPage = new Packet(pkt);
    readPage->configAsReadPage(pkt, pageSizeMap[num_blocks], num_blocks, target_page);

    assert(pktInProcess == 0);

    for (int i = 0; i < num_blocks; i++) {
        PacketPtr readBlock = new Packet(readPage);
        Addr block_addr = 0;
        for (int j = 0; j < 4; j++) {
            block_addr = (block_addr << 8) | (metaData[4 * i + 4 + j]);
        }
        block_addr = block_addr << 9;
        Addr block_size = pageSizeMap[i + 1] - pageSizeMap[i];
        readBlock->configAsReadBlock(readPage, block_addr, block_size, i);
        assignToQueueForNew(readBlock);
    }
}

bool
MemCtrl::avoidDeadLockForCompr(MemInterface* mem_intr) {
    assert(mem_intr->readQueueSize == 0);
    assert(mem_intr->writeQueueSize != 0);

    bool res = true;

    if (operationMode != "compresso") {
        return res;
    }

    for (const auto &pkt: waitQueue) {
        if (pkt->isRead()) {
            if (isEligible(pkt)) {
                res = false;
                break;
            }
        }
    }
    return res;
}


void
MemCtrl::recompressForNew(PacketPtr pkt, std::vector<uint8_t>& metaData) {
    /* pkt is readPage pkt */
    std::vector<uint8_t> compressed_page(pkt->getSize(), 0);
    memcpy(compressed_page.data(), pkt->getPtr<uint8_t>(), pkt->getSize());

    uint8_t coverage = new_getCoverage(metaData);

    if (coverage == 0) {
        printf("[warning]: the coverage is zero\n");
        PacketPtr writeBlock = new Packet(pkt);
        Addr block_addr = 0;
        for (int u = 0; u < 4; u++) {   // 4B per MPFN
            block_addr = (block_addr << 8) | (metaData[4 + u]);
        }
        block_addr = block_addr << 9;
        uint64_t block_size = pageSizeMap[1] - pageSizeMap[0];
        writeBlock->configAsWriteBlock(pkt, block_addr, block_size);
        memset(writeBlock->getPtr<uint8_t>(), 0, block_size);
        assignToQueueForNew(writeBlock);
        return;
    }

    std::vector<uint8_t> page(4096, 0);

    /* restore the page */
    uint64_t offset = 0;
    uint64_t start_pos = metaData[2] * 64;
    for (int i = 0; i < coverage; i++) {
        uint8_t type = new_getType(metaData, i);
        if (type < 0b100) {
            /* the cacheline is not overflow */
            std::vector<uint8_t> cacheLine(64, 0);
            memcpy(cacheLine.data(), compressed_page.data() + offset, sizeMap[type]);
            new_restoreData(cacheLine, type);
            memcpy(page.data() + 64 * i, cacheLine.data(), 64);

        } else {
            assert(start_pos != 0);
            uint8_t overflowIdx = compressed_page[offset];
            memcpy(page.data() + 64 * i, compressed_page.data() + overflowIdx * 64 + start_pos, 64);
        }
        offset += sizeMap[type & 0b11];

        // printf("\n");

        // for (int rt = 0; rt < 64; rt++) {
        //     if (rt % 8 == 0) {
        //         printf("\n");
        //     }
        //     printf("%02x ", static_cast<unsigned int>(page[64 * i + rt]));
        // }
        // printf("\n");

    }

    /* compress the page and generate the new metaData */
    std::vector<uint8_t> new_metaData(64, 0);
    uint64_t new_size = 0;
    assert(coverage <= 64);
    new_metaData[0] = 0x80 | coverage;

    for (int i = 0; i < coverage; i++) {
        std::vector<uint8_t> cacheLine(64);
        memcpy(cacheLine.data(), page.data() + i * 64, 64);
        std::vector<uint8_t> compressed_data = compressForNew(cacheLine);
        memcpy(page.data() + new_size, compressed_data.data(), compressed_data.size());
        if (isAllZero(cacheLine)) {
            /* do nothing */
            assert(compressed_data.size() == 1);
            new_size += 1;
        } else {
            if (compressed_data.size() <= 22) {
                new_setType(new_metaData, i, 0b001);
                new_size += 22;
            } else if (compressed_data.size() <= 44) {
                new_setType(new_metaData, i, 0b010);
                new_size += 44;
            } else {
                new_setType(new_metaData, i, 0b011);
                new_size += 64;
            }
        }
    }

    if (coverage == 64) {
        uint64_t startOverflowRegion = ((new_size + 63) >> 6) << 6;
        assert(startOverflowRegion >= new_size);
        new_metaData[2] = (startOverflowRegion / 64);
        assert(startOverflowRegion < pageSizeMap[9]);
        new_size = startOverflowRegion;
    }



    auto it = std::lower_bound(pageSizeMap.begin(), pageSizeMap.end(), new_size);
    uint64_t chunk_num = it - pageSizeMap.begin();
    assert(chunk_num >= 1);

    new_metaData[1] = chunk_num;
    assert(stat_used_bytes >= pageSizeMap[metaData[1]]);
    stat_used_bytes -= pageSizeMap[metaData[1]];

    for (int cn = 0; cn < chunk_num; cn++) {
        new_allocateBlock(new_metaData, cn + 1);
    }
    assert(chunk_num <= metaData[1]);

    // if (chunk_num < metaData[1]) {
        // printf("saved %d blocks \n", metaData[1] - chunk_num);
    // }
    /* give back the old blocks */
    for (int cn = 0; cn < metaData[1]; cn++) {
        Addr old_addr = 0;
        for (int u = 0; u < 4; u++){   // 4B per MPFN
            old_addr = (old_addr << 8) | (metaData[4 * cn + 4 + u]);
        }
        if (pageSizeMap[cn + 1] - pageSizeMap[cn] == 512) {
            fineGrainedFreeList.push_front(old_addr << 9);
        } else {
            freeList.push_front(old_addr << 9);
        }
    }

    // printf("Recompress: the old metaData is: \n");
    // for (int k = 0; k < 64; k++) {
    //     printf("%02x",static_cast<unsigned>(metaData[k]));
    // }
    // printf("\n");

    /* update the metaData */
    metaData = new_metaData;

    // printf("Recompress: the new metaData is: \n");
    // for (int k = 0; k < 64; k++) {
    //     printf("%02x",static_cast<unsigned>(metaData[k]));
    // }
    // printf("\n");

    /* issue writeBlock pkt to write to memory */
    pkt->new_subPktCnt = chunk_num;
    for (int cn = 0; cn < chunk_num; cn++) {
        PacketPtr writeBlock = new Packet(pkt);
        Addr block_addr = 0;
        for (int u = 0; u < 4; u++) {   // 4B per MPFN
            block_addr = (block_addr << 8) | (metaData[4 * cn + 4 + u]);
        }
        block_addr = block_addr << 9;
        uint64_t block_size = pageSizeMap[cn + 1] - pageSizeMap[cn];
        writeBlock->configAsWriteBlock(pkt, block_addr, block_size);
        memcpy(writeBlock->getPtr<uint8_t>(), page.data() + pageSizeMap[cn], block_size);
        assignToQueueForNew(writeBlock);
    }
}


void
MemCtrl::atomicRecompressForNew(std::vector<uint8_t>& compressed_page, std::vector<uint8_t>& metaData, MemInterface* mem_intr) {

    uint8_t coverage = new_getCoverage(metaData);
    if (coverage == 0) {
        return;
    }

    std::vector<uint8_t> page(4096, 0);

    /* restore the page */
    uint64_t offset = 0;
    uint64_t start_pos = metaData[2] * 64;
    for (int i = 0; i < coverage; i++) {
        uint8_t type = new_getType(metaData, i);
        if (type < 0b100) {
            /* the cacheline is not overflow */
            std::vector<uint8_t> cacheLine(64, 0);
            memcpy(cacheLine.data(), compressed_page.data() + offset, sizeMap[type]);
            new_restoreData(cacheLine, type);
            memcpy(page.data() + 64 * i, cacheLine.data(), 64);

        } else {
            assert(start_pos != 0);
            uint8_t overflowIdx = compressed_page[offset];
            memcpy(page.data() + 64 * i, compressed_page.data() + overflowIdx * 64 + start_pos, 64);
        }
        offset += sizeMap[type & 0b11];
    }

    /* compress the page and generate the new metaData */
    std::vector<uint8_t> new_metaData(64, 0);
    uint64_t new_size = 0;
    assert(coverage <= 64);
    new_metaData[0] = 0x80 | coverage;

    for (int i = 0; i < coverage; i++) {
        std::vector<uint8_t> cacheLine(64);
        memcpy(cacheLine.data(), page.data() + i * 64, 64);
        std::vector<uint8_t> compressed_data = compressForNew(cacheLine);
        memcpy(page.data() + new_size, compressed_data.data(), compressed_data.size());
        if (isAllZero(cacheLine)) {
            /* do nothing */
            assert(compressed_data.size() == 1);
            new_size += 1;
        } else {
            if (compressed_data.size() <= 22) {
                new_setType(new_metaData, i, 0b001);
                new_size += 22;
            } else if (compressed_data.size() <= 44) {
                new_setType(new_metaData, i, 0b010);
                new_size += 44;
            } else {
                new_setType(new_metaData, i, 0b011);
                new_size += 64;
            }
        }
    }

    if (coverage == 64) {
        uint64_t startOverflowRegion = ((new_size + 63) >> 6) << 6;
        assert(startOverflowRegion >= new_size);
        new_metaData[2] = (startOverflowRegion / 64);
        assert(startOverflowRegion < pageSizeMap[9]);
        new_size = startOverflowRegion;
    }

    auto it = std::lower_bound(pageSizeMap.begin(), pageSizeMap.end(), new_size);
    uint64_t chunk_num = it - pageSizeMap.begin();
    assert(chunk_num >= 1);

    new_metaData[1] = chunk_num;
    assert(stat_used_bytes >= pageSizeMap[metaData[1]]);
    stat_used_bytes -= pageSizeMap[metaData[1]];

    for (int cn = 0; cn < chunk_num; cn++) {
        new_allocateBlock(new_metaData, cn + 1);
    }
    assert(chunk_num <= metaData[1]);

    // if (chunk_num < metaData[1]) {
        // printf("saved %d blocks \n", metaData[1] - chunk_num);
    // }
    /* give back the old blocks */
    for (int cn = 0; cn < metaData[1]; cn++) {
        Addr old_addr = 0;
        for (int u = 0; u < 4; u++){   // 4B per MPFN
            old_addr = (old_addr << 8) | (metaData[4 * cn + 4 + u]);
        }
        if (pageSizeMap[cn + 1] - pageSizeMap[cn] == 512) {
            fineGrainedFreeList.push_front(old_addr << 9);
        } else {
            freeList.push_front(old_addr << 9);
        }
    }

    /* update the metaData */
    metaData = new_metaData;

    // printf("Recompress: the new metaData is: \n");
    // for (int k = 0; k < 64; k++) {
    //     printf("%02x",static_cast<unsigned>(metaData[k]));
    // }
    // printf("\n");


    /* write to the new space */
    for (int cn = 0; cn < chunk_num; cn++) {
        Addr block_addr = 0;
        for (int u = 0; u < 4; u++) {   // 4B per MPFN
            block_addr = (block_addr << 8) | (metaData[4 * cn + 4 + u]);
        }
        block_addr = block_addr << 9;
        uint64_t block_size = pageSizeMap[cn + 1] - pageSizeMap[cn];
        mem_intr->atomicWrite(page, block_addr, block_size, pageSizeMap[cn]);

    }
}


} // namespace memory
} // namespace gem5
