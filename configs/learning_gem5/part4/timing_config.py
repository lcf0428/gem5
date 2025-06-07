import argparse
import os
import m5
from m5.objects import *

parser = argparse.ArgumentParser(description="Configurable system with different operation mode of Memory Controller")
parser.add_argument("--mem_operation_mode", type=str, default="normal",
                    help="Memory controller mode: normal, compresso, DyLeCT")
parser.add_argument("--binary", type=str, required=True,
                    help="Path to the binary to execute")
# parser.add_argument("--cmd_args", type=str, nargs='*', default=[],
#                     help="Arguments to pass to the binary")
parser.add_argument("--cmd_args", type=str, default="",
                    help="Arguments to pass to the binary")
parser.add_argument("--input", type=str, default=None,
                    help="Path to input file for stdin")
options = parser.parse_args()

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("512MiB")]

system.cpu = X86TimingSimpleCPU()
system.membus = SystemXBar()

system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
system.cpu.createInterruptController()
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

system.mem_ctrl = MemCtrl(operation_mode=options.mem_operation_mode)
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

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