/*
 * Copyright (c) 2010-2012,2017-2019 ARM Limited
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
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
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

#include "mem/abstract_mem.hh"

#include <vector>

#include "base/loader/memory_image.hh"
#include "base/loader/object_file.hh"
#include "cpu/thread_context.hh"
#include "debug/LLSC.hh"
#include "debug/MemoryAccess.hh"
#include "mem/packet_access.hh"
#include "sim/system.hh"

namespace gem5
{

    bool isAddressCoveredForAM(uintptr_t start_addr, size_t pkt_size, int type) {
        // uintptr_t target_addr = 0x57180;
        // pkt_size = 4096;
        // start_addr = (start_addr >> 12) << 12;
        // return (target_addr >= start_addr) && (target_addr < start_addr + pkt_size);
        if (type == 0) {
            return true;
        } else {
            return false;
        }
        // return false;
        // return true;
    }

    bool coverageTest(Addr start_addr, Addr target_addr, size_t pkt_size) {
        // target_addr = 0x25cc000;
        // return (target_addr >= start_addr) && (target_addr < start_addr + pkt_size);
        return false;
    }

namespace memory
{

AbstractMemory::AbstractMemory(const Params &p) :
    ClockedObject(p), range(p.range), pmemAddr(NULL),
    backdoor(params().range, nullptr,
             (MemBackdoor::Flags)(p.writeable ?
                 MemBackdoor::Readable | MemBackdoor::Writeable :
                 MemBackdoor::Readable)),
    confTableReported(p.conf_table_reported), inAddrMap(p.in_addr_map),
    kvmMap(p.kvm_map), writeable(p.writeable), collectStats(p.collect_stats),
    _system(NULL), stats(*this)
{
    panic_if(!range.valid() || !range.size(),
             "Memory range %s must be valid with non-zero size.",
             range.to_string());
}

void
AbstractMemory::initState()
{
    ClockedObject::initState();

    const auto &file = params().image_file;
    if (file == "")
        return;

    auto *object = loader::createObjectFile(file, true);
    fatal_if(!object, "%s: Could not load %s.", name(), file);

    loader::debugSymbolTable.insert(*object->symtab().globals());
    loader::MemoryImage image = object->buildImage();

    AddrRange image_range(image.minAddr(), image.maxAddr());
    if (!range.contains(image_range.start())) {
        warn("%s: Moving image from %s to memory address range %s.",
                name(), image_range.to_string(), range.to_string());
        image = image.offset(range.start());
        image_range = AddrRange(image.minAddr(), image.maxAddr());
    }
    panic_if(!image_range.isSubset(range), "%s: memory image %s doesn't fit.",
             name(), file);

    PortProxy proxy([this](PacketPtr pkt) { functionalAccess(pkt); },
                    system()->cacheLineSize());

    panic_if(!image.write(proxy), "%s: Unable to write image.");
}

void
AbstractMemory::setBackingStore(uint8_t* pmem_addr)
{
    // If there was an existing backdoor, let everybody know it's going away.
    if (backdoor.ptr())
        backdoor.invalidate();

    // The back door can't handle interleaved memory.
    backdoor.ptr(range.interleaved() ? nullptr : pmem_addr);

    pmemAddr = pmem_addr;
}

AbstractMemory::MemStats::MemStats(AbstractMemory &_mem)
    : statistics::Group(&_mem), mem(_mem),
    ADD_STAT(bytesRead, statistics::units::Byte::get(),
             "Number of bytes read from this memory"),
    ADD_STAT(bytesInstRead, statistics::units::Byte::get(),
             "Number of instructions bytes read from this memory"),
    ADD_STAT(bytesWritten, statistics::units::Byte::get(),
             "Number of bytes written to this memory"),
    ADD_STAT(numReads, statistics::units::Count::get(),
             "Number of read requests responded to by this memory"),
    ADD_STAT(numWrites, statistics::units::Count::get(),
             "Number of write requests responded to by this memory"),
    ADD_STAT(numOther, statistics::units::Count::get(),
             "Number of other requests responded to by this memory"),
    ADD_STAT(bwRead, statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Total read bandwidth from this memory"),
    ADD_STAT(bwInstRead,
             statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Instruction read bandwidth from this memory"),
    ADD_STAT(bwWrite, statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Write bandwidth from this memory"),
    ADD_STAT(bwTotal, statistics::units::Rate<
                statistics::units::Byte, statistics::units::Second>::get(),
             "Total bandwidth to/from this memory")
{
}

void
AbstractMemory::MemStats::regStats()
{
    using namespace statistics;

    statistics::Group::regStats();

    System *sys = mem.system();
    assert(sys);
    const auto max_requestors = sys->maxRequestors();

    bytesRead
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        bytesRead.subname(i, sys->getRequestorName(i));
    }

    bytesInstRead
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        bytesInstRead.subname(i, sys->getRequestorName(i));
    }

    bytesWritten
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        bytesWritten.subname(i, sys->getRequestorName(i));
    }

    numReads
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numReads.subname(i, sys->getRequestorName(i));
    }

    numWrites
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numWrites.subname(i, sys->getRequestorName(i));
    }

    numOther
        .init(max_requestors)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        numOther.subname(i, sys->getRequestorName(i));
    }

    bwRead
        .precision(0)
        .prereq(bytesRead)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        bwRead.subname(i, sys->getRequestorName(i));
    }

    bwInstRead
        .precision(0)
        .prereq(bytesInstRead)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        bwInstRead.subname(i, sys->getRequestorName(i));
    }

    bwWrite
        .precision(0)
        .prereq(bytesWritten)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        bwWrite.subname(i, sys->getRequestorName(i));
    }

    bwTotal
        .precision(0)
        .prereq(bwTotal)
        .flags(total | nozero | nonan)
        ;
    for (int i = 0; i < max_requestors; i++) {
        bwTotal.subname(i, sys->getRequestorName(i));
    }

    bwRead = bytesRead / simSeconds;
    bwInstRead = bytesInstRead / simSeconds;
    bwWrite = bytesWritten / simSeconds;
    bwTotal = (bytesRead + bytesWritten) / simSeconds;
}

AddrRange
AbstractMemory::getAddrRange() const
{
    return range;
}

// Add load-locked to tracking list.  Should only be called if the
// operation is a load and the LLSC flag is set.
void
AbstractMemory::trackLoadLocked(PacketPtr pkt)
{
    const RequestPtr &req = pkt->req;
    Addr paddr = LockedAddr::mask(req->getPaddr());

    // first we check if we already have a locked addr for this
    // xc.  Since each xc only gets one, we just update the
    // existing record with the new address.
    std::list<LockedAddr>::iterator i;

    for (i = lockedAddrList.begin(); i != lockedAddrList.end(); ++i) {
        if (i->matchesContext(req)) {
            DPRINTF(LLSC, "Modifying lock record: context %d addr %#x\n",
                    req->contextId(), paddr);
            i->addr = paddr;
            return;
        }
    }

    // no record for this xc: need to allocate a new one
    DPRINTF(LLSC, "Adding lock record: context %d addr %#x\n",
            req->contextId(), paddr);
    lockedAddrList.push_front(LockedAddr(req));
    backdoor.invalidate();
}


// Called on *writes* only... both regular stores and
// store-conditional operations.  Check for conventional stores which
// conflict with locked addresses, and for success/failure of store
// conditionals.
bool
AbstractMemory::checkLockedAddrList(PacketPtr pkt)
{
    const RequestPtr &req = pkt->req;
    Addr paddr = LockedAddr::mask(req->getPaddr());
    bool isLLSC = pkt->isLLSC();

    // Initialize return value.  Non-conditional stores always
    // succeed.  Assume conditional stores will fail until proven
    // otherwise.
    bool allowStore = !isLLSC;

    // Iterate over list.  Note that there could be multiple matching records,
    // as more than one context could have done a load locked to this location.
    // Only remove records when we succeed in finding a record for (xc, addr);
    // then, remove all records with this address.  Failed store-conditionals do
    // not blow unrelated reservations.
    std::list<LockedAddr>::iterator i = lockedAddrList.begin();

    if (isLLSC) {
        while (i != lockedAddrList.end()) {
            if (i->addr == paddr && i->matchesContext(req)) {
                // it's a store conditional, and as far as the memory system can
                // tell, the requesting context's lock is still valid.
                DPRINTF(LLSC, "StCond success: context %d addr %#x\n",
                        req->contextId(), paddr);
                allowStore = true;
                break;
            }
            // If we didn't find a match, keep searching!  Someone else may well
            // have a reservation on this line here but we may find ours in just
            // a little while.
            i++;
        }
        req->setExtraData(allowStore ? 1 : 0);
    }
    // LLSCs that succeeded AND non-LLSC stores both fall into here:
    if (allowStore) {
        // We write address paddr.  However, there may be several entries with a
        // reservation on this address (for other contextIds) and they must all
        // be removed.
        i = lockedAddrList.begin();
        while (i != lockedAddrList.end()) {
            if (i->addr == paddr) {
                DPRINTF(LLSC, "Erasing lock record: context %d addr %#x\n",
                        i->contextId, paddr);
                ContextID owner_cid = i->contextId;
                assert(owner_cid != InvalidContextID);
                ContextID requestor_cid = req->hasContextId() ?
                                           req->contextId() :
                                           InvalidContextID;
                if (owner_cid != requestor_cid) {
                    ThreadContext* ctx = system()->threads[owner_cid];
                    ctx->getIsaPtr()->globalClearExclusive();
                }
                i = lockedAddrList.erase(i);
            } else {
                i++;
            }
        }
    }

    return allowStore;
}

#if TRACING_ON
static inline void
tracePacket(System *sys, const char *label, PacketPtr pkt)
{
    int size = pkt->getSize();
    if (size == 1 || size == 2 || size == 4 || size == 8) {
        ByteOrder byte_order = sys->getGuestByteOrder();
        DPRINTF(MemoryAccess, "%s from %s of size %i on address %#x data "
                "%#x %c\n", label, sys->getRequestorName(pkt->req->
                requestorId()), size, pkt->getAddr(),
                pkt->getUintX(byte_order),
                pkt->req->isUncacheable() ? 'U' : 'C');
        return;
    }
    DPRINTF(MemoryAccess, "%s from %s of size %i on address %#x %c\n",
            label, sys->getRequestorName(pkt->req->requestorId()),
            size, pkt->getAddr(), pkt->req->isUncacheable() ? 'U' : 'C');
    DDUMP(MemoryAccess, pkt->getConstPtr<uint8_t>(), pkt->getSize());
}

#   define TRACE_PACKET(A) tracePacket(system(), A, pkt)
#else
#   define TRACE_PACKET(A)
#endif

void
AbstractMemory::access(PacketPtr pkt)
{
    if (pkt->cacheResponding()) {
        DPRINTF(MemoryAccess, "Cache responding to %#llx: not responding\n",
                pkt->getAddr());
        return;
    }

    if (pkt->cmd == MemCmd::CleanEvict || pkt->cmd == MemCmd::WritebackClean) {
        DPRINTF(MemoryAccess, "CleanEvict  on 0x%x: not responding\n",
                pkt->getAddr());
      return;
    }

    assert(pkt->getAddrRange().isSubset(range));

    uint8_t *host_addr = toHostAddr(pkt->getAddr());

    if (pkt->cmd == MemCmd::SwapReq) {
        if (pkt->isAtomicOp()) {
            if (pmemAddr) {
                pkt->setData(host_addr);
                (*(pkt->getAtomicOp()))(host_addr);
            }
        } else {
            std::vector<uint8_t> overwrite_val(pkt->getSize());
            uint64_t condition_val64;
            uint32_t condition_val32;

            panic_if(!pmemAddr, "Swap only works if there is real memory " \
                     "(i.e. null=False)");

            bool overwrite_mem = true;
            // keep a copy of our possible write value, and copy what is at the
            // memory address into the packet
            pkt->writeData(&overwrite_val[0]);
            pkt->setData(host_addr);

            if (pkt->req->isCondSwap()) {
                if (pkt->getSize() == sizeof(uint64_t)) {
                    condition_val64 = pkt->req->getExtraData();
                    overwrite_mem = !std::memcmp(&condition_val64, host_addr,
                                                 sizeof(uint64_t));
                } else if (pkt->getSize() == sizeof(uint32_t)) {
                    condition_val32 = (uint32_t)pkt->req->getExtraData();
                    overwrite_mem = !std::memcmp(&condition_val32, host_addr,
                                                 sizeof(uint32_t));
                } else
                    panic("Invalid size for conditional read/write\n");
            }

            if (overwrite_mem)
                std::memcpy(host_addr, &overwrite_val[0], pkt->getSize());

            assert(!pkt->req->isInstFetch());
            TRACE_PACKET("Read/Write");
            if (collectStats) {
                stats.numOther[pkt->req->requestorId()]++;
            }
        }
    } else if (pkt->isRead()) {
        assert(!pkt->isWrite());
        if (pkt->isLLSC()) {
            assert(!pkt->fromCache());
            // if the packet is not coming from a cache then we have
            // to do the LL/SC tracking here
            trackLoadLocked(pkt);
        }
        if (pmemAddr) {
            pkt->setData(host_addr);
        }

        if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0)) {
            // printf("Atomic read marker: ");
            printf("marker:%lx\n", pkt);
            printf("Timing read marker: ");
            uint8_t* start = pkt->getPtr<uint8_t>();
            for (int ts = 0; ts < pkt->getSize(); ts++) {
               printf("%02x ", static_cast<unsigned int>(start[ts]));
            }
            printf("\n");
            fflush(stdout);
        }

        TRACE_PACKET(pkt->req->isInstFetch() ? "IFetch" : "Read");
        if (collectStats) {
            stats.numReads[pkt->req->requestorId()]++;
            stats.bytesRead[pkt->req->requestorId()] += pkt->getSize();
            if (pkt->req->isInstFetch()) {
                stats.bytesInstRead[pkt->req->requestorId()] += pkt->getSize();
            }
        }
    } else if (pkt->isInvalidate() || pkt->isClean()) {
        assert(!pkt->isWrite());
        // in a fastmem system invalidating and/or cleaning packets
        // can be seen due to cache maintenance requests

        // no need to do anything
    } else if (pkt->isWrite()) {
        if (writeOK(pkt)) {
            if (pmemAddr) {

                if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0)) {
                    // printf("Atomic write marker: ");
                    printf("marker:%lx\n", pkt);
                    printf("Timing write marker: ");
                    uint8_t* start = pkt->getPtr<uint8_t>();
                    for (int ts = 0; ts < pkt->getSize(); ts++) {
                    printf("%02x ", static_cast<unsigned int>(start[ts]));
                    }
                    printf("\n");
                    fflush(stdout);
                }

                pkt->writeData(host_addr);
                DPRINTF(MemoryAccess, "%s write due to %s\n",
                        __func__, pkt->print());
            }
            assert(!pkt->req->isInstFetch());
            TRACE_PACKET("Write");
            if (collectStats) {
                stats.numWrites[pkt->req->requestorId()]++;
                stats.bytesWritten[pkt->req->requestorId()] += pkt->getSize();
            }
        }
    } else {
        panic("Unexpected packet %s", pkt->print());
    }

    if (pkt->needsResponse()) {
        pkt->makeResponse();
    }
}

void
AbstractMemory::accessForDyL(PacketPtr pkt)
{
    if (pkt->cacheResponding()) {
        DPRINTF(MemoryAccess, "Cache responding to %#llx: not responding\n",
                pkt->getAddr());
        return;
    }

    if (pkt->cmd == MemCmd::CleanEvict || pkt->cmd == MemCmd::WritebackClean) {
        DPRINTF(MemoryAccess, "CleanEvict  on 0x%x: not responding\n",
                pkt->getAddr());
      return;
    }

    assert(pkt->getAddrRange().isSubset(range));

    if (coverageTest(pkt->getAddr(), 0x25cc000, pkt->getSize())) {
        printf("acess For DyL\n");
        printf("recv Timing: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
        if (pkt->isWrite()) {
            uint8_t* start = pkt->getPtr<uint8_t>();
            for (int ts = 0; ts < pkt->getSize(); ts++) {
                printf("%02x ", static_cast<unsigned int>(start[ts]));
            }
            printf("\n");
            fflush(stdout);
        }
    }

    uint8_t *host_addr = toHostAddr(pkt->getAddr());

    if (pkt->cmd == MemCmd::SwapReq) {
        printf("have you enter this: swap req\n");
        if (pkt->isAtomicOp()) {
            if (pmemAddr) {
                pkt->setData(host_addr);
                (*(pkt->getAtomicOp()))(host_addr);
            }
        } else {
            std::vector<uint8_t> overwrite_val(pkt->getSize());
            uint64_t condition_val64;
            uint32_t condition_val32;

            panic_if(!pmemAddr, "Swap only works if there is real memory " \
                     "(i.e. null=False)");

            bool overwrite_mem = true;
            // keep a copy of our possible write value, and copy what is at the
            // memory address into the packet
            pkt->writeData(&overwrite_val[0]);
            pkt->setData(host_addr);

            if (pkt->req->isCondSwap()) {
                if (pkt->getSize() == sizeof(uint64_t)) {
                    condition_val64 = pkt->req->getExtraData();
                    overwrite_mem = !std::memcmp(&condition_val64, host_addr,
                                                 sizeof(uint64_t));
                } else if (pkt->getSize() == sizeof(uint32_t)) {
                    condition_val32 = (uint32_t)pkt->req->getExtraData();
                    overwrite_mem = !std::memcmp(&condition_val32, host_addr,
                                                 sizeof(uint32_t));
                } else
                    panic("Invalid size for conditional read/write\n");
            }

            if (overwrite_mem)
                std::memcpy(host_addr, &overwrite_val[0], pkt->getSize());

            assert(!pkt->req->isInstFetch());
            TRACE_PACKET("Read/Write");
            if (collectStats) {
                stats.numOther[pkt->req->requestorId()]++;
            }
        }
    } else if (pkt->isRead()) {
        assert(!pkt->isWrite());
        if (pkt->isLLSC()) {
            assert(!pkt->fromCache());
            // if the packet is not coming from a cache then we have
            // to do the LL/SC tracking here
            trackLoadLocked(pkt);
        }
        if (pmemAddr) {
            pkt->setData(host_addr);
        }

        if (isAddressCoveredForAM(pkt->DyLBackup, pkt->getSize(), 0)) {
            // printf("Atomic read marker: ");
            printf("marker:%lx\n", pkt);
            printf("Timing read marker: ");
            uint8_t* start = pkt->getPtr<uint8_t>();
            for (int ts = 0; ts < pkt->getSize(); ts++) {
               printf("%02x ", static_cast<unsigned int>(start[ts]));
            }
            printf("\n");
            fflush(stdout);
        }

        TRACE_PACKET(pkt->req->isInstFetch() ? "IFetch" : "Read");
        if (collectStats) {
            stats.numReads[pkt->req->requestorId()]++;
            stats.bytesRead[pkt->req->requestorId()] += pkt->getSize();
            if (pkt->req->isInstFetch()) {
                stats.bytesInstRead[pkt->req->requestorId()] += pkt->getSize();
            }
        }
    } else if (pkt->isInvalidate() || pkt->isClean()) {
        assert(!pkt->isWrite());
        // in a fastmem system invalidating and/or cleaning packets
        // can be seen due to cache maintenance requests

        // no need to do anything
    } else if (pkt->isWrite()) {
        if (writeOK(pkt)) {
            if (pmemAddr) {

                if (isAddressCoveredForAM(pkt->DyLBackup, pkt->getSize(), 0)) {
                    // printf("Atomic write marker: ");
                    printf("marker:%lx\n", pkt);
                    printf("Timing write marker: ");
                    uint8_t* start = pkt->getPtr<uint8_t>();
                    for (int ts = 0; ts < pkt->getSize(); ts++) {
                        printf("%02x ", static_cast<unsigned int>(start[ts]));
                    }
                    printf("\n");
                    fflush(stdout);
                }

                pkt->writeData(host_addr);
                DPRINTF(MemoryAccess, "%s write due to %s\n",
                        __func__, pkt->print());
            }
            assert(!pkt->req->isInstFetch());
            TRACE_PACKET("Write");
            if (collectStats) {
                stats.numWrites[pkt->req->requestorId()]++;
                stats.bytesWritten[pkt->req->requestorId()] += pkt->getSize();
            }
        }
    } else {
        panic("Unexpected packet %s", pkt->print());
    }

    pkt->setAddr(pkt->DyLBackup);

    if (pkt->needsResponse()) {
        pkt->makeResponse();
    }
}

void
AbstractMemory::accessForCompr(PacketPtr pkt, uint64_t burst_size, uint64_t pageNum, std::vector<uint8_t>& pageBuffer, std::vector<uint8_t>& mPageBuffer) {
    // printf("enter the accessForCompr function: the pkt address is 0x%lx\n", pkt);

    assert(pkt->comprBackup);
    PacketPtr real_recv_pkt = pkt->comprBackup;

    if (real_recv_pkt->cacheResponding()) {
        DPRINTF(MemoryAccess, "Cache responding to %#llx: not responding\n",
                real_recv_pkt->getAddr());
        return;
    }

    if (real_recv_pkt->cmd == MemCmd::CleanEvict || real_recv_pkt->cmd == MemCmd::WritebackClean) {
        DPRINTF(MemoryAccess, "CleanEvict  on 0x%x: not responding\n",
                real_recv_pkt->getAddr());
      return;
    }

    assert(pkt->getAddrRange().isSubset(range));

    /* prepare the auxiliary information */

    std::vector<uint8_t> sizeMap = {0, 8, 32, 64};

    std::unordered_map<uint64_t, std::vector<uint8_t>> metaDataMap = pkt->comprMetaDataMap;

    /* get initial information */
    unsigned size = pkt->getSize();
    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;

    if (pkt->cmd == MemCmd::SwapReq) {
        assert(size == 64);
        assert(base_addr % 64 == 0);

        uint64_t ppn = addr >> 12;

        assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */

        std::vector<uint8_t> metaData = metaDataMap[ppn];

        uint8_t cacheLineIdx = (addr >> 6) & 0x3F;
        uint8_t type = getType(metaData, cacheLineIdx);

        std::vector<uint8_t> cacheLine(64, 0);

        /* the pkt should be only covered by one cacheLine */

        memcpy(cacheLine.data(), pkt->getPtr<uint8_t>(), 64);

        /* compress the cacheline */
        std::vector<uint8_t> compressed = compress(cacheLine);

        if (compressed.size() > 32) {
            assert(compressed.size() == 64);
        }

        if (pageNum == ppn) {
            assert(mPageBuffer.size() == metaData.size());
            for (int temp = 0; temp < metaData.size(); temp++) {
                assert(mPageBuffer[temp] == metaData[temp]);
            }
            uint8_t* pageBuffer_addr = pageBuffer.data() + cacheLineIdx * 64;

            memcpy(pageBuffer_addr, compressed.data(), sizeMap[type]);

        } else {
            std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);
            bool inInflate = cLStatus.first;
            Addr real_addr = cLStatus.second;

            if (inInflate) {
                type = 0b11;
            }

            if (type != 0) {
                if (!inInflate) {
                    assert(compressed.size() <= sizeMap[type]);
                }

                uint8_t* host_addr = toHostAddr(real_addr);

                if (pmemAddr) {
                    if (type == 0b11) {
                        std::memcpy(host_addr, cacheLine.data(), cacheLine.size());
                    } else {
                        std::memcpy(host_addr, compressed.data(), compressed.size());
                    }
                }
            }
        }
        if (!real_recv_pkt->isAtomicOp()) {
            assert(!pkt->req->isInstFetch());
            TRACE_PACKET("Read/Write");
            if (collectStats) {
                stats.numOther[pkt->req->requestorId()]++;
            }
        }
    } else if (pkt->isRead()) {
        assert(!pkt->isWrite());
        if (real_recv_pkt->isLLSC()) {
            assert(!real_recv_pkt->fromCache());
            // if the packet is not coming from a cache then we have
            // to do the LL/SC tracking here
            // printf("Abstract Memory Line %d: have to track load locked\n", __LINE__);
            trackLoadLocked(real_recv_pkt);
        }

        for (unsigned int i = 0; i < pkt_count; i++) {
            uint64_t ppn = addr >> 12;
            uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

            assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */

            std::vector<uint8_t> metaData = metaDataMap[ppn];

            // printf("Abstract Memory Line %d: the first byte of metaData is %X\n", __LINE__, static_cast<unsigned int>(metaData[0]));

            // printf("Abstract Memory Line %d: the ppn is %lld, the pageNum is %lld\n", __LINE__, ppn, pageNum);

            if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 1)) {
                printf("the pageNum is %d\n", ppn);
                printf("the metadata is :\n");\
                for (int k = 0; k < 64; k++) {
                    printf("%02x",static_cast<unsigned>(metaData[k]));

                }
                printf("\n");
            }


            uint8_t type = getType(metaData, cacheLineIdx);
            std::vector<uint8_t> cacheLine(64, 0);

            if (pageNum == ppn) {
                if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 1)) {
                   printf("pageBuffer hit\n");
                }
                assert(mPageBuffer.size() == metaData.size());
                for (int temp = 0; temp < metaData.size(); temp++) {
                    assert(mPageBuffer[temp] == metaData[temp]);
                }
                if (type != 0) {
                    uint8_t* pageBuffer_addr = pageBuffer.data() + cacheLineIdx * 64;
                    std::memcpy(cacheLine.data(), pageBuffer_addr, sizeMap[type]);
                }
                restoreData(cacheLine, type);
                assert(cacheLine.size() == 64);

                uint8_t loc = addr & 0x3F;
                uint64_t ofs = addr - pkt->getAddr();
                size_t readSize = std::min(pkt->getSize() - ofs, 64UL - loc);
                // printf("Abstract Memory Line %d: start set data for MC, ofs is %lld, loc is %d, size is %ld\n", __LINE__, ofs, loc, size);
                pkt->setDataForMC(cacheLine.data() + loc, ofs, readSize);

            } else {
                std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);

                bool inInflate = cLStatus.first;
                Addr real_addr = cLStatus.second;

                // printf("Abstract Memory Line %d: get the real addr 0x%lx\n", __LINE__, real_addr);

               assert(pmemAddr);
               uint8_t *host_addr = toHostAddr(real_addr);

               if (inInflate) {
                   type = 0b11;  // autually uncompressed;
                   std::memcpy(cacheLine.data(), host_addr, 64);
               } else {
                   if (type != 0) {
                    //    printf("Abstract Memory Line %d: type != 0, type is %d\n", __LINE__, static_cast<unsigned int>(type));

                    //    printf("Abstract Memory Line %d: the host addr is 0x%llx\n", __LINE__, (uint64_t)host_addr);

                       std::memcpy(cacheLine.data(), host_addr, sizeMap[type]);

                    //    printf("Abstract Memory Line %d: finish read the compressed data\n", __LINE__);
                    //    printf("the read compressed data is :\n");
                    //    for (int ts = 0; ts < sizeMap[type]; ts++) {
                    //        if (ts % 8 == 0) {
                    //            printf("\n");
                    //        }
                    //        printf("%02x ", static_cast<unsigned int>(cacheLine[ts]));
                    //    }
                    //    printf("\n");

                   }
               }

                restoreData(cacheLine, type);
                assert(cacheLine.size() == 64);

                if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 1)) {
                    printf("the real address is 0x%lx\n", reinterpret_cast<uint64_t>(real_addr));
                    printf("Abstract Memory Line %d: finish restore the data\n", __LINE__);
                    for (int u = 0; u < 8; u++) {
                       for (int v = 0; v < 8; v++) {
                           printf("%02x ", static_cast<unsigned int>(cacheLine[u * 8 + v]));
                       }
                       printf("\n");
                    }
                    printf("\n");
                }

                uint8_t loc = addr & 0x3F;
                uint64_t ofs = addr - pkt->getAddr();
                size_t size = std::min(pkt->getSize() - ofs, 64UL - loc);
                // printf("Abstract Memory Line %d: start set data for MC, ofs is %lld, loc is %d, size is %ld\n", __LINE__, ofs, loc, size);
                pkt->setDataForMC(cacheLine.data() + loc, ofs, size);
                // printf("Abstract Memory Line %d: finish set data for MC\n", __LINE__);
            }

            addr = (addr | (burst_size - 1)) + 1;
        }

        // printf("Atomic read marker: ");
        // uint8_t* start = pkt->getPtr<uint8_t>();
        // for (int ts = 0; ts < pkt->getSize(); ts++) {
        //    printf("%02x ", static_cast<unsigned int>(start[ts]));
        // }
        // printf("\n");

        if (pkt->getPType() == 0x2 && isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0)) {
            printf("marker:%lx\n", pkt);
            printf("Timing read marker: ");
            uint8_t* start = pkt->getPtr<uint8_t>();
            for (int ts = 0; ts < pkt->getSize(); ts++) {
                printf("%02x ", static_cast<unsigned int>(start[ts]));
            }
            printf("\n");
            fflush(stdout);
        }

        TRACE_PACKET(real_recv_pkt->req->isInstFetch() ? "IFetch" : "Read");
        if (collectStats) {
            stats.numReads[real_recv_pkt->req->requestorId()]++;
            stats.bytesRead[real_recv_pkt->req->requestorId()] += real_recv_pkt->getSize();
            if (real_recv_pkt->req->isInstFetch()) {
                stats.bytesInstRead[real_recv_pkt->req->requestorId()] += real_recv_pkt->getSize();
            }
        }

    } else if (pkt->isInvalidate() || pkt->isClean()) {
        assert(!pkt->isWrite());
        // in a fastmem system invalidating and/or cleaning packets
        // can be seen due to cache maintenance requests

        // no need to do anything
    } else if (pkt->isWrite()) {
        if (writeOK(real_recv_pkt) || pkt->getPType() == 0x20) {
            /* assert the pkt should be burst_size/cacheline aligned */
            assert(offset == 0);
            assert((size & (burst_size - 1)) == 0);
            assert(pkt_count == (size / burst_size));

            // printf("Atomic write marker: ");
            // uint8_t* start = real_recv_pkt->getPtr<uint8_t>();
            // for (int ts = 0; ts < real_recv_pkt->getSize(); ts++) {
            //    printf("%02x ", static_cast<unsigned int>(start[ts]));
            // }
            // printf("\n");

            if (pkt->getPType() == 0x2 && isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0)) {
                printf("marker:%lx\n", pkt);
                printf("Timing write marker: ");
                uint8_t* start = real_recv_pkt->getPtr<uint8_t>();
                for (int ts = 0; ts < real_recv_pkt->getSize(); ts++) {
                   printf("%02x ", static_cast<unsigned int>(start[ts]));
                }
                printf("\n");
                fflush(stdout);
            }

            for (unsigned int i = 0; i < pkt_count; i++) {
                uint64_t ppn = addr >> 12;
                uint8_t cacheLineIdx = (addr >> 6) & 0x3F;


                assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */
                std::vector<uint8_t> metaData = metaDataMap[ppn];


                if(isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 1)) {
                    printf("the ppn %d metadata is: \n", ppn);
                    for (int k = 0; k < 64; k++) {
                        printf("%02x",static_cast<unsigned>(metaData[k]));

                    }
                    printf("\n");
                }

                uint8_t type = getType(metaData, cacheLineIdx);

                std::vector<uint8_t> cacheLine(64, 0);

                uint64_t ofs = addr - pkt->getAddr();

                pkt->writeDataForMC(cacheLine.data(), ofs, 64);

                if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 1)) {
                    printf("Abstract Memory Line %d: write the pkt data\n", __LINE__);
                    for (int u = 0; u < 8; u++) {
                       for (int v = 0; v < 8; v++) {
                           printf("%02x ", static_cast<unsigned int>(cacheLine[u * 8 + v]));
                       }
                       printf("\n");
                    }
                    printf("\n");
                }


                /* compress the cacheline */
                std::vector<uint8_t> compressed = compress(cacheLine);

                if (compressed.size() > 32) {
                   assert(compressed.size() == 64);
                }
                // printf("the pageNum is %ld\n", ppn);
                // printf("the metadata is :\n");
                // for (int k = 0; k < 64; k++) {
                //     printf("%02x",static_cast<unsigned>(metaData[k]));

                // }
                // printf("\n");

                // printf("cacheline idx is %d\n", cacheLineIdx);

                if (pageNum == ppn) {
                    if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 1)) {
                        printf("pageNum == ppn, pageBuffer hit, pageNum is %ld\n", pageNum);
                    }
                    assert(mPageBuffer.size() == metaData.size());
                    // printf("mPageBuffer: \n");
                    // for (int k = 0; k < 64; k++) {
                    //     printf("%02x",static_cast<unsigned>(mPageBuffer[k]));

                    // }
                    // printf("\n");
                    // printf("metadata: \n");
                    // for (int k = 0; k < 64; k++) {
                    //     printf("%02x",static_cast<unsigned>(metaData[k]));

                    // }
                    // printf("\n");
                    for (int temp = 0; temp < metaData.size(); temp++) {
                        assert(mPageBuffer[temp] == metaData[temp]);
                    }
                    uint8_t* pageBuffer_addr = pageBuffer.data() + cacheLineIdx * 64;

                    memcpy(pageBuffer_addr, compressed.data(), sizeMap[type]);
                } else {
                    std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);

                    bool inInflate = cLStatus.first;
                    Addr real_addr = cLStatus.second;

                    if (!inInflate && type != 0) {
                        // printf("Abstract Memory Line %d: get the real addr 0x%lx\n", __LINE__, real_addr);
                    }

                    if (inInflate) {
                        type = 0b11;
                    }

                    if (type != 0) {
                        if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 1)) {
                            printf("if inInflate: %d\n", inInflate);
                        }

                        if (!inInflate) {
                            // printf("the compressed size is %d\n", compressed.size());
                            // printf("the space is %d\n", sizeMap[type]);
                            assert(compressed.size() <= sizeMap[type]);
                        }

                        uint8_t* host_addr = toHostAddr(real_addr);

                        if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 1)) {
                            // printf("the host address is 0x%llx:\n", reinterpret_cast<uint64_t>(host_addr));
                            printf("the real address is 0x%lx\n", reinterpret_cast<uint64_t>(real_addr));
                        }


                        uint64_t test_size = 0;
                        if (pmemAddr) {
                           if (type == 3) {
                            //    printf("type = 3 \n");
                            //    printf("host address %lx\n", reinterpret_cast<uint64_t>(host_addr));
                               std::memcpy(host_addr, cacheLine.data(), cacheLine.size());
                               test_size = cacheLine.size();
                           } else {
                               std::memcpy(host_addr, compressed.data(), compressed.size());
                               test_size = compressed.size();
                           }
                        }
                    }
                }
                Addr old_addr = addr;
                addr = (addr | (burst_size - 1)) + 1;
                assert(addr == old_addr + burst_size);
            }

            DPRINTF(MemoryAccess, "%s write due to %s\n",
                __func__, real_recv_pkt->print());
            assert(!real_recv_pkt->req->isInstFetch());
            TRACE_PACKET("Write");
            if (collectStats) {
                stats.numWrites[real_recv_pkt->req->requestorId()]++;
                stats.bytesWritten[real_recv_pkt->req->requestorId()] += real_recv_pkt->getSize();
            }
        }
    } else {
        panic("Unexpected packet %s", pkt->print());
    }

    if (real_recv_pkt->getPType() == 0x1 && real_recv_pkt->needsResponse()) {
        real_recv_pkt->makeResponse();
    }
}

