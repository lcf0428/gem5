# /local/home/liuche/SimPoint/SimPoint.3.2/analysiscode/simpoint -loadFVFile /local/home/liuche/test/gem5_results/SPEC/SE_500B/profile/omnetpp/simpoint.bb.gz -maxK 10 -saveSimpoints /local/home/liuche/simpoint_test/simpoints -saveSimpointWeights /local/home/liuche/simpoint_test/weights.txt -inputVectorsGzipped

import os
import sys
import optparse
import glob

programs = [
        # "perlbench",
        # "gcc",
        # "mcf",
        # "omnetpp",
        # "xalancbmk",
        # "x264",
        # "deepsjeng",
        # "leela",
        # "exchange2",
        # "xz",
        # "bwaves",
        # "cactuBSSN",
        # "lbm",
        # "wrf",
        # # "cam4",
        # "pop2",
        "imagick",
        # "nab",
        "fotonik3d",
        # "roms",
]

HOME = "/local/home/liuche"
SIMPOINT_DIR = "{}/SimPoint/SimPoint.3.2/analysiscode".format(HOME)
PROFILE_DIR = "{}/test/gem5_results/SPEC/SE_500B/profile".format(HOME)
BUILD_DIR = "{}/test/gem5_results/SPEC/SE_500B/build".format(HOME)


for i in range(len(programs)):
    program = programs[i]

    simpoint_cmd = "nohup {}/simpoint -loadFVFile {}/{}/simpoint.bb.gz -maxK 1 -saveSimpoints {}/{}/simpoints -saveSimpointWeights {}/{}/weights.txt -inputVectorsGzipped > {}/{}/simPoint.out 2>&1 &".format(SIMPOINT_DIR, PROFILE_DIR, program, BUILD_DIR, program, BUILD_DIR, program, PROFILE_DIR, program)

    print(simpoint_cmd)
    os.system(simpoint_cmd)
