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

#define ALIGN(x) (((x + 4095) >> 12) << 12)

namespace gem5
{

namespace memory
{

MemCtrl::MemCtrl(const MemCtrlParams &p) :
    qos::MemCtrl(p),
    operationMode(p.operation_mode),
    port(name() + ".port", *this), isTimingMode(false),
    retryRdReq(false), retryWrReq(false),
    nextReqEvent([this] {processNextReqEvent(dram, respQueue,
                         respondEvent, nextReqEvent, retryWrReq);}, name()),
    respondEvent([this] {processRespondEvent(dram, respQueue,
                         respondEvent, retryRdReq); }, name()),
    dram(p.dram),
    globalPredictor(0), mcache(MetaCache(64)),
    sizeMap(std::vector<uint8_t>(4)),
    hasBuffered(false), pageNum(0),
    mPageBuffer(std::vector<uint8_t>(64)),
    pageBuffer(std::vector<uint8_t>(4096)),
    curReadNum(0), curWriteNum(0), blockedNum(0),
    readBufferSizeForCompr(dram->readBufferSize),
    writeBufferSizeForCompr(dram->writeBufferSize),
    blockedForCompr(false),
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
    } else {
        panic("unknown mode for memory controller");
    }

    /* initialize the state for memory compression */

    /* get the size of OSPA space */
    uint64_t OSPACapacity = 1ULL << ceilLog2(dram->AbstractMemory::size());
    uint32_t numPages = OSPACapacity / (4 * 1024);

    printf("the num of pages is %d\n", numPages);

    if (operationMode == "compresso") {
        /* calculate the real start address */
        printf("the real start address is 0x%llx\n", realStartAddr);
        realStartAddr = ALIGN(64 * numPages); // 64B metadata per OSPA page (4KB)

        /* push the available free chunks into the freeList */
        uint64_t dramCapacity = std::min((dram->capacity() * (1024 * 1024)), OSPACapacity / 2);
        assert(realStartAddr < dramCapacity);
        for (Addr addr = realStartAddr; (addr + 512) <= dramCapacity; addr += 512) {
            freeList.emplace_back(addr);
        }
    } else if (operationMode == "DyLeCT") {
        // TODO

    }