void
AbstractMemory::accessForNew(PacketPtr pkt, uint8_t mode) {
    std::vector<uint8_t> sizeMap = {1, 22, 44, 64};
    if (mode == 0) {
        /* pkt is aux_pkt */
        PacketPtr real_recv_pkt = pkt->new_backup;
        if (real_recv_pkt->cacheResponding()) {
            DPRINTF(MemoryAccess, "Cache responding to %#llx: not responding\n",
                    real_recv_pkt->getAddr());
            return;
        }

        if (real_recv_pkt->cmd == MemCmd::CleanEvict || real_recv_pkt->cmd == MemCmd::WritebackClean) {
            DPRINTF(MemoryAccess, "CleanEvict  on 0x%x: not responding\n",
                    real_recv_pkt->getAddr());
            return;
        }
        if (pkt->cmd == MemCmd::SwapReq) {
            if (pkt->isAtomicOp()) {

            } else {
                assert(!real_recv_pkt->req->isInstFetch());
                TRACE_PACKET("Read/Write");
                if (collectStats) {
                    stats.numOther[real_recv_pkt->req->requestorId()]++;
                }   
            }
        } else if (pkt->isRead()) {
            assert(!pkt->isWrite());
            if (real_recv_pkt->isLLSC()) {
                assert(!real_recv_pkt->fromCache());
                trackLoadLocked(real_recv_pkt);
            }
            if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0)) {
                // printf("Atomic read marker: ");
                printf("marker:%lx\n", pkt);
                printf("Timing read marker: ");
                uint8_t* start = pkt->getPtr<uint8_t>();
                for (int ts = 0; ts < pkt->getSize(); ts++) {
                    printf("%02x ", static_cast<unsigned int>(start[ts]));
                }
                printf("\n");
                fflush(stdout);
            }
            TRACE_PACKET(real_recv_pkt->req->isInstFetch() ? "IFetch" : "Read");
            if (collectStats) {
                stats.numReads[real_recv_pkt->req->requestorId()]++;
                stats.bytesRead[real_recv_pkt->req->requestorId()] += real_recv_pkt->getSize();
                if (real_recv_pkt->req->isInstFetch()) {
                    stats.bytesInstRead[real_recv_pkt->req->requestorId()] += real_recv_pkt->getSize();
                }
            }
        }  else if (pkt->isInvalidate() || pkt->isClean()) {
            assert(!pkt->isWrite());
            // in a fastmem system invalidating and/or cleaning packets
            // can be seen due to cache maintenance requests

            // no need to do anything
        } else if (pkt->isWrite()) {
            if (writeOK(pkt)) {
                if (pmemAddr) {
                    if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0)) {
                        // printf("Atomic write marker: ");
                        printf("marker:%lx\n", pkt);
                        printf("Timing write marker: ");
                        uint8_t* start = pkt->getPtr<uint8_t>();
                        for (int ts = 0; ts < pkt->getSize(); ts++) {
                        printf("%02x ", static_cast<unsigned int>(start[ts]));
                        }
                        printf("\n");
                        fflush(stdout);
                    }
                    
                }
                assert(!real_recv_pkt->req->isInstFetch());
                TRACE_PACKET("Write");
                if (collectStats) {
                    stats.numWrites[real_recv_pkt->req->requestorId()]++;
                    stats.bytesWritten[real_recv_pkt->req->requestorId()] += real_recv_pkt->getSize();
                }
            }
        } else {
            panic("Unexpected packet %s", pkt->print());
        }

        if (real_recv_pkt->needsResponse()) {
            real_recv_pkt->makeResponse();
        }

    } else {
        assert(pkt->getAddrRange().isSubset(range));
        uint8_t *host_addr = toHostAddr(pkt->getAddr());

        if (pkt->cmd == MemCmd::SwapReq) {
            assert(pkt->newPType == 0x04);  // the pkt is sub pkt;
            PacketPtr aux_pkt = pkt->new_backup;
            uint8_t cacheLineIdx = (aux_pkt->getAddr() >> 6) & 0x3F;

            if (pkt->isAtomicOp()) {
                panic("not support yet");
                // if (pmemAddr) {
                //     pkt->setData(host_addr);
                //     (*(pkt->getAtomicOp()))(host_addr);
                // }
            } else {
                std::vector<uint8_t> overwrite_val(pkt->getSize());
                uint64_t condition_val64;
                uint32_t condition_val32;

                panic_if(!pmemAddr, "Swap only works if there is real memory " \
                        "(i.e. null=False)");

                bool overwrite_mem = true;
                // keep a copy of our possible write value, and copy what is at the
                // memory address into the packet
                pkt->writeData(&overwrite_val[0]);

                uint8_t real_size = 64;
                if (pkt->old_type < 0b100) {
                    real_size = sizeMap[pkt->old_type];
                }
                std::vector<uint8_t> data(64, 0);
                uint8_t *old_host_addr = toHostAddr(pkt->old_addr);
                memcpy(data.data(), old_host_addr, real_size); 
                new_restoreDataAM(data, pkt->old_type);
                pkt->setSizeForMC(real_size);
                pkt->allocateForMC();
                pkt->setData(old_host_addr);

                if (aux_pkt->req->isCondSwap()) {
                    uint8_t offset = aux_pkt->getAddr() & 0x3F;
                    if (aux_pkt->getSize() == sizeof(uint64_t)) {
                        condition_val64 = aux_pkt->req->getExtraData();
                        overwrite_mem = !std::memcmp(&condition_val64, data.data() + offset,     
                                                    sizeof(uint64_t));
                    } else if (aux_pkt->getSize() == sizeof(uint32_t)) {
                        condition_val32 = (uint32_t)aux_pkt->req->getExtraData();
                        overwrite_mem = !std::memcmp(&condition_val32, data.data() + offset,
                                                    sizeof(uint32_t));
                    } else
                        panic("Invalid size for conditional read/write\n");
                }

                if (overwrite_mem) {
                    std::memcpy(host_addr, &overwrite_val[0], pkt->getSize());
                } else {
                    // TODO: may have to write the origin data to the overflow place
                    panic("not implement yet");
                }
            }
        } else if (pkt->isRead()) {
            assert(!pkt->isWrite());
            if (pmemAddr) {
                if (pkt->suffixLen == 0) {
                    pkt->setData(host_addr);   
                } else {
                    assert(pkt->newPType == 0x04 || pkt->newPType == 0x08);  // should be sub-pkt;
                    assert(pkt->getSize() > pkt->suffixLen);
                    uint64_t prefixLen = pkt->getSize() - pkt->suffixLen;
                    pkt->setDataForMC(host_addr, 0, prefixLen);
                    uint8_t* host_new_block_addr = toHostAddr(pkt->newBlockAddr);
                    pkt->setDataForMC(host_new_block_addr, prefixLen, pkt->suffixLen);
                }
            }
        } else if (pkt->isInvalidate() || pkt->isClean()) {
            assert(!pkt->isWrite());
            // in a fastmem system invalidating and/or cleaning packets
            // can be seen due to cache maintenance requests

            // no need to do anything
        } else if (pkt->isWrite()) {
            if (writeOK(pkt) || pkt->newPType > 0x04) {
                if (pmemAddr) {
                    // printf("the host address correspond to 0x%lx\n", pkt->getAddr());
                    // printf("pkt->size is %d\n", pkt->getSize());
                    // fflush(stdout);
                    if (pkt->suffixLen == 0) {
                        pkt->writeData(host_addr);   
                    } else {
                        assert(pkt->newPType == 0x04);  // should be sub-pkt;
                        assert(pkt->getSize() > pkt->suffixLen);
                        uint64_t prefixLen = pkt->getSize() - pkt->suffixLen;
                        pkt->writeDataForMC(host_addr, 0, prefixLen);
                        uint8_t* host_new_block_addr = toHostAddr(pkt->newBlockAddr);
                        pkt->writeDataForMC(host_new_block_addr, prefixLen, pkt->suffixLen);
                    }
                    
                }
            }
        } else {
            panic("Unexpected packet %s", pkt->print());
        }
    }
}

