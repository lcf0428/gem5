import glob
import optparse
import os
import sys

programs = [
    # "perlbench",
    "gcc",
    "mcf",
    "omnetpp",
    "xalancbmk",
    "x264",
    "deepsjeng",
    "leela",
    "exchange2",
    "xz",
    "bwaves",
    "cactuBSSN",
    "lbm",
    "wrf",
    # # "cam4",
    # "pop2",
    "imagick",
    "nab",
    "fotonik3d",
    "roms",
]

benchmarks_dir = [
    # "benchspec/CPU/600.perlbench_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/602.gcc_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/605.mcf_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/620.omnetpp_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/623.xalancbmk_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/625.x264_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/631.deepsjeng_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/641.leela_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/648.exchange2_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/657.xz_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/603.bwaves_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/607.cactuBSSN_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/619.lbm_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/621.wrf_s/run/run_base_refspeed_mytest-m64.0000",
    # # "benchspec/CPU/627.cam4_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/628.pop2_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/638.imagick_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/644.nab_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/649.fotonik3d_s/run/run_base_refspeed_mytest-m64.0000",
    "benchspec/CPU/654.roms_s/run/run_base_refspeed_mytest-m64.0000",
]

binaries = [
    # "./perlbench_s_base.mytest-m64",
    "./sgcc_base.mytest-m64",
    "./mcf_s_base.mytest-m64",
    "./omnetpp_s_base.mytest-m64",
    "./xalancbmk_s_base.mytest-m64",
    "./x264_s_base.mytest-m64",
    "./deepsjeng_s_base.mytest-m64",
    "./leela_s_base.mytest-m64",
    "./exchange2_s_base.mytest-m64",
    "./xz_s_base.mytest-m64",
    "./speed_bwaves_base.mytest-m64",
    "./cactuBSSN_s_base.mytest-m64",
    "./lbm_s_base.mytest-m64",
    "./wrf_s_base.mytest-m64",
    # # "./cam4_s_base.mytest-m64",
    # "./speed_pop2_base.mytest-m64",
    "./imagick_s_base.mytest-m64",
    "./nab_s_base.mytest-m64",
    "./fotonik3d_s_base.mytest-m64",
    "./sroms_base.mytest-m64",
]

args = [
    # "-I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1",
    "gcc-pp.c -O5 -fipa-pta -o gcc-pp.opts-O5_-fipa-pta.s",
    "inp.in",
    "-c General -r 0",
    "-v t5.xml xalanc.xsl",
    "--pass 1 --stats x264_stats.log --bitrate 1000 --frames 1000 -o BuckBunny_New.264 BuckBunny.yuv 1280x720",
    "ref.txt",
    "ref.sgf",
    "6",
    "cpu2006docs.tar.xz 6643 055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae 1036078272 1111795472 4",
    "bwaves_1",
    "spec_ref.par",
    "2000 reference.dat 0 0 200_200_260_ldc.of",
    "",
    # # "",
    # "",
    "-limit disk 0 refspeed_input.tga -resize 817% -rotate -2.76 -shave 540x375 -alpha remove -auto-level -contrast-stretch 1x1% -colorspace Lab -channel R -equalize +channel -colorspace sRGB -define histogram:unique-colors=false -adaptive-blur 0x5 -despeckle -auto-gamma -adaptive-sharpen 55 -enhance -brightness-contrast 10x10 -resize 30% refspeed_output.tga",
    " 3j1n 20140317 220",
    "",
    "",
]

stdin_file = [
    # "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "bwaves_1.in",
    "",
    "",
    "",
    # # "",
    # "",
    "",
    "",
    "",
    "ocean_benchmark3.in",
]

tick_intervals = [
    #  0,
    1000000,
    6,
    1000000,
    2000000,
    2000000,
    800000,
    1200000,
    1800000,
    60000,
    1800000,
    1800000,
    1000000,
    120000,
    # 0,
    # 0,
    410000,
    5000,
    2,
    2400000,
]