    sizeMap = {0, 8, 32, 64};
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

Tick
MemCtrl::recvAtomic(PacketPtr pkt)
{
    if (!dram->getAddrRange().contains(pkt->getAddr())) {
        panic("Can't handle address range for packet %s\n", pkt->print());
    }

    Tick res = 0;
    if (operationMode == "normal") {
        res = recvAtomicLogic(pkt, dram);
    } else if (operationMode == "compresso") {
        res = recvAtomicLogicForCompr(pkt, dram);
    } else if (operationMode == "DeLyCT") {
        //TODO
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

    // do the actual memory access and turn the packet into a response
    mem_intr->access(pkt);

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
                // assert(pageNum == ppn || pkt->isWrite());
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
                    for (uint8_t u = 0; u < 64; u++) {
                        uint8_t type = getType(mPageBuffer, u);
                        uint8_t dataLen = sizeMap[type];
                        for (uint8_t v = 0; v < dataLen; v++) {
                            compressedPage.emplace_back(pageBuffer[64 * u + v]);
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
                    printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, pageNum, mPageBufferAddr);
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
                auxPkt->comprMetaDataMap[ppn] = mPageBuffer;
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
        DPRINTF(MemCtrl, "Line %d: cur req is write \n", __LINE__);

        Addr addrAligned = (base_addr >> 6) << 6;
        uint64_t new_size = ((((base_addr + size) + (burst_size - 1)) >> 6) << 6) - addrAligned;

        assert(burst_size == 64);
        assert(new_size == (addr - addrAligned));

        if (auxPkt->cmd == MemCmd::SwapReq) {
            DPRINTF(MemCtrl, "Line %d: req's cmd is swapReq \n", __LINE__);
            /* step 2.1.1 assert(pkt.size <= 64 && pkt is not cross the boundary)*/
            assert(size <= 64 && (burst_size - (base_addr | (burst_size - 1)) <= size));

            PPN ppn = (base_addr >> 12 & ((1ULL << 52) - 1));
            std::vector<uint8_t> cacheLine(64, 0);

            assert(auxPkt->comprMetaDataMap.find(ppn) != auxPkt->comprMetaDataMap.end());
            std::vector<uint8_t> metaData = auxPkt->comprMetaDataMap[ppn];
            uint8_t cacheLineIdx = (base_addr >> 6) & 0x3F;
            uint8_t type = getType(metaData, cacheLineIdx);
            bool inInflate = false;
            Addr real_addr = 0;

            if (ppn == pageNum) {
                type = getType(mPageBuffer, cacheLineIdx);
                for (unsigned int u = 0; u < sizeMap[type]; u++) {
                    cacheLine[u] = pageBuffer[cacheLineIdx * 64 + u];
                }
                restoreData(cacheLine, type);
            } else {
                std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);
                inInflate = cLStatus.first;
                real_addr = cLStatus.second;

                if (inInflate) {
                    type = 0b11;
                }

                if (type != 0) {
                    /* step 2.1.2 read the cacheLine from memory */
                    mem_intr->atomicRead(cacheLine.data(), real_addr, sizeMap[type]);
                }

                /* step 2.1.3 decompress */
                restoreData(cacheLine, type);
            }

            /* step 2.1.4 the same as before, host_addr = cacheLine.data() + ofs */
            assert(cacheLine.size() == burst_size);
            uint8_t* uPtr = cacheLine.data() + offset;
            if (pkt->isAtomicOp()) {
                if (mem_intr->hasValidHostMem()) {
                    pkt->setData(uPtr);
                    (*(pkt->getAtomicOp()))(uPtr);
                }
            } else {
                std::vector<uint8_t> overwrite_val(pkt->getSize());
                uint64_t condition_val64;
                uint32_t condition_val32;

                panic_if(!mem_intr->hasValidHostMem(), "Swap only works if there is real memory " \
                        "(i.e. null=False)");

                bool overwrite_mem = true;
                // keep a copy of our possible write value, and copy what is at the
                // memory address into the packet
                pkt->writeData(&overwrite_val[0]);
                pkt->setData(uPtr);

                if (pkt->req->isCondSwap()) {
                    if (pkt->getSize() == sizeof(uint64_t)) {
                        assert(uPtr == cacheLine.data());
                        condition_val64 = pkt->req->getExtraData();
                        overwrite_mem = !std::memcmp(&condition_val64, uPtr,
                                                    sizeof(uint64_t));
                    } else if (pkt->getSize() == sizeof(uint32_t)) {
                        condition_val32 = (uint32_t)pkt->req->getExtraData();
                        overwrite_mem = !std::memcmp(&condition_val32, uPtr,
                                                    sizeof(uint32_t));
                    } else
                        panic("Invalid size for conditional read/write\n");
                }

                if (overwrite_mem) {
                    std::memcpy(uPtr, &overwrite_val[0], pkt->getSize());
                }
            }

            /*step 2.1.5 recompress the cacheline */
            std::vector<uint8_t> compressed = compress(cacheLine);

            if (ppn == pageNum) {
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
            } else {
                /* step 2.1.6 deal with potential overflow/underflow */
                updateMetaData(compressed, metaData, cacheLineIdx, inInflate, mem_intr);
                auxPkt->comprMetaDataMap[ppn] = metaData;
                Addr metadata_addr = ppn * 64;
                printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, ppn, metadata_addr);
                mcache.add(metadata_addr, metaData);
                mem_intr->atomicWrite(metaData, metadata_addr, 64, 0);
            }

            assert(new_size == 64);
            auxPkt->setAddr(addrAligned);
            auxPkt->setSizeForMC(new_size);
            auxPkt->allocateForMC();
            auxPkt->setDataForMC(cacheLine.data(), 0, new_size);
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
                    DPRINTF(MemCtrl, "Line %d: the pageNum == ppn, need to modify the pageBuffer \n", __LINE__);
                    assert(pageBuffer.size() == 4096);
                    uint8_t type = getType(mPageBuffer, cacheLineIdx);

                    for (unsigned int j = 0; j < sizeMap[type]; j++) {
                        cacheLine[j] = pageBuffer[64 * cacheLineIdx + j];
                    }

                    DPRINTF(MemCtrl, "Line %d: finish read the cacheline from pagebuffer \n", __LINE__);

                    restoreData(cacheLine, type);
                    assert(cacheLine.size() == 64);

                    DPRINTF(MemCtrl, "Line %d: finish restore the cacheline \n", __LINE__);

                    /* write the data */
                    uint64_t ofs = addr - base_addr;
                    uint8_t loc = addr & 0x3F;
                    size_t writeSize = std::min(64UL - loc, size - ofs);

                    DPRINTF(MemCtrl, "Line %d: before write the data, the ofs is %lld, the loc is %d, the writeSize is %ld\n", __LINE__, ofs, loc, writeSize);

                    auxPkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);
                    DPRINTF(MemCtrl, "Line %d: finish write the data \n", __LINE__);

                    std::vector<uint8_t> compressed = compress(cacheLine);

                    DPRINTF(MemCtrl, "Line %d: finish recompress the data \n", __LINE__);

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


                    std::vector<uint8_t> compressed = compress(cacheLine);
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
                        printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, ppn, metadata_addr);
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
        DPRINTF(MemCtrl, "Line %d: cur req is read \n", __LINE__);
        assert(pkt->isRead());
    }
    // do the actual memory access and turn the packet into a response
    // mem_intr->access(pkt);
    DPRINTF(MemCtrl, "next we have to do the real access\n");
    assert(burst_size == 64);
    mem_intr->accessForCompr(auxPkt, burst_size, pageNum, pageBuffer, mPageBuffer);
    DPRINTF(MemCtrl, "finish access\n");

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
MemCtrl::writeQueueFull(unsigned int neededEntries) const
{
    DPRINTF(MemCtrl,
            "Write queue limit %d, current size %d, entries needed %d\n",
            writeBufferSize, totalWriteQueueSize, neededEntries);

    auto wrsize_new = (totalWriteQueueSize + neededEntries);
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

            // Increment read entries of the rank (dram)
            // Increment count to trigger issue of non-deterministic read (nvm)
            mem_intr->setupRank(mem_pkt->rank, true);
            // Default readyTime to Max; will be reset once read is issued
            mem_pkt->readyTime = MaxTick;
            mem_pkt->burstHelper = burst_helper;

            assert(!readQueueFull(1));
            stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;

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
    
                                // Increment read entries of the rank (dram)
                                // Increment count to trigger issue of non-deterministic read (nvm)
                                mem_intr->setupRank(mem_pkt->rank, true);
                                // Default readyTime to Max; will be reset once read is issued
                                mem_pkt->readyTime = MaxTick;
                                mem_pkt->burstHelper = burst_helper;
    
                                // stats.rdQLenPdf[totalReadQueueSize + respQueue.size()]++;
    
                                DPRINTF(MemCtrl, "Compr: Adding to read queue\n");
    
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

    DPRINTF(MemCtrl, "the pkt is %s\n", pkt->cmdString());

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

        /* first iteration
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

            bool crossBoundary = false;
            Addr real_addr = addr;
            unsigned real_size = size;
            std::vector<uint8_t> cacheLine(64, 0);

            if (ppn == pageNum) {
                /* hit in the page buffer */
                stats.writeBursts++;
                stats.requestorWriteAccesses[pkt->requestorId()]++;

                /* update the metadata if necessary */
                uint8_t type = getType(mPageBuffer, cacheLineIdx);
                assert(type == getType(metaData, cacheLineIdx));
                for (unsigned int j = 0; j < sizeMap[type]; j++) {
                    cacheLine[j] = pageBuffer[64 * cacheLineIdx + j];
                }
                restoreData(cacheLine, type);
                assert(cacheLine.size() == 64);

                /* write the data */
                uint64_t ofs = addr - base_addr;
                uint8_t loc = addr & 0x3F;
                size_t writeSize = std::min(64UL - loc, pkt->getSize() - ofs);

                pkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

                std::vector<uint8_t> compressed = compress(cacheLine);

                /*write back to pageBuffer and update the mPageBuffer if necessary */
                if (isAllZero(cacheLine)) {
                    /* set the mPageBuffer entry to be 0 */
                    DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
                    setType(metaData, cacheLineIdx, 0);
                } else {
                    if (compressed.size() <= 8) {
                        /* set the mPageBuffer entry to be 0b1*/
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

                real_size = sizeMap[type];

                restoreData(cacheLine, type);

                // printf("the restored cacheline value is :\n");
                // for (int qw = 0; qw < 8; qw++) {
                //    for (int er = 0; er < 8; er++) {
                //        printf("%02x ", cacheLine[qw* 8 + er]);
                //    }
                //    printf("\n");
                // }
                // printf("\n");

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


                pkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);

                // printf("the updated cacheline value is :\n");
                // for (int qw = 0; qw < 8; qw++) {
                //    for (int er = 0; er < 8; er++) {
                //        printf("%02x ", cacheLine[qw* 8 + er]);
                //    }
                //    printf("\n");
                // }
                // printf("\n");

                std::vector<uint8_t> compressed = compress(cacheLine);
                printf("after compress, the updated cacheline value is :\n");
                for (int qw = 0; qw < compressed.size(); qw++) {
                    if (qw % 8 == 0) {
                        printf("\n");
                    }
                    printf("%02x ", cacheLine[qw]);
                }
                printf("\n");

                if (compressed.size() > 32) {
                    assert(compressed.size() == 64);
                }

                /* update the metadata if necessary */
                if (inInflate) {
                    /* check if we could write back */
                    if (compressed.size() <= sizeMap[old_type]) {
                        DPRINTF(MemCtrl, "underflow, write back to the oirginal space\n");
                        /* TODO: this may also cost some time, right now just make it atomic */
                        real_addr = moveForwardAtomic(metaData, cacheLineIdx, mem_intr);
                        real_size = sizeMap[old_type];
                        updatedMetaData.insert(ppn);
                    } else {
                        /* do nothing */
                    }
                } else {
                    if (compressed.size() <= sizeMap[old_type]) {
                        /* if not overflow */
                        /* do nothing */
                    } else {
                        DPRINTF(MemCtrl, "Line %d: the cacheline overflow \n", __LINE__);
                        printf("the pageNum is %d\n", ppn);
                        printf("the metadata is :\n");\
                        for (int k = 0; k < 64; k++) {
                            printf("%02x",static_cast<unsigned>(metaData[k]));

                        }
                        printf("\n");

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

                            printf("the new metadata is :\n");
                            for (int k = 0; k < 64; k++) {
                                printf("%02x",static_cast<unsigned>(metaData[k]));

                            }
                            printf("\n");
                        } else {
                            /* deal with page overflow */
                            overflowPageNum = ppn;
                            pageOverFlow = true;
                            break;
                        }
                    }
                }
                metaDataMap[ppn] = metaData;
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
            readForCompress->configAsReadForCompress(pkt->comprMetaDataMap[overflowPageNum], overflowPageNum);

            DPRINTF(MemCtrl, "(pkt) Line %d, the readForCompress pkt %lx\n", __LINE__, readForCompress);

            waitQueue.insert(readForCompress);

            if (!addToReadQueueForCompr(readForCompress, 64, dram)) {
                // If we are not already scheduled to get a request out of the
                // queue, do so now
                if (!nextReqEvent.scheduled()) {
                    DPRINTF(MemCtrl, "Request scheduled immediately\n");
                    schedule(nextReqEvent, curTick());
                }
            }
            readForCompress->checkIfValid();
            return false;

        } else {
            pkt->comprMetaDataMap = metaDataMap;
            //update the new metadata for the following pkt in waitQueue
            for (const auto& ppn: updatedMetaData) {
                DPRINTF(MemCtrl, "Line %d, enter the update subseqMetadata\n", __LINE__);
                updateSubseqMetaData(pkt, ppn);
                printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, ppn, ppn * 64);
                mcache.add(ppn * 64, metaDataMap[ppn]);
                if (ppn != pageNum) {
                    mem_intr->atomicWrite(metaDataMap[ppn], ppn * 64, 64, 0);
                } else {
                    mPageBuffer = metaDataMap[ppn];
                }
                
            }

            addr = base_addr;
            for (int cnt = 0; cnt < pkt_count; ++cnt) {
                /* Compr: use the metadata to do the translation */
                PPN ppn = (addr >> 12 & ((1ULL << 52) - 1));
                uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

                Addr metadata_addr = ppn * 64;
                std::vector<uint8_t> metaData = pkt->comprMetaDataMap[ppn];

                printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, ppn, metadata_addr);
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
                                    // Default readyTime to Max if nvm interface;
                                    //will be reset once read is issued
                                    mem_pkt->readyTime = MaxTick;

                                    mem_intr->setupRank(mem_pkt->rank, false);

                                    stats.wrQLenPdf[totalWriteQueueSize]++;

                                    DPRINTF(MemCtrl, "Line %d: Adding to write queue\n", __LINE__);

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
                                // Default readyTime to Max if nvm interface;
                                //will be reset once read is issued
                                mem_pkt->readyTime = MaxTick;

                                mem_intr->setupRank(mem_pkt->rank, false);

                                stats.wrQLenPdf[totalWriteQueueSize]++;

                                DPRINTF(MemCtrl, "Line %d: Adding to write queue\n", __LINE__);

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

            printf("the changed newdata is :\n");
            for (int qw = 0; qw < newData.size(); qw++) {
                if (qw % 8 == 0) {
                    printf("\n");
                }
                printf("%02x ", newData[qw]);
            }
            printf("\n");
            
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
            // Default readyTime to Max if nvm interface;
            //will be reset once read is issued
            mem_pkt->readyTime = MaxTick;

            mem_intr->setupRank(mem_pkt->rank, false);

            DPRINTF(MemCtrl, "Line %d: Adding to write queue\n", __LINE__);

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
    DPRINTF(MemCtrl, "Line %d, next enter the access and respond function\n", __LINE__);
    accessAndRespondForCompr(pkt, frontendLatency, mem_intr);
    DPRINTF(MemCtrl, "Line %d, finish access and respond\n", __LINE__);
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

    bool isAccepted = false;
    if (operationMode == "normal") {
        isAccepted = recvTimingReqLogic(pkt);
    } else if (operationMode == "compresso") {
        isAccepted = recvTimingReqLogicForCompr(pkt);

    } else if (operationMode == "DyLeCT") {
        // TODO
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
            printf("marker accept TimingReq: request %s addr %#x size %d\n",
                pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());

            uint8_t* my_test_start = pkt->getPtr<uint8_t>();
            for (int is = 0; is < pkt->getSize(); is++) {
                if (is % 8 == 0) {
                    printf("\n");
                }
                printf("%02x ",static_cast<unsigned>(my_test_start[is]));
            }
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
            printf("marker accept TimingReq: request %s addr %#x size %d\n",
                pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
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
        assert(!hasBlocked);
        if (blockedNum + pkt_count <= std::min(writeBufferSizeForCompr, readBufferSizeForCompr)) {
            pkt->comprTick = curTick();
            blockPktQueue.emplace_back(pkt);
            blockedNum += pkt_count;
            return true;
        } else {
            return false;
        }
    }

    // run the QoS scheduler and assign a QoS priority value to the packet
    qosSchedule( { &readQueue, &writeQueue }, burst_size, pkt);

    DPRINTF(MemCtrl, "finish the Qos scheduler\n");

    /* refuse the pkt is exceed the buffer size */
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
            curReadNum += pkt_count;
        }
    }

    /* initial an auxiliary pkt for next steps */
    PacketPtr auxPkt = new Packet(pkt, curTick());

    DPRINTF(MemCtrl, "(pkt) Line %d, the aux pkt %lx for the request %s\n", __LINE__, auxPkt, pkt->cmdString());
    DPRINTF(MemCtrl, "the auxPkt is %s\n", auxPkt->cmdString());

    printf("marker accept TimingReq: request %s addr %#x size %d\n",
        pkt->cmdString().c_str(), pkt->getAddr(), pkt->getSize());
    printf("%lx\n", auxPkt);
    memcpy(auxPkt->getPtr<uint8_t>(), pkt->getPtr<uint8_t>(), pkt->getSize());

    if (hasBlocked) {
        auxPkt->comprTick = pkt->comprTick;
    }

    waitQueue.insert(auxPkt);

    prepareMetaData(auxPkt);
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
                accessAndRespondForCompr(mem_pkt->pkt, frontendLatency + backendLatency,
                                mem_intr);
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
            accessAndRespondForCompr(mem_pkt->pkt, frontendLatency + backendLatency,
                            mem_intr);
        }
    }

    queue.pop_front();

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
    mem_intr->access(pkt);

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
        // mem_intr->access(pkt);
        mem_intr->accessForCompr(pkt, burst_size, pageNum, pageBuffer, mPageBuffer);
        waitQueue.erase(pkt);

        printf("after erase the current pkt, the waitQueue size is %d\n", waitQueue.size());

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

        if (ppn == pageNum) {
            metaData = mPageBuffer;
        } else {
            mem_intr->atomicRead(metaData.data(), pkt->getAddr(), 64);
        }

        /* finish read the metaData */
        if (!isValidMetaData(metaData)) {
            assert(pageNum != ppn);
            // flush the pageBuffer in memory
            blockedForCompr = true;
            PacketPtr writeForCompress = new Packet(pkt, auxPkt->comprTick - 1);
            DPRINTF(MemCtrl, "(pkt) Line %d, the writeForCompress pkt %lx\n", __LINE__, writeForCompress); 

            std::vector<uint8_t> uncompressPage(4096);
            for (int u = 0; u < 64; u++) {
                std::vector<uint8_t> curCL(64, 0);
                memcpy(curCL.data(), pageBuffer.data() + 64 * u, 64);
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
            // store the size of compressedPage into the control block (using 12 bit)
            uint64_t compressedSize = cPageSize;

            mPageBuffer[1] = compressedSize & (0xFF);
            mPageBuffer[0] = mPageBuffer[0] | ((compressedSize >> 8) & 0xF);

            DPRINTF(MemCtrl, "the final metadata is: \n");
            for (int k = 0; k < 64; k++) {
                printf("%02x",static_cast<unsigned>(mPageBuffer[k]));

            }
            printf("\n");

            writeForCompress->configAsWriteForCompress(uncompressPage.data(), pageNum);
            writeForCompress->comprMetaDataMap[pageNum] = mPageBuffer;

            waitQueue.insert(writeForCompress);
            // TODO2: right now just instantly update the metadata in memory
            mem_intr->atomicWrite(mPageBuffer, pageNum * 64, 64);
            printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, pageNum, pageNum * 64);
            mcache.add(pageNum * 64, mPageBuffer);
            DPRINTF(MemCtrl, "Line %d, enter the update subseqMetadata\n", __LINE__);
            updateSubseqMetaData(writeForCompress, pageNum);
            
            initialPageBuffer(ppn);
            assert(auxPkt->comprMetaDataMap.find(ppn) != auxPkt->comprMetaDataMap.end());
            auxPkt->comprMetaDataMap[ppn] = mPageBuffer;
            mem_intr->atomicWrite(mPageBuffer, ppn * 64, 64);

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
        mem_intr->accessForCompr(pkt, burst_size, pageNum, pageBuffer, mPageBuffer);
        std::vector<uint8_t> newMetaData = recompressTiming(pkt);
        assert(pkt->comprTick != 0);
        PacketPtr writeForCompress = new Packet(pkt->comprBackup, pkt->comprTick);
        DPRINTF(MemCtrl, "(pkt) Line %d, the readMetaData pkt %lx\n", __LINE__, writeForCompress); 
        uint8_t* start_addr = pkt->getPtr<uint8_t>();
        writeForCompress->configAsWriteForCompress(start_addr, ppn);
        writeForCompress->comprMetaDataMap[ppn] = newMetaData;

        /* readForCompress has finished its life time */
        waitQueue.erase(pkt);

        /* add the writeForCompress to the waitQueue */
        waitQueue.insert(writeForCompress);

        // TODO2: right now just instantly update the metadata in memory
        printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, ppn, ppn * 64);
        mcache.add(ppn * 64, newMetaData);
        mem_intr->atomicWrite(newMetaData, ppn * 64, 64);
        DPRINTF(MemCtrl, "Line %d, enter the update subseqMetadata\n", __LINE__);
        updateSubseqMetaData(writeForCompress, ppn); //TODO

        assignToQueue(writeForCompress);

    } else if (pType == 0x20) {
        /* writeForCompress */
        mem_intr->accessForCompr(pkt, burst_size, pageNum, pageBuffer, mPageBuffer);
        waitQueue.erase(pkt);
        checkForReadyPkt();

        PacketPtr backup_pkt = pkt->comprBackup;
        if (backup_pkt->getPType() == 0x4) {
            /* backup is readMetaData, the writeForCompress is triggered by replace the pageBuffer */
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

        }
        blockedForCompr = false;
        /* the memory controller is no longer blocked */
        /* reprocess the blocked pkt */
        for (unsigned int u = 0; u < blockPktQueue.size(); u++) {
            PacketPtr blocked_pkt = blockPktQueue[u];
            bool is_accepted = recvTimingReqLogicForCompr(blocked_pkt, true);
            if (!is_accepted) {
                panic("should be always accept at this moment");
            }
        }
        blockPktQueue.clear();

    } else {
        panic("unknown pkt type in accessAndRespondForCompr");
    }
    DPRINTF(MemCtrl, "finish access and respond, the waitQueue size is %d\n", waitQueue.size());
    for (const auto &pkt: waitQueue) {
        DPRINTF(MemCtrl, "[test], the pkt address is %lx\n", pkt);
    }
    delete pkt;
    return;
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
    DPRINTF(MemCtrl, "the current waitQueue size is %d\n", waitQueue.size());

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
            if (!(mem_intr->writeQueueSize == 0) &&
                (drainState() == DrainState::Draining ||
                 mem_intr->writeQueueSize > writeLowThreshold)) {

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

        mem_intr->writeQueueSize--;

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
             "Per-requestor write average memory access latency")
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
}