void
AbstractMemory::functionalAccess(PacketPtr pkt)
{
    assert(pkt->getAddrRange().isSubset(range));

    uint8_t *host_addr = toHostAddr(pkt->getAddr());

    // if (coverageTest(pkt->getAddr(), 0x25cc000, pkt->getSize())) {
    //     printf("acess For DyL\n");
    //     printf("recv Timing: %s 0x%x\n", pkt->cmdString().c_str(), pkt->getAddr());
    //     if (pkt->isWrite()) {
    //         uint8_t* start = pkt->getPtr<uint8_t>();
    //         for (int ts = 0; ts < pkt->getSize(); ts++) {
    //             printf("%02x ", static_cast<unsigned int>(start[ts]));
    //         }
    //         printf("\n");
    //         fflush(stdout);
    //     }
    // }

    if (pkt->isRead()) {
        if (pmemAddr) {
            pkt->setData(host_addr);
        }


        uint8_t* test_start = pkt->getPtr<uint8_t>();
        if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0)) {
            printf("Functional read: ");
            for (int u = 0; u < pkt->getSize(); u++) {
                printf("%02x ", static_cast<unsigned int>(test_start[u]));
            }
            printf("\n");
        }

        TRACE_PACKET("Read");
        pkt->makeResponse();
    } else if (pkt->isWrite()) {
        if (pmemAddr) {
            pkt->writeData(host_addr);
        }

        if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0)) {
            printf("Functional write: ");
            uint8_t* test_start = pkt->getPtr<uint8_t>();
            for (int i = 0; i < pkt->getSize(); i++) {
                printf("%02x ", static_cast<unsigned int>(test_start[i]));
            }
            printf("\n");
        }

        TRACE_PACKET("Write");
        pkt->makeResponse();
    } else if (pkt->isPrint()) {
        Packet::PrintReqState *prs =
            dynamic_cast<Packet::PrintReqState*>(pkt->senderState);
        assert(prs);
        // Need to call printLabels() explicitly since we're not going
        // through printObj().
        prs->printLabels();
        // Right now we just print the single byte at the specified address.
        ccprintf(prs->os, "%s%#x\n", prs->curPrefix(), *host_addr);
    } else {
        panic("AbstractMemory: unimplemented functional command %s",
              pkt->cmdString());
    }
}

