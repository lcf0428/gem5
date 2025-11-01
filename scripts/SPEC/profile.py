import os
import sys
import optparse
import glob

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

HOME = "/local/home/liuche"
BASE_DIR = "{}/SPEC".format(HOME)
GEM5_DIR = "{}/gitlab/gem5".format(HOME)
RESULTS_DIR = "{}/gem5_results/SPEC/SE_500B/profile".format(GEM5_DIR)
SIMULATE_DIR = "{}/scripts/SPEC".format(GEM5_DIR)

for i in range(len(programs)):
        program = programs[i]

        path = "{}/{}".format(BASE_DIR, benchmarks_dir[i])
        print("Directoy changed to: " + path)
        os.chdir(path)

        binary = binaries[i]
        arg = args[i]

        CHECKPOINT_DIR = "{}/{}/m5out-profiling".format(BASE_DIR, benchmarks_dir[i])
        gem5_out = "{}/{}/".format(RESULTS_DIR, program)
        sim_out = "gem5_2.out"
        cur_input = inputs[i]

        if cur_input:
                gem5_cmd = "nohup {}/build/X86/gem5.opt -d {}  {}/configs/deprecated/example/se.py --simpoint-profile \
                        --simpoint-interval 1000000000 --maxinsts 500000000000 --cpu-type=NonCachingSimpleCPU --mem-type=SimpleMemory --mem-size=20GB --env {}/env.txt --cmd ./{} --options=\"{}\" --input {} > {} 2>&1 &".format(GEM5_DIR, gem5_out, GEM5_DIR, SIMULATE_DIR, binary, arg, cur_input, sim_out)
        else:
                gem5_cmd = "nohup {}/build/X86/gem5.opt -d {}  {}/configs/deprecated/example/se.py --simpoint-profile \
                        --simpoint-interval 1000000000 --maxinsts 500000000000 --cpu-type=NonCachingSimpleCPU --mem-type=SimpleMemory --mem-size=20GB --env {}/env.txt --cmd ./{} --options=\"{}\" > {} 2>&1 &".format(GEM5_DIR, gem5_out, GEM5_DIR, SIMULATE_DIR, binary, arg, sim_out)


        print(gem5_cmd)
        os.system(gem5_cmd)