void
MemCtrl::recvFunctional(PacketPtr pkt)
{
    bool found = false;
    if (operationMode == "normal") {
        found = recvFunctionalLogic(pkt, dram);
    } else if (operationMode == "compresso") {
        found = recvFunctionalLogicForCompr(pkt, dram);
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
    DPRINTF(MemCtrl, "recvFunction: %s 0x%x\n",
        pkt->cmdString(), pkt->getAddr());
    if (mem_intr->getAddrRange().contains(pkt->getAddr())) {
        // rely on the abstract memory
        mem_intr->functionalAccess(pkt);
        return true;
    } else {
        return false;
    }
}

bool
MemCtrl::recvFunctionalLogicForCompr(PacketPtr pkt, MemInterface* mem_intr) {
    DPRINTF(MemCtrl, "recvFunction: %s 0x%x\n",
        pkt->cmdString(), pkt->getAddr());

    if (mem_intr->getAddrRange().contains(pkt->getAddr())) {

        /* step 0: create an auxiliary packet for processing the pkt */
        PacketPtr auxPkt = new Packet(pkt);

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
                    assert(pageNum == ppn || pkt->isWrite());
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

                        DPRINTF(MemCtrl, "the origin metadata is: \n");
                        for (int k = 0; k < 64; k++) {
                            printf("%02x",static_cast<unsigned>(mPageBuffer[k]));
                        }
                        printf("\n");

                        std::vector<uint8_t> compressedPage;
                        for (uint8_t u = 0; u < 64; u++) {
                            uint8_t type = getType(mPageBuffer, u);
                            uint8_t dataLen = sizeMap[type];
                            for (uint8_t v = 0; v < dataLen; v++) {
                                compressedPage.emplace_back(pageBuffer[64 * u + v]);
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

                        DPRINTF(MemCtrl, "the final metadata is: \n");
                        for (int k = 0; k < 64; k++) {
                            printf("%02x",static_cast<unsigned>(mPageBuffer[k]));

                        }
                        printf("\n");

                        /* write the mPageBuffer to mcache and memory */
                        Addr mPageBufferAddr = pageNum * 64;
                        printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, pageNum, mPageBufferAddr);
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
                    auxPkt->comprMetaDataMap[ppn] = mPageBuffer;
                } else {
                    auxPkt->comprMetaDataMap[ppn] = metaData;
                }
            }
            // Starting address of next memory pkt (aligned to burst boundary)
            addr = (addr | (burst_size - 1)) + 1;
        }

        /* step 2: process the pkt based on isWrite or isRead*/

        // DPRINTF(MemCtrl, "(F) Line %d: actually process the pkt \n", __LINE__);
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

                // DPRINTF(MemCtrl, "(F) Line %d: start process the pkt, the pkt size is %lld, the pkt's address is 0x%llx \n", __LINE__, size, base_addr);
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

                        assert(pageBuffer.size() == 4096);
                        uint8_t type = getType(mPageBuffer, cacheLineIdx);

                        for (unsigned int j = 0; j < sizeMap[type]; j++) {
                            cacheLine[j] = pageBuffer[64 * cacheLineIdx + j];
                        }

                        // DPRINTF(MemCtrl, "Line %d: finish read the cacheline from pagebuffer \n", __LINE__);

                        restoreData(cacheLine, type);
                        assert(cacheLine.size() == 64);

                        // DPRINTF(MemCtrl, "Line %d: finish restore the cacheline \n", __LINE__);

                        /* write the data */
                        uint64_t ofs = addr - base_addr;
                        uint8_t loc = addr & 0x3F;
                        size_t writeSize = std::min(64UL - loc, size - ofs);

                        // DPRINTF(MemCtrl, "Line %d: before write the data, the ofs is %lld, the loc is %d, the writeSize is %ld\n", __LINE__, ofs, loc, writeSize);

                        auxPkt->writeDataForMC(cacheLine.data() + loc, ofs, writeSize);
                        // DPRINTF(MemCtrl, "Line %d: finish write the data \n", __LINE__);

                        std::vector<uint8_t> compressed = compress(cacheLine);

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

                        std::vector<uint8_t> compressed = compress(cacheLine);
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
        mem_intr->comprFunctionalAccess(auxPkt, burst_size, pageNum, pageBuffer, mPageBuffer);
        // mem_intr->functionalAccessForMC(auxPkt, burst_size, pageNum);
        return true;
    } else {
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
    printf("original metadata: \n");
    for (int k = 0; k < 64; k++) {
        printf("%02x",static_cast<unsigned>(metaData[k]));

    }
    printf("\n");
    assert(type < 4);
    int startPos = (2 + 32) * 8 + index * 2;
    int loc = startPos / 8;
    int ofs = startPos % 8;
    metaData[loc] = (metaData[loc] & (~(0b11 << (6 - ofs)))) |  (type << (6 - ofs));
    printf("new metadata: \n");
    for (int k = 0; k < 64; k++) {
        printf("%02x",static_cast<unsigned>(metaData[k]));

    }
    printf("\n");
}