/*
    mode = 0: not blocked
    mode = 1: blocked and only track the status
    mode = 2: the original pkt blocked and become unblocked, pkt is actually the auxPKt and perform actual access to the memory
*/

void
AbstractMemory::functionalAccessForDyL(PacketPtr pkt, int mode) {
    assert(pkt->getAddrRange().isSubset(range));

    uint8_t *host_addr = toHostAddr(pkt->getAddr());

    if (mode != 2) {
        pkt->setAddr(pkt->DyLBackup);
    }

    if (pkt->isRead()) {
        if (pmemAddr && (mode == 0)) {
            pkt->setData(host_addr);
        }

        if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0) && (mode != 2)) {
            uint8_t* test_start = pkt->getPtr<uint8_t>();
            printf("Functional read: ");
            for (int u = 0; u < pkt->getSize(); u++) {
                printf("%02x ", static_cast<unsigned int>(test_start[u]));
            }
            printf("\n");
        }

        if (mode != 2) {
            TRACE_PACKET("Read");
            pkt->makeResponse();
        }
    } else if (pkt->isWrite()) {
        if (pmemAddr && (mode != 1)) {
            pkt->writeData(host_addr);
        }

        if (isAddressCoveredForAM(pkt->getAddr(), pkt->getSize(), 0) && (mode != 2)) {
            printf("Functional write: ");
            uint8_t* test_start = pkt->getPtr<uint8_t>();
            for (int i = 0; i < pkt->getSize(); i++) {
                printf("%02x ", static_cast<unsigned int>(test_start[i]));
            }
            printf("\n");
        }

        if (mode != 2) {
            TRACE_PACKET("Write");
            pkt->makeResponse();
        }
    } else if (pkt->isPrint()) {
        if (mode != 2) {
            Packet::PrintReqState *prs =
                dynamic_cast<Packet::PrintReqState*>(pkt->senderState);
            assert(prs);
            // Need to call printLabels() explicitly since we're not going
            // through printObj().
            prs->printLabels();
            // Right now we just print the single byte at the specified address.
            ccprintf(prs->os, "%s%#x\n", prs->curPrefix(), *host_addr);
        }
    } else {
        panic("AbstractMemory: unimplemented functional command %s",
              pkt->cmdString());
    }
}

