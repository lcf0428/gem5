import glob
import optparse
import os
import sys

programs = [
    # "betweennessCentr",
    "BFS",
    "connectedComp",
    "degreeCentr",
    "DFS",
    "pageRank",
    "graphColor",
    "shortestPath",
    "kCore",
    "triangleCount",
]

benchmarks_dir = [
    # "benchmark/bench_betweennessCentr",
    "benchmark/bench_BFS",
    "benchmark/bench_connectedComp",
    "benchmark/bench_degreeCentr",
    "benchmark/bench_DFS",
    "benchmark/bench_pageRank",
    "benchmark/bench_graphColoring",
    "benchmark/bench_shortestPath",
    "benchmark/bench_kCore",
    "benchmark/bench_triangleCount",
    # # "benchspec/CPU/600.perlbench_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/602.gcc_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/605.mcf_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/620.omnetpp_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/623.xalancbmk_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/625.x264_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/631.deepsjeng_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/641.leela_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/648.exchange2_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/657.xz_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/603.bwaves_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/607.cactuBSSN_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/619.lbm_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/621.wrf_s/run/run_base_refspeed_mytest-m64.0000",
    # # # "benchspec/CPU/627.cam4_s/run/run_base_refspeed_mytest-m64.0000",
    # # "benchspec/CPU/628.pop2_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/638.imagick_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/644.nab_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/649.fotonik3d_s/run/run_base_refspeed_mytest-m64.0000",
    # "benchspec/CPU/654.roms_s/run/run_base_refspeed_mytest-m64.0000",
]

binaries = [
    # "./bc",
    "./bfs",
    "./connectedcomponent",
    "./dc",
    "./dfs",
    "./pagerank",
    "./graphcoloring",
    "./sssp",
    "./kcore",
    "./tc",
    # # "./perlbench_s_base.mytest-m64",
    # "./sgcc_base.mytest-m64",
    # "./mcf_s_base.mytest-m64",
    # "./omnetpp_s_base.mytest-m64",
    # "./xalancbmk_s_base.mytest-m64",
    # "./x264_s_base.mytest-m64",
    # "./deepsjeng_s_base.mytest-m64",
    # "./leela_s_base.mytest-m64",
    # "./exchange2_s_base.mytest-m64",
    # "./xz_s_base.mytest-m64",
    # "./speed_bwaves_base.mytest-m64",
    # "./cactuBSSN_s_base.mytest-m64",
    # "./lbm_s_base.mytest-m64",
    # "./wrf_s_base.mytest-m64",
    # # # "./cam4_s_base.mytest-m64",
    # # "./speed_pop2_base.mytest-m64",
    # "./imagick_s_base.mytest-m64",
    # "./nab_s_base.mytest-m64",
    # "./fotonik3d_s_base.mytest-m64",
    # "./sroms_base.mytest-m64",
]