void MemCtrl::initialPageBuffer(const PPN& ppn) {
    /* mark the corresponding metadata as valid */
    mPageBuffer[0] = (0b1 << 7);

    for (uint64_t i = 1; i < 64; i++) {
        if (i >= 34 && i < 50) {
            /* set all the encodings as 0b11 (uncompressed) */
            mPageBuffer[i] = 0xFF;
        } else {
            mPageBuffer[i] = 0;
        }
    }
    pageNum = ppn;
    hasBuffered = true;
    memset(pageBuffer.data(), 0, pageBuffer.size() * sizeof(uint8_t));
}

void
MemCtrl::restoreData(std::vector<uint8_t>& cacheLine, uint8_t type) {
//    printf("enter the restore data, the type is %d\n", static_cast<uint8_t>(type));
    if (type == 0b00) {
        for (int i = 0; i < cacheLine.size(); i++) {
            cacheLine[i] = 0;
        }
    } else if (type == 0b10 || type == 0b01) {
        decompress(cacheLine);
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
    }
    DPRINTF(MemCtrl, "Line %d: the new address is 0x%llx\n", __LINE__, addr);
    /* update the metaData for the inflation room */
    setInflateEntry(valid_inflate_num, metaData, index);

    valid_inflate_num++;
    metaData[63] = (metaData[63] & 0xE0) | (valid_inflate_num & 0x1F);
    return addr;
}