void
AbstractMemory::comprFunctionalAccess(PacketPtr pkt, uint64_t burst_size, uint64_t pageNum, std::vector<uint8_t>& pageBuffer, std::vector<uint8_t>& mPageBuffer) {
    /* receive a pkt from outside world */
    assert(pkt->comprBackup);  // the memory controller should be in compresso operation mode
    PacketPtr real_recv_pkt = pkt->comprBackup;

    /* get initial information */
    unsigned size = pkt->getSize();
    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;

    /* prepare the auxiliary information */
    std::vector<uint8_t> sizeMap = {0, 8, 32, 64};
    std::unordered_map<uint64_t, std::vector<uint8_t>> metaDataMap = pkt->comprMetaDataMap;

    /* start access the real memory */
    uint8_t *host_addr = toHostAddr(pkt->getAddr());

    if (pkt->isRead()) {
        /* process the pkt in order */
        for (unsigned int i = 0; i < pkt_count; i++) {
            uint64_t ppn = addr >> 12;
            uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

            assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */
            std::vector<uint8_t> metaData = metaDataMap[ppn];
            uint8_t type = getType(metaData, cacheLineIdx);
            std::vector<uint8_t> cacheLine(64, 0);

            if (ppn == pageNum) {
                assert(mPageBuffer.size() == metaData.size());
                for (int temp = 0; temp < metaData.size(); temp++) {
                    assert(mPageBuffer[temp] == metaData[temp]);
                }
                if (type != 0) {
                    uint8_t* pageBuffer_addr = pageBuffer.data() + cacheLineIdx * 64;
                    std::memcpy(cacheLine.data(), pageBuffer_addr, sizeMap[type]);
                }
                restoreData(cacheLine, type);
                assert(cacheLine.size() == 64);

                uint8_t loc = addr & 0x3F;
                uint64_t ofs = addr - pkt->getAddr();
                size_t readSize = std::min(pkt->getSize() - ofs, 64UL - loc);
                // printf("Abstract Memory Line %d: start set data for MC, ofs is %lld, loc is %d, size is %ld\n", __LINE__, ofs, loc, size);
                pkt->setDataForMC(cacheLine.data() + loc, ofs, readSize);

            } else {
                std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);

                bool inInflate = cLStatus.first;
                Addr real_addr = cLStatus.second;

                // printf("Abstract Memory Line %d: get the real addr 0x%lx\n", __LINE__, real_addr);

                assert(pmemAddr);
                uint8_t *host_addr = toHostAddr(real_addr);

                if (inInflate) {
                   type = 0b11;  // autually uncompressed;
                   std::memcpy(cacheLine.data(), host_addr, 64);
                } else {
                    if (type != 0) {
                        // printf("Abstract Memory Line %d: type != 0, type is %d\n", __LINE__, static_cast<unsigned int>(type));
                        // printf("Abstract Memory Line %d: the host addr is 0x%llx\n", __LINE__, (uint64_t)host_addr);

                        std::memcpy(cacheLine.data(), host_addr, sizeMap[type]);
                        // printf("Abstract Memory Line %d: finish read the compressed data\n", __LINE__);
                        // printf("the read compressed data is :\n");
                        // for (int ts = 0; ts < sizeMap[type]; ts++) {
                        //     if (ts % 8 == 0) {
                        //         printf("\n");
                        //     }
                        //     printf("%02x ", static_cast<unsigned int>(cacheLine[ts]));
                        // }
                        // printf("\n");

                    }
                }

                restoreData(cacheLine, type);
                // printf("Abstract Memory Line %d: finish restore the data\n", __LINE__);
                assert(cacheLine.size() == 64);

                // for (int u = 0; u < 8; u++) {
                //    for (int v = 0; v < 8; v++) {
                //        printf("%02x ", static_cast<unsigned int>(cacheLine[u * 8 + v]));
                //    }
                //    printf("\n");
                // }
                // printf("\n");

                uint8_t loc = addr & 0x3F;
                uint64_t ofs = addr - pkt->getAddr();
                size_t size = std::min(pkt->getSize() - ofs, 64UL - loc);
                // printf("Abstract Memory Line %d: start set data for MC, ofs is %lld, loc is %d, size is %ld\n", __LINE__, ofs, loc, size);
                pkt->setDataForMC(cacheLine.data() + loc, ofs, size);
                // printf("Abstract Memory Line %d: finish set data for MC\n", __LINE__);

            }
            addr = (addr | (burst_size - 1)) + 1;
        }

        // printf("Functional read: ");
        // uint8_t* start = pkt->getPtr<uint8_t>();
        // for (int ts = 0; ts < pkt->getSize(); ts++) {
        //    printf("%02x ", static_cast<unsigned int>(start[ts]));
        // }
        // printf("\n");

        TRACE_PACKET("Read");
        real_recv_pkt->makeResponse();
    } else if (pkt->isWrite()) {
        /* assert the pkt should be burst_size/cacheline aligned */
        assert(offset == 0);
        assert((size & (burst_size - 1)) == 0);
        assert(pkt_count == (size / burst_size));
        // printf("Functional write: ");
        // uint8_t* start = real_recv_pkt->getPtr<uint8_t>();
        // for (int ts = 0; ts < real_recv_pkt->getSize(); ts++) {
        //    printf("%02x ", static_cast<unsigned int>(start[ts]));
        // }
        // printf("\n");

        for (unsigned int i = 0; i < pkt_count; i++) {
            uint64_t ppn = addr >> 12;
            uint8_t cacheLineIdx = (addr >> 6) & 0x3F;

            assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */
            std::vector<uint8_t> metaData = metaDataMap[ppn];

            uint8_t type = getType(metaData, cacheLineIdx);

            std::vector<uint8_t> cacheLine(64, 0);

            uint64_t ofs = addr - pkt->getAddr();

            pkt->writeDataForMC(cacheLine.data(), ofs, 64);

            /* compress the cacheline */
            std::vector<uint8_t> compressed = compress(cacheLine);

            if (compressed.size() > 32) {
                assert(compressed.size() == 64);
            }

            // printf("the metadata is :\n");
            // for (int k = 0; k < 64; k++) {
            //     printf("%02x",static_cast<unsigned>(metaData[k]));

            // }
            // printf("\n");

            if (pageNum == ppn) {
                verifyMetaData(type, cacheLine, compressed.size());
                assert(mPageBuffer.size() == metaData.size());
                for (int temp = 0; temp < metaData.size(); temp++) {
                    assert(mPageBuffer[temp] == metaData[temp]);
                }
                uint8_t* pageBuffer_addr = pageBuffer.data() + cacheLineIdx * 64;
                memcpy(pageBuffer_addr, compressed.data(), sizeMap[type]);
            } else {
                std::pair<bool, Addr> cLStatus = addressTranslation(metaData, cacheLineIdx);

                bool inInflate = cLStatus.first;
                Addr real_addr = cLStatus.second;

                if (inInflate) {
                    type = 0b11;
                }

                verifyMetaData(type, cacheLine, compressed.size());

                if (type != 0) {
                    // printf("if inInflate: %d\n", inInflate);
                    if (!inInflate) {
                        // printf("the compressed size is %d\n", compressed.size());
                        // printf("the space is %d\n", sizeMap[type]);
                        assert(compressed.size() <= sizeMap[type]);
                    }

                    uint8_t* host_addr = toHostAddr(real_addr);

                    // printf("the host address is 0x%llx:\n", host_addr);
                    // for (int qw = 0; qw < 8; qw++) {
                    //     printf("%02x ", host_addr[qw]);
                    // }
                    // printf("\n");

                    // printf("the original cacheline value is :\n");
                    // for (int qw = 0; qw < 8; qw++) {
                    //     for (int er = 0; er < 8; er++) {
                    //         printf("%02x ", cacheLine[qw* 8 + er]);
                    //     }
                    //     printf("\n");
                    // }
                    // printf("\n");

                    uint64_t test_size = 0;
                    if (pmemAddr) {
                        if (type == 0b11) {
                            std::memcpy(host_addr, cacheLine.data(), cacheLine.size());
                            test_size = cacheLine.size();
                        } else {
                            std::memcpy(host_addr, compressed.data(), compressed.size());
                            test_size = compressed.size();
                        }
                    }

                    // for (int qw = 0; qw < test_size; qw++) {
                    //     if (qw % 8 == 0) {
                    //         printf("\n");
                    //     }
                    //     printf("%02x ", host_addr[qw]);
                    // }
                    // printf("\n");
                } else {
                    /* all zero */
                }
            }
            Addr old_addr = addr;
            addr = (addr | (burst_size - 1)) + 1;
            assert(addr == old_addr + burst_size);
        }

        TRACE_PACKET("Write");
        real_recv_pkt->makeResponse();
    } else if (pkt->isPrint()) {
        uint8_t *host_addr = toHostAddr(real_recv_pkt->getAddr());
        Packet::PrintReqState *prs =
            dynamic_cast<Packet::PrintReqState*>(pkt->senderState);
        assert(prs);
        // Need to call printLabels() explicitly since we're not going
        // through printObj().
        prs->printLabels();
        // Right now we just print the single byte at the specified address.
        ccprintf(prs->os, "%s%#x\n", prs->curPrefix(), *host_addr);
    } else {
        panic("AbstractMemory: unimplemented functional command %s",
              pkt->cmdString());
    }
}

