import subprocess
import os


def main():
    subprocess.run(["echo", "\n\r=============building============\n\r"])
    subprocess.run(["scons", "-j6"])
    # subprocess.run(["echo", "\n\r=========汇编文件生成中==========\n\r"])
    os.system("/home/hehe/gcc-arm-none-eabi-8-2019-q3-update/bin/arm-none-eabi-objdump -d ./rt-thread.elf >./rtthread.asm")
    # subprocess.run(["echo", "\n\r=========解析ELF文件中==========\n\r"])
    os.system("/home/hehe/gcc-arm-none-eabi-8-2019-q3-update/bin/arm-none-eabi-readelf -a ./rt-thread.elf >./rtthreadelf.asm")
    # subprocess.run(["echo", "\n\r==============成功===============\n\r"])
    # subprocess.run(["make", "all"])
    # subprocess.run(["make", "change"])
    # subprocess.run(["arm-none-eabi-objdump", "-d",
    #                 "./OBJ/Output.elf", ">./OBJ/Output.s"])
    # subprocess.run(["make", "delete"])


if __name__ == "__main__":
    main()