Addr
MemCtrl::moveForwardAtomic(std::vector<uint8_t>& metaData, const uint8_t& index, MemInterface* mem_intr) {
    DPRINTF(MemCtrl, "Line %d: you enter the move forward atomic function\n", __LINE__);

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

    DPRINTF(MemCtrl, "Line %d: the index of target cacheline in inflate room is %d\n", __LINE__, static_cast<unsigned int>(loc));

    assert(loc < valid_inflate_num);

    uint64_t prevSize = (((origin_size + 0x3F) >> 6) << 6) + (loc + 1) * 64;
    uint8_t chunkIdx = prevSize / 512;
    uint64_t MPFN = 0;
    for (int u = 0; u < 4; u++) {
        MPFN = (MPFN << 8) | metaData[2 + 4 * chunkIdx + u];
    }

    uint64_t startAddr = (MPFN << 9) | (prevSize % 512);
    uint64_t moveSize = (valid_inflate_num - loc - 1) * 64;

    // printf("the moved size is %d\n", moveSize);

    if (moveSize > 0) {
        std::vector<uint8_t> val(moveSize, 0);
        mem_intr->atomicRead(val.data(), startAddr, moveSize);

        // printf("the moved data is: \n");
        // for (int i = 0; i < val.size(); i++) {
        //    printf("%02x, ", static_cast<unsigned int>(val[i]));
        // }
        // printf("\n");

        mem_intr->atomicWrite(val, startAddr - 64, moveSize);

        for (int i = loc + 1; i < valid_inflate_num; i++) {
            uint8_t cLIdx = getInflateEntry(i, metaData);
            setInflateEntry(i - 1, metaData, cLIdx);
        }
    }
    /* zero out the last space */
    setInflateEntry(valid_inflate_num - 1, metaData, 0);

    valid_inflate_num--;
    metaData[63] = (metaData[63] & 0xE0) | (valid_inflate_num & 0x1F);

    printf("the new metadata is :\n");
    for (int k = 0; k < 64; k++) {
        printf("%02x",static_cast<unsigned>(metaData[k]));

    }
    printf("\n");

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
                   decompress(curCL);
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

   uint8_t chunkNum = (curSize + 0x1FF) % 512;

   for (unsigned int i = 0; i < chunkNum; i++) {
       Addr chunk_addr = 0;
       for (int u = 0; u < 4; u++){   // 4B per MPFN
           chunk_addr = (chunk_addr << 8) | (metaData[2 + 4 * i + u]);
       }
       chunk_addr <<= 9;
       freeList.push_back(chunk_addr);
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

       std::vector<uint8_t> compressedCL = compress(curCL);

       bool isCompressed = false;
       if (isAllZero(curCL)) {
           /* set the mPageBuffer entry to be 0 */
           DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
           setType(newMetaData, i, 0);
       } else {
           if (compressedCL.size() <= 8) {
               /* set the mPageBuffer entry to be 0b1*/
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
   printf("Line %d: the mcache add: pageNum is %d, addr is 0x%lx\n", __LINE__, pageNum, metadata_addr);
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

    uint8_t* page = writeForCompress->getPtr<uint8_t>();
    uint64_t size = 0;

    for (unsigned int i = 0; i < 64; i++) {
        std::vector<uint8_t> curCL(64, 0);
        for (unsigned int j = 0; j < 64; j++) {
            curCL[j] = page[i * 64 + j];
        }

        std::vector<uint8_t> compressedCL = compress(curCL);

        bool isCompressed = false;
        if (isAllZero(curCL)) {
            /* set the mPageBuffer entry to be 0 */
            DPRINTF(MemCtrl, "Line %d, enter the setType\n", __LINE__);
            setType(metaData, i, 0);
        } else {
            if (compressedCL.size() <= 8) {
                /* set the mPageBuffer entry to be 0b1*/
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

    uint8_t chunkNum = (curSize + 0x1FF) % 512;

    for (unsigned int i = 0; i < chunkNum; i++) {
        Addr chunk_addr = 0;
        for (int u = 0; u < 4; u++){   // 4B per MPFN
            chunk_addr = (chunk_addr << 8) | (old_metaData[2 + 4 * i + u]);
        }
        chunk_addr <<= 9;
        freeList.emplace_back(chunk_addr);
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
    // store the size of compressedPage into the control block (using 12 bit)
    metaData[1] = size & (0xFF);
    metaData[0] = metaData[0] | ((size >> 8) & 0xF);

    return metaData;
}

std::vector<uint8_t>
MemCtrl::compress(const std::vector<uint8_t>& cacheLine) {
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
MemCtrl::decompress(std::vector<uint8_t>& data) {
    printf("decompress the data: \n");
    for (int i = 0 ; i < data.size(); i++) {
        if (i % 8 == 0) {
            printf("\n");
        }
        printf("%d ", static_cast<unsigned int>(data[i]));
    }
    printf("\n");
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
    auto targetPkt = waitQueue.find(pkt);
    assert(targetPkt != waitQueue.end());

    bool res = true;
    for (auto it = waitQueue.begin(); it != targetPkt; it++) {
        PacketPtr curPkt = *it;
        if (hasOverLap(curPkt->comprMetaDataMap, pkt->comprMetaDataMap)) {

            DPRINTF(MemCtrl, "collide with the pkt %lx\n", curPkt);
            DPRINTF(MemCtrl, "tick for collided pkt is %lld, tick for pkt is %lld\n", curPkt->comprTick, pkt->comprTick);
            res = false;
            break;
        }
    }
    DPRINTF(MemCtrl, "The pkt is %lx, if is eligible %d\n", pkt, res);
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
            waitQueue.erase(it);
            curPkt->comprMetaDataMap[ppn] = metaData;
            waitQueue.insert(curPkt);
        }
    }
}

void 
MemCtrl::checkForReadyPkt(){
    for (const auto& wait_pkt: waitQueue) {
        if (wait_pkt->comprIsReady) {
            assignToQueue(wait_pkt);
        }
    }
}

void
MemCtrl::prepareMetaData(PacketPtr pkt){
    if (!isEligible(pkt)) {
        return;
    }
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

        printf("iterate the pkt, check if metadata for ppn is available %d\n", ppn);

        if (pkt->comprMetaDataMap.find(ppn) == pkt->comprMetaDataMap.end()) {
            printf("not found in metadata map\n");
            pageCnt++;
            /* step 1.1: calculate the MPA for metadata */
            Addr memory_addr = ppn * 64;
            std::vector<uint8_t> metaData(64, 0);
            printf("cur pageNum is %d\n", pageNum);

            if (pageNum == ppn) {
                printf("hit in pageBuffer\n");
                pkt->comprMetaDataMap[ppn] = mPageBuffer;
                entryCnt++;
            } else {
                if (mcache.isExist(memory_addr)) {
                    printf("hit in metadata cache, ppn %d\n", ppn);
                    pkt->comprMetaDataMap[ppn] = mcache.find(memory_addr);
                    printf("the metadata is :\n");
                    for (int k = 0; k < 64; k++) {
                        printf("%02x",static_cast<unsigned>(pkt->comprMetaDataMap[ppn][k]));

                    }
                    printf("\n");


                    entryCnt++;
                } else {
                    // create a readMetaData pkt and add to read queue
                    PacketPtr readMetaData = new Packet(pkt);
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


} // namespace memory
} // namespace gem5