void
AbstractMemory::functionalAccessForNew(PacketPtr pkt, uint64_t burst_size, Addr zeroAddr) {
    /* receive a pkt from outside world */
    assert(pkt->new_backup);
    PacketPtr real_recv_pkt = pkt->new_backup;

    /* get initial information */
    unsigned size = pkt->getSize();
    unsigned offset = pkt->getAddr() & (burst_size - 1);
    unsigned int pkt_count = divCeil(offset + size, burst_size);

    Addr base_addr = pkt->getAddr();
    Addr addr = base_addr;

    /* prepare the auxiliary information */
    std::vector<uint8_t> sizeMap = {1, 22, 44, 64};
    std::unordered_map<uint64_t, std::vector<uint8_t>> metaDataMap = pkt->newfunctionMetaDataMap;

    if (pkt->isRead()) {
        /* process the pkt in order */
        for (unsigned int i = 0; i < pkt_count; i++) {
            uint64_t ppn = addr >> 12;
            uint8_t cachelineIdx = (addr >> 6) & 0x3F;

            assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */
            std::vector<uint8_t> metaData = metaDataMap[ppn];
            uint8_t type = new_getTypeAM(metaData, cachelineIdx);
            std::vector<uint8_t> cacheLine(64, 0);

            std::vector<uint64_t> translationRes(3, 0);
            translationRes[0] = zeroAddr;
            if (new_getCoverageAM(metaData) > cachelineIdx) {
                translationRes = new_addressTranslationAM(metaData, cachelineIdx);
            } else {
                assert(type == 0);
            }
            uint8_t* host_origin_addr = toHostAddr(translationRes[0]);
            
            if (type >= 0b100) {
                std::memcpy(cacheLine.data(), host_origin_addr, 1);
                uint8_t overflowIdx = cacheLine[0];

                Addr overflow_addr = new_calOverflowAddrAM(metaData, overflowIdx);
                uint8_t* host_overflow_addr = toHostAddr(overflow_addr);
                std::memcpy(cacheLine.data(), host_overflow_addr, 64);
            } else {
                assert(sizeMap[type] > translationRes[2]);
                if (translationRes[2] == 0) {
                    std::memcpy(cacheLine.data(), host_origin_addr, sizeMap[type]);   
                } else {
                    uint64_t prefixLen = sizeMap[type] - translationRes[2];
                    std::memcpy(cacheLine.data(), host_origin_addr, prefixLen);
                    uint8_t* host_new_block_addr = toHostAddr(translationRes[1]);
                    std::memcpy(cacheLine.data() + prefixLen, host_new_block_addr, translationRes[2]);
                }
            }

            new_restoreDataAM(cacheLine, type);
            assert(cacheLine.size() == 64);

            uint8_t loc = addr & 0x3F;
            uint64_t ofs = addr - pkt->getAddr();
            size_t size = std::min(pkt->getSize() - ofs, 64UL - loc);
            // printf("Abstract Memory Line %d: start set data for MC, ofs is %lld, loc is %d, size is %ld\n", __LINE__, ofs, loc, size);
            pkt->setDataForMC(cacheLine.data() + loc, ofs, size);

            addr = (addr | (burst_size - 1)) + 1;
        }

        if (isAddressCoveredForAM(real_recv_pkt->getAddr(),real_recv_pkt->getSize(), 0)) {
            printf("Functional read: ");
            uint8_t* start = pkt->getPtr<uint8_t>();
            for (int ts = 0; ts < pkt->getSize(); ts++) {
            printf("%02x ", static_cast<unsigned int>(start[ts]));
            }
            printf("\n");
        }


        TRACE_PACKET("Read");
        real_recv_pkt->makeResponse();
    } else if (pkt->isWrite()) {  
        /* assert the pkt should be burst_size/cacheline aligned */
        assert(offset == 0);
        assert((size & (burst_size - 1)) == 0);
        assert(pkt_count == (size / burst_size));

        if (isAddressCoveredForAM(real_recv_pkt->getAddr(),real_recv_pkt->getSize(), 0)) {
            printf("Functional write: ");
            uint8_t* start = real_recv_pkt->getPtr<uint8_t>();
            for (int ts = 0; ts < real_recv_pkt->getSize(); ts++) {
            printf("%02x ", static_cast<unsigned int>(start[ts]));
            }
            printf("\n");
        }

        for (unsigned int i = 0; i < pkt_count; i++) {
            uint64_t ppn = addr >> 12;
            uint8_t cacheLineIdx = (addr >> 6) & 0x3F;
            
            assert(metaDataMap.find(ppn) != metaDataMap.end());  /* the metaData info should be ready by this point */
            std::vector<uint8_t> metaData = metaDataMap[ppn];

            // printf("cacheLineIdx is %d\n", cacheLineIdx);
            // printf("coverage %d\n", new_getCoverageAM(metaData));

            assert(cacheLineIdx < new_getCoverageAM(metaData));
            uint8_t type = new_getTypeAM(metaData, cacheLineIdx);

            std::vector<uint8_t> cacheLine(64, 0);

            uint64_t ofs = addr - pkt->getAddr();

            pkt->writeDataForMC(cacheLine.data(), ofs, 64);

            std::vector<uint8_t> new_cacheLine = cacheLine;

            if (type < 0b100) {
                /* compress the cacheline */
                new_cacheLine = new_compress(cacheLine);
            }

            if (new_cacheLine.size() > 44) {
                assert(new_cacheLine.size() == 64);
            }

            // printf("the metadata is :\n");
            // for (int k = 0; k < 64; k++) {
            //     printf("%02x",static_cast<unsigned>(metaData[k]));

            // }
            // printf("\n");

            std::vector<uint64_t> translationRes = new_addressTranslationAM(metaData, cacheLineIdx);
  

            uint8_t* origin_host_addr = toHostAddr(translationRes[0]);

            Addr real_addr = translationRes[0];

            if (type >= 0b100) {
                uint8_t overflowIdx = 0;
                std::memcpy(&overflowIdx, origin_host_addr, 1);
                real_addr = new_calOverflowAddrAM(metaData, overflowIdx);
                assert(new_cacheLine.size() == 64);
            }

            uint8_t* real_host_addr = toHostAddr(real_addr);

            if (isAddressCoveredForAM(real_recv_pkt->getAddr(),real_recv_pkt->getSize(), 1)) {
                printf("the cacheLineIdx is %d\n", static_cast<unsigned int>(cacheLineIdx));
                printf("the origin space data resides is 0x%lx\n", translationRes[0]);
                printf("the real mpa address is 0x%lx\n", real_addr);
                printf("ppn is %d, the metadata is:\n", ppn);
                for (int k = 0; k < 64; k++) {
                    printf("%02x",static_cast<unsigned>(metaData[k]));
                }
                printf("\n");
                printf("the new_cacheline size is %d\n", new_cacheLine.size());

            }




            if (pmemAddr) {
                if (type >= 0b100 || translationRes[2] == 0) {
                    std::memcpy(real_host_addr, new_cacheLine.data(), new_cacheLine.size());
                } else {
                    uint64_t prefixLen = sizeMap[type] - translationRes[2];
                    if (prefixLen >= new_cacheLine.size()) {
                        std::memcpy(real_host_addr, new_cacheLine.data(), new_cacheLine.size());
                    } else {
                        std::memcpy(real_host_addr, new_cacheLine.data(), prefixLen);
                        uint8_t* host_new_block_addr = toHostAddr(translationRes[1]);
                        std::memcpy(host_new_block_addr, new_cacheLine.data() + prefixLen, new_cacheLine.size() - prefixLen);
                    }
                }
            }

            Addr old_addr = addr;
            addr = (addr | (burst_size - 1)) + 1;
            assert(addr == old_addr + burst_size);
        }

        TRACE_PACKET("Write");
        real_recv_pkt->makeResponse();
    } else if (pkt->isPrint()) {
        uint8_t *host_addr = toHostAddr(real_recv_pkt->getAddr());
        Packet::PrintReqState *prs =
            dynamic_cast<Packet::PrintReqState*>(pkt->senderState);
        assert(prs);
        // Need to call printLabels() explicitly since we're not going
        // through printObj().
        prs->printLabels();
        // Right now we just print the single byte at the specified address.
        ccprintf(prs->os, "%s%#x\n", prs->curPrefix(), *host_addr);
    } else {
        panic("AbstractMemory: unimplemented functional command %s",
              pkt->cmdString());
    }

}


