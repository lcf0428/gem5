from m5.objects import Cache

# 自定义三级缓存SimObject类
class L3Cache(Cache):
    def __init__(
        self,
        size: str,
        assoc: int = 16,
        tag_latency: int = 20,
        data_latency: int = 20,
        response_latency: int = 2,
        mshrs: int = 32,
        tgts_per_mshr: int = 12,
    ):
        super().__init__()
        # 设置L3缓存的参数（容量、相联度、各种延迟等）
        self.size = size
        self.assoc = assoc
        self.tag_latency = tag_latency
        self.data_latency = data_latency
        self.response_latency = response_latency
        self.mshrs = mshrs
        self.tgts_per_mshr = tgts_per_mshr
        # 通常L3为mostly inclusive，以包含下级缓存内容
        self.clusivity = "mostly_incl"


from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import AbstractClassicCacheHierarchy
from gem5.components.cachehierarchies.abstract_three_level_cache_hierarchy import AbstractThreeLevelCacheHierarchy
from gem5.components.cachehierarchies.classic.caches.l1icache import L1ICache
from gem5.components.cachehierarchies.classic.caches.l1dcache import L1DCache
from gem5.components.cachehierarchies.classic.caches.l2cache import L2Cache
from gem5.components.cachehierarchies.classic.caches.mmu_cache import MMUCache
from gem5.components.boards.abstract_board import AbstractBoard
from m5.objects import L2XBar, SystemXBar, BadAddr, Port
from gem5.isas import ISA

class PrivateL1PrivateL2SharedL3CacheHierarchy(AbstractClassicCacheHierarchy, AbstractThreeLevelCacheHierarchy):
    def __init__(
        self,
        l1i_size: str,
        l1d_size: str,
        l2_size: str,
        l3_size: str,
        l1i_assoc: int = 8,
        l1d_assoc: int = 8,
        l2_assoc: int = 16,
        l3_assoc: int = 16,
        membus: SystemXBar = None
    ):
        # 初始化父类
        AbstractClassicCacheHierarchy.__init__(self)
        AbstractThreeLevelCacheHierarchy.__init__(
            self, 
            l1i_size=l1i_size, l1i_assoc=l1i_assoc,
            l1d_size=l1d_size, l1d_assoc=l1d_assoc,
            l2_size=l2_size, l2_assoc=l2_assoc,
            l3_size=l3_size, l3_assoc=l3_assoc
        )
        # 如果未传入membus，则创建默认的系统总线
        if membus is None:
            self.membus = SystemXBar(width=64)
            # 设置badaddr响应器
            self.membus.badaddr_responder = BadAddr()
            self.membus.default = self.membus.badaddr_responder.pio
        else:
            self.membus = membus

    def get_cpu_side_port(self) -> Port:
        # 顶层CPU端口（最终连接到CPU的cache端口），使用系统总线的cpu_side_ports
        return self.membus.cpu_side_ports

    def get_mem_side_port(self) -> Port:
        # 底层内存端口（最终连接到内存），使用系统总线的mem_side_ports
        return self.membus.mem_side_ports

    def incorporate_cache(self, board: AbstractBoard) -> None:
        # 将Cache层次集成到Board中
        # 1) 连接系统端口，用于模拟器的功能访问
        board.connect_system_port(self.membus.cpu_side_ports)
        # 2) 将内存控制器连接到内存总线的mem_side端口
        for _, port in board.get_memory().get_mem_ports():
            self.membus.mem_side_ports = port

        # 3) 创建每核私有L1 I/D caches和L2 caches，以及相应总线
        num_cores = board.get_processor().get_num_cores()
        self.l1icaches = [L1ICache(size=self._l1i_size, assoc=self._l1i_assoc) for _ in range(num_cores)]
        self.l1dcaches = [L1DCache(size=self._l1d_size, assoc=self._l1d_assoc) for _ in range(num_cores)]
        # 每个核一个L2跨栏总线，用于连接L1 I/D到L2
        self.l2buses = [L2XBar() for _ in range(num_cores)]
        self.l2caches = [L2Cache(size=self._l2_size, assoc=self._l2_assoc) for _ in range(num_cores)]
        # 页表Walk缓存（ITLB/DTLB），每核各一个
        self.iptw_caches = [MMUCache(size="8KiB") for _ in range(num_cores)]
        self.dptw_caches = [MMUCache(size="8KiB") for _ in range(num_cores)]
        # 创建共享L3跨栏总线（可直接复用L2XBar）
        self.l3bus = L2XBar()  # 若希望L3总线有不同延迟，可自定义L3XBar类:contentReference[oaicite:2]{index=2}
        # 创建共享的 L3 Cache 实例
        self.l3cache = L3Cache(size=self._l3_size, assoc=self._l3_assoc)

        # 如果系统包含一致性IO设备，为其设置IO缓存
        if board.has_coherent_io():
            self._setup_io_cache(board)

        # 4) 遍历每个核心，将其L1、L2、L3端口连接起来
        for i, cpu in enumerate(board.get_processor().get_cores()):
            # 连接CPU的I/D Cache端口到L1 caches
            cpu.connect_icache(self.l1icaches[i].cpu_side)
            cpu.connect_dcache(self.l1dcaches[i].cpu_side)
            # 将L1I、L1D和PageTable Walker缓存的mem_side连接到对应L2总线（作为L2总线的CPU侧客户端）
            self.l1icaches[i].mem_side = self.l2buses[i].cpu_side_ports
            self.l1dcaches[i].mem_side = self.l2buses[i].cpu_side_ports
            self.iptw_caches[i].mem_side = self.l2buses[i].cpu_side_ports
            self.dptw_caches[i].mem_side = self.l2buses[i].cpu_side_ports
            # 将L2总线的内存端口连接到L2 Cache的CPU侧端口
            self.l2buses[i].mem_side_ports = self.l2caches[i].cpu_side
            # **核心步骤**：将每个L2 Cache的内存侧连接到共享L3总线的CPU侧端口
            self.l3bus.cpu_side_ports = self.l2caches[i].mem_side
            # 连接CPU的页表Walker端口到相应的MMUCache
            cpu.connect_walker_ports(self.iptw_caches[i].cpu_side, self.dptw_caches[i].cpu_side)
            # X86架构需要连接中断端口
            if board.get_processor().get_isa() == ISA.X86:
                cpu.connect_interrupt(self.membus.mem_side_ports, self.membus.cpu_side_ports)
            else:
                cpu.connect_interrupt()
        # 出了循环后，连接共享L3和内存总线：
        # 将L3总线的内存端口连接到L3 Cache的CPU侧
        self.l3bus.mem_side_ports = self.l3cache.cpu_side
        # 将L3 Cache的内存侧连接到系统总线的CPU侧，从而通向主存
        self.membus.cpu_side_ports = self.l3cache.mem_side

    def _setup_io_cache(self, board: AbstractBoard) -> None:
        # 为一致性IO配置一个小的缓存
        self.iocache = Cache(
            size="1KiB", assoc=8,
            tag_latency=50, data_latency=50, response_latency=50,
            mshrs=20, tgts_per_mshr=12,
            addr_ranges=board.mem_ranges
        )
        # 将IO缓存插入CPU侧总线和IO端口之间
        self.iocache.mem_side = self.membus.cpu_side_ports
        self.iocache.cpu_side = board.get_mem_side_coherent_io_port()
