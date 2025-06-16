
import argparse
import os
import m5
from caches import *
from m5.objects import *

parser = argparse.ArgumentParser(description="OOO core with DRAM energy stats and full cache hierarchy")
parser.add_argument("--mem_operation_mode", type=str, default="normal",
                    help="Memory controller mode: normal, compresso, DyLeCT")
parser.add_argument("--binary", type=str, required=True,
                    help="Path to the binary to execute")
parser.add_argument("--cmd_args", type=str, default="",
                    help="Arguments to pass to the binary")
parser.add_argument("--input", type=str, default=None,
                    help="Path to input file for stdin")
options = parser.parse_args()

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "3GHz"
system.clk_domain.voltage_domain = VoltageDomain()
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("512MiB")]

# Use O3 CPU
system.cpu = DerivO3CPU()

# Create memory bus
system.membus = SystemXBar()

# Create an L1 instruction and data cache
system.cpu.icache = L1ICache()
system.cpu.dcache = L1DCache()

system.cpu.icache.connectCPU(system.cpu)
system.cpu.dcache.connectCPU(system.cpu)

# L2 cache
system.l2bus = L2XBar()
system.cpu.icache.connectBus(system.l2bus)
system.cpu.dcache.connectBus(system.l2bus)

system.l2cache = L2Cache()
system.l2cache.connectCPUSideBus(system.l2bus)

# Create a memory bus
system.membus = SystemXBar()

# Connect the L2 cache to the membus
system.l2cache.connectMemSideBus(system.membus)

# Interrupts
system.cpu.createInterruptController()
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# DRAM with energy stats enabled
system.mem_ctrl = MemCtrl(operation_mode=options.mem_operation_mode)
system.mem_ctrl.dram = DDR4_2400_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.dram.enable_dram_powerdown = True  # 仅支持低功耗模式统计
system.mem_ctrl.port = system.membus.mem_side_ports

# Final connections
system.system_port = system.membus.cpu_side_ports
system.workload = SEWorkload.init_compatible(options.binary)

process = Process()
process.cmd = [options.binary] + options.cmd_args.split()
if options.input:
    process.input = options.input
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()
print("Starting Simulation...")
exit_event = m5.simulate()
print(f"Exiting @ {m5.curTick()} because {exit_event.getCause()}")