uint8_t
AbstractMemory::getType(const std::vector<uint8_t>& metaData, const uint8_t& index) {
    int startPos = (2 + 32) * 8 + index * 2;
    int loc = startPos / 8;
    int ofs = startPos % 8;
    uint8_t type = 0b11 & (metaData[loc] >> (6 - ofs));
    return type;
}

std::vector<uint8_t>
AbstractMemory::compress(const std::vector<uint8_t>& cacheLine) {
    assert(cacheLine.size() == 64);
    std::pair<uint64_t, std::vector<uint16_t>> transformed = BDXTransform(cacheLine);
    uint64_t base = transformed.first;
    std::vector<uint8_t> compressed = compressC(transformed.second);
    for (int i = 0; i < 4; i++) {
        uint8_t val = base & 0xFF;
        base >>= 8;
        compressed.insert(compressed.begin(), val);
    }
    if (compressed.size() > 32) {
        return cacheLine;
    }
    return compressed;
}

void
AbstractMemory::decompress(std::vector<uint8_t>& data) {
    uint64_t base = 0;
    for (int i = 0; i < 4; i++) {
        base = (base << 8) | data[0];
        data.erase(data.begin());
    }
    std::vector<uint16_t> interm = decompressC(data);
    data = BDXRecover(base, interm);
}