args = [
    # "--undirected --threadnum 2 --dataset ../../dataset/small --perf-event PERF_COUNT_HW_CPU_CYCLES PERF_COUNT_HW_INSTRUCTIONS PERF_COUNT_HW_BRANCH_INSTRUCTIONS PERF_COUNT_HW_BRANCH_MISSES PERF_COUNT_HW_CACHE_L1D_READ_ACCESS PERF_COUNT_HW_CACHE_L1D_READ_MISS",
    # "--threadnum 2 --dataset ../../dataset/small --root 31 --perf-event PERF_COUNT_HW_CPU_CYCLES PERF_COUNT_HW_INSTRUCTIONS PERF_COUNT_HW_BRANCH_INSTRUCTIONS PERF_COUNT_HW_BRANCH_MISSES PERF_COUNT_HW_CACHE_L1D_READ_ACCESS PERF_COUNT_HW_CACHE_L1D_READ_MISS",
    # "--threadnum 2 --dataset ../../dataset/small --perf-event PERF_COUNT_HW_CPU_CYCLES PERF_COUNT_HW_INSTRUCTIONS PERF_COUNT_HW_BRANCH_INSTRUCTIONS PERF_COUNT_HW_BRANCH_MISSES PERF_COUNT_HW_CACHE_L1D_READ_ACCESS PERF_COUNT_HW_CACHE_L1D_READ_MISS",
    # "--threadnum 2 --dataset ../../dataset/small --perf-event PERF_COUNT_HW_CPU_CYCLES PERF_COUNT_HW_INSTRUCTIONS PERF_COUNT_HW_BRANCH_INSTRUCTIONS PERF_COUNT_HW_BRANCH_MISSES PERF_COUNT_HW_CACHE_L1D_READ_ACCESS PERF_COUNT_HW_CACHE_L1D_READ_MISS",
    # "--dataset ../../dataset/small --root 31 --perf-event PERF_COUNT_HW_CPU_CYCLES PERF_COUNT_HW_INSTRUCTIONS PERF_COUNT_HW_BRANCH_INSTRUCTIONS PERF_COUNT_HW_BRANCH_MISSES PERF_COUNT_HW_CACHE_L1D_READ_ACCESS PERF_COUNT_HW_CACHE_L1D_READ_MISS",
    # "--undirected --threadnum 1 --dataset ../../dataset/small",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb --root 6",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb",
    "--dataset /local/home/liuche/dataset/datagen-7_5-fb --root 6",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb --quad 0.001 --damp 0.85 --maxiter 100",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb --root 6",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb --kcore 6",
    "--threadnum 1 --dataset /local/home/liuche/dataset/datagen-7_5-fb",
    # # "-I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1",
    # "gcc-pp.c -O5 -fipa-pta -o gcc-pp.opts-O5_-fipa-pta.s",
    # "inp.in",
    # "-c General -r 0",
    # "-v t5.xml xalanc.xsl",
    # "--pass 1 --stats x264_stats.log --bitrate 1000 --frames 1000 -o BuckBunny_New.264 BuckBunny.yuv 1280x720",
    # "ref.txt",
    # "ref.sgf",
    # "6",
    # "cpu2006docs.tar.xz 6643 055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae 1036078272 1111795472 4",
    # "bwaves_1",
    # "spec_ref.par",
    # "2000 reference.dat 0 0 200_200_260_ldc.of",
    # "",
    # # # "",
    # # "",
    # "-limit disk 0 refspeed_input.tga -resize 817% -rotate -2.76 -shave 540x375 -alpha remove -auto-level -contrast-stretch 1x1% -colorspace Lab -channel R -equalize +channel -colorspace sRGB -define histogram:unique-colors=false -adaptive-blur 0x5 -despeckle -auto-gamma -adaptive-sharpen 55 -enhance -brightness-contrast 10x10 -resize 30% refspeed_output.tga",
    # " 3j1n 20140317 220",
    # "",
    # "",
]

inputs = [
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
    # # "",
    # "",
    # "",
    # "",
    # "",
    # "",
    # "",
    # "",
    # "",
    # "",
    # "bwaves_1.in",
    # "",
    # "",
    # "",
    # # # "",
    # # "",
    # "",
    # "",
    # "",
    # "ocean_benchmark3.in",
]

HOME = "/local/home/liuche"
BASE_DIR = f"{HOME}/graphBIG/graphBIG"
GEM5_DIR = f"{HOME}/gem5/gem5"
RESULTS_DIR = f"{HOME}/gem5_results/graphBIG/profile"
SIMULATE_DIR = f"{GEM5_DIR}/scripts/graphBIG"

ids = [8]

# for i in range(len(programs)):
for i in ids:
    program = programs[i]

    path = f"{BASE_DIR}/{benchmarks_dir[i]}"
    print("Directoy changed to: " + path)
    os.chdir(path)

    binary = binaries[i]
    arg = args[i]

    CHECKPOINT_DIR = "{}/{}/m5out-profiling".format(
        BASE_DIR, benchmarks_dir[i]
    )
    gem5_out = f"{RESULTS_DIR}/{program}/"
    sim_out = f"{RESULTS_DIR}/gem5_{program}.out"
    cur_input = inputs[i]

    if cur_input:
        gem5_cmd = 'nohup {}/build/X86/gem5.opt -d {}  {}/configs/deprecated/example/se.py --simpoint-profile \
                        --simpoint-interval 1000000000 --maxinsts 500000000000 --cpu-type=NonCachingSimpleCPU --mem-type=SimpleMemory --mem-size=25GB --env {}/env.txt --cmd ./{} --options="{}" --input {} > {} 2>&1 &'.format(
            GEM5_DIR,
            gem5_out,
            GEM5_DIR,
            SIMULATE_DIR,
            binary,
            arg,
            cur_input,
            sim_out,
        )
    else:
        gem5_cmd = 'nohup {}/build/X86/gem5.opt -d {}  {}/configs/deprecated/example/se.py --simpoint-profile \
                        --simpoint-interval 1000000000 --maxinsts 500000000000 --cpu-type=NonCachingSimpleCPU --mem-type=SimpleMemory --mem-size=25GB --env {}/env.txt --cmd ./{} --options="{}" > {} 2>&1 &'.format(
            GEM5_DIR, gem5_out, GEM5_DIR, SIMULATE_DIR, binary, arg, sim_out
        )

    print(gem5_cmd)
    os.system(gem5_cmd)