recency_list_sizes = [
#     0,
    1256174,
    3324,
    42878,
    51053,
    27590,
    1232714,
    2611,
    225,
    2751166,
    2859201, # bwaves ?
    892346,
    577706,
    46355,
#     0,
#     0,
    302402,
    3469,
    878, # fotonik3d ?
    1853443
]

small_recency_list_sizes = [
#     0,
    684623,
    000,  # mcf
    23106,
    26231,
    15749,
    704405,
    000,  # leela
    000,  # exchange2
    1572095,
    1530496, # bwaves ?
    509912,
    330117,
    24810,  # wrf
#     0,
#     0,
    112143,
    1658,
    000, # fotonik3d ?
    1059110,
]

small_memory_capacity = [
    # 0,
    28042150,
    000,  # mcf
    946406,
    1074414,
    645071,
    28852437,
    000,  # leela
    000,  # exchange2
    64393020,
    62689084,
    20885996, # cactuBSSN
    13521617,
    1016218, # wrf
    # 0,
    # 0,
    4593369,
    67896,
    000, # fotonik3d ?
    43381162,
]


operation_mode = sys.argv[1]
operation_mode = str(operation_mode)

HOME = "/local/home/liuche"
BASE_DIR = f"{HOME}/SPEC"  # TODO: change the SPEC directory
GEM5_DIR = f"{HOME}/gem5/gem5"  # TODO: change the gem5 directory
BUILD_DIR = f"{HOME}/gem5_results/SPEC/build"
RESULTS_DIR = f"{HOME}/gem5_results/SPEC/checkpoint/{operation_mode}_small"
SIMULATE_DIR = f"{GEM5_DIR}/scripts/SPEC"


index_list = [8, 16]
# index_list = [9]

# for i in range(len(programs)):
# for i in range(2, 3):
for i in index_list:
    program = programs[i]

    path = f"{BASE_DIR}/{benchmarks_dir[i]}"
    print("Directoy changed to: " + path)
    os.chdir(path)

    binary = binaries[i]
    arg = args[i]
    tick_interval = str(tick_intervals[i])

    gem5_out = f"{RESULTS_DIR}/{program}/"
    sim_out = f"checkpoint_{operation_mode}_small.out"
    cur_input = stdin_file[i]

    # recency_list_size = small_recency_list_sizes[i]
    recency_list_size = small_memory_capacity[i]

    if cur_input:
        gem5_cmd = "nohup {}/build/X86/gem5.opt -d {} {}/configs/example/gem5_library/checkpoints/simpoints-se-checkpoint-new.py \
                --simpoint_interval 1000000000 --simpoint_file {}/{}/simpoints --weight_file {}/{}/weights.txt --warmup_interval 100000 \
                --checkpoint-path {} --env {}/env.txt --cmd ./{}  --stdin_file {} --mem_operation_mode={} --recency_list_size={} --tick_interval={} \
                --options {} > {} 2>&1 &".format(GEM5_DIR, gem5_out, GEM5_DIR, BUILD_DIR, program, BUILD_DIR, program, gem5_out, SIMULATE_DIR, binary,  cur_input, operation_mode, recency_list_size, tick_interval, arg, sim_out)
    else:
        gem5_cmd = "nohup {}/build/X86/gem5.opt -d {} {}/configs/example/gem5_library/checkpoints/simpoints-se-checkpoint-new.py \
                --simpoint_interval 1000000000 --simpoint_file {}/{}/simpoints --weight_file {}/{}/weights.txt --warmup_interval 100000 \
                --checkpoint-path {} --env {}/env.txt --cmd ./{} --mem_operation_mode={} --recency_list_size={} --tick_interval={} \
                --options {} > {} 2>&1 &".format(GEM5_DIR, gem5_out, GEM5_DIR, BUILD_DIR, program, BUILD_DIR, program, gem5_out, SIMULATE_DIR, binary, operation_mode, recency_list_size, tick_interval, arg, sim_out)

    print(gem5_cmd)
    os.system(gem5_cmd)