std::pair<uint64_t, std::vector<uint16_t>>
AbstractMemory::BDXTransform(const std::vector<uint8_t>& origin) {
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
AbstractMemory::BDXRecover(const uint64_t& base, std::vector<uint16_t>& DBX) {
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
AbstractMemory::supply(uint16_t code, int len, uint8_t& val, int& idx, std::vector<uint8_t>& compressed) {
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
AbstractMemory::compressC(const std::vector<uint16_t>& inputData) {
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
AbstractMemory::dismantle(int len, int& idx, int& ofs, const std::vector<uint8_t>& compresseData) {
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
AbstractMemory::checkStatus(const int& idx, const int& ofs, const std::vector<uint8_t>& compresseData) {
    uint8_t sign = (compresseData[idx] >> (7 - ofs)) & 0x1;
    return (sign == 0x1);
}

void
AbstractMemory::interpret(int& idx, int& ofs, const std::vector<uint8_t>& compresseData, std::vector<uint16_t>& decompressed) {
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
AbstractMemory::decompressC(const std::vector<uint8_t>& compresseData){
    std::vector<uint16_t> decompressed;
    int idx = 0;
    int ofs = 0;
    while (decompressed.size() < 33) {
        interpret(idx, ofs, compresseData, decompressed);
}
assert(decompressed.size() == 33);
return decompressed;
}

std::pair<bool, Addr>
AbstractMemory::addressTranslation(const std::vector<uint8_t>& metaData, uint8_t index){
    // printf("=============== enter addressTranslation ===================\n");
    std::vector<uint8_t> sizeMap = {0, 8, 32, 64};

    assert(metaData.size() == 64);
    uint64_t origin_size = ((metaData[0] & (0x0F)) << 8) | metaData[1];
    uint8_t valid_inflate_num = (metaData[63] & ((0b1 << 5) - 1));  // use last 5 bit to store the counter
    assert(valid_inflate_num <= 17);
    assert(origin_size + valid_inflate_num * 64 <= 4096);

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

    // printf("if in inflate: %d: the loc is %d\n", in_inflate, loc);

    uint64_t sumSize = 0;
    if (in_inflate) {
        sumSize = (((origin_size + 0x3F) >> 6) << 6) + loc * 64;
    } else {
        for (uint8_t u = 0; u < index; u++) {
            uint8_t type = getType(metaData, u);
            sumSize += sizeMap[type];
        }
    }
    // printf("abstractMemory: sumSize: %d\n", sumSize);
    uint8_t chunkIdx = sumSize / 512;

    // printf("chunkIdx: %d\n", static_cast<unsigned int>(chunkIdx));

    for (int u = 0; u < 4; u++){   // 4B per MPFN
        addr = (addr << 8) | (metaData[2 + 4 * chunkIdx + u]);
    }
    addr = (addr << 9) | (sumSize & 0x1FF);

    // printf("the generate addr is 0x%llx\n", addr);
    return std::make_pair(in_inflate, addr);
}

void
AbstractMemory::restoreData(std::vector<uint8_t>& cacheLine, uint8_t type) {
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
AbstractMemory::setType(std::vector<uint8_t>& metaData, const uint8_t& index, const uint8_t& type) {
    assert(type < 4);
    int startPos = (2 + 32) * 8 + index * 2;
    int loc = startPos / 8;
    int ofs = startPos % 8;
    metaData[loc] = (metaData[loc] & (~(0b11 << (6 - ofs)))) |  (type << (6 - ofs));
}


/* ====== special for the new architecture ======= */

uint8_t 
AbstractMemory::new_getTypeAM(const std::vector<uint8_t>& metaData, const uint8_t& index) {
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

uint8_t
AbstractMemory::new_getCoverageAM(const std::vector<uint8_t>& metaData) {
    uint8_t cover = metaData[0] & 0x7F;
    return cover;
}

std::vector<uint8_t>
AbstractMemory::new_compress(const std::vector<uint8_t>& cacheLine) {
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
        compressed.insert(compressed.begin(), val);
    }
    if (compressed.size() > 44) {
        return cacheLine;
    }
    return compressed;
}

void
AbstractMemory::new_decompress(std::vector<uint8_t>& data) {
    uint64_t base = 0;
    for (int i = 0; i < 4; i++) {
        base = (base << 8) | data[0];
        data.erase(data.begin());
    }

    std::vector<uint16_t> interm = decompressC(data);
    data = BDXRecover(base, interm);
}

void
AbstractMemory::new_restoreDataAM(std::vector<uint8_t>& compressed, uint8_t type) {
    if (type >= 0b011) {
        return;
    } else if (type == 0b000) {
        compressed.resize(64);
        for (int i = 0; i < compressed.size(); i++) {
            compressed[i] = 0;
        }
    } else {
        new_decompress(compressed);
    }
}

/*
    Givem the metadata and the target cacheLineIdx, return a vector containing three elements
    res[0]: the starting address of the original space allocated for this cacheline
    res[1] (only valid for those cachelines that across two blocks): the starting address of the second part
    res[2]: the size of the second part
*/
std::vector<uint64_t>
AbstractMemory::new_addressTranslationAM(const std::vector<uint8_t>& metaData, uint8_t cachelineIdx) {

    std::vector<uint64_t> res(3, 0);
    assert(metaData.size() == 64);
    
    std::vector<uint8_t> sizeMap = {1, 22, 44, 64};
    std::vector<uint64_t> pageSizeMap = {0, 512, 1024, 2048, 3072, 4096, 4608, 5120, 6144, 7168};


    // printf("the metadata is:\n");

    // for (int i = 0; i < 64; i++) {
    //     if (i % 8 == 0) {
    //         printf("\n");
    //     }
    //     printf("%x ", static_cast<unsigned int>(metaData[i]));
    // }

    // printf("\n");

    uint64_t startLoc = 0;

    for (uint8_t u = 0; u < cachelineIdx; u++) {
        uint8_t type = new_getTypeAM(metaData, u);
        startLoc += sizeMap[(type & 0b11)];
    }
    assert(startLoc < pageSizeMap[9]);

    // printf("new_addressTranslation AM cacheLineidx %d, startLoc is %ld\n", static_cast<unsigned int>(cachelineIdx), startLoc);
    
    Addr addr = 0;
    auto it = std::upper_bound(pageSizeMap.begin(), pageSizeMap.end(), startLoc);
    uint64_t chunkIdx = it - pageSizeMap.begin();
    
    assert(chunkIdx >= 1);
    assert(chunkIdx <= metaData[1]);

    for (int u = 0; u < 4; u++){   // 4B per MPFN
        addr = (addr << 8) | (metaData[4 * chunkIdx + u]);
    }

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

    uint8_t cur_type = new_getTypeAM(metaData, cachelineIdx);

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
AbstractMemory::new_calOverflowAddrAM(const std::vector<uint8_t>& metaData, uint8_t overflowIdx) {
    std::vector<uint64_t> pageSizeMap = {0, 512, 1024, 2048, 3072, 4096, 4608, 5120, 6144, 7168};

    uint64_t offset = metaData[2] * 64 + overflowIdx * 64;
    // printf("metaDatap[2] is %d\n", static_cast<unsigned int>(metaData[2]));
    // printf("overflowIdx is %d\n", static_cast<unsigned int>(overflowIdx));
    assert(offset < pageSizeMap[9]);
    Addr addr = 0;
    auto it = std::upper_bound(pageSizeMap.begin(), pageSizeMap.end(), offset);
    uint64_t chunkIdx = it - pageSizeMap.begin();
    assert(chunkIdx >= 1);

    for (int u = 0; u < 4; u++){   // 4B per MPFN
        addr = (addr << 8) | (metaData[4 * chunkIdx + u]);
    }

    uint64_t leftover = offset - pageSizeMap[chunkIdx - 1];
    uint64_t chunkSize = pageSizeMap[chunkIdx] - pageSizeMap[chunkIdx - 1];
    assert(leftover < chunkSize);
    if (chunkSize == 512) {
        addr = (addr << 9) | (leftover & 0x1FF);
    } else {
        assert(chunkSize == 1024);
        addr = (addr << 9) | (leftover & 0x3FF);
    }
    return addr;
}


/* ====== end for the new ====== */

} // namespace memory
} // namespace gem5
