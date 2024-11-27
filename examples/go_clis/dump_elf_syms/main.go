/*
 * @Author: CALM.WU
 * @Date: 2023-01-09 15:23:25
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2023-01-09 17:44:33
 */

package main

import (
	"debug/dwarf"
	"debug/elf"
	"flag"

	"github.com/golang/glog"
)

var _elf_file_path string

func init() {
	flag.StringVar(&_elf_file_path, "path", "/proc/self/exe", "execute or so file path")
}

func main() {
	flag.Parse()

	glog.Infof("start to dump '%d' address+symbol", _elf_file_path)

	f, err := elf.Open(_elf_file_path)
	if err != nil {
		glog.Fatalf("elf.Open failed! err: %s", err.Error())
	}
	defer f.Close()

	for index, section := range f.Sections {
		glog.Infof("[%d] section: %s, type: %s", index, section.Name, section.Type.String())
	}

	symbols, err := f.Symbols()
	if err != nil {
		glog.Fatalf("f.Symbols failed! err: %s", err.Error())
	}
	// glog.Info("dump SHT_SYMTAB")
	for _, symbol := range symbols {
		if int(symbol.Section) < len(f.Sections) && f.Sections[symbol.Section].Name == ".text" {
			glog.Infof("[.text] %s: %#x", symbol.Name, symbol.Value)
		}
	}

	// symbols, err = f.DynamicSymbols()
	// if err != nil {
	// 	glog.Fatal("f.DynamicSymbols failed! err: %s", err.Error())
	// } else {
	// 	for _, symbol := range symbols {
	// 		glog.Infof("[.dynsym] %s: %#x, library: %s", symbol.Name, symbol.Value, symbol.Library)
	// 	}
	// }

	// get the DWARF data，debug 信息
	dw, err := f.DWARF()
	if err == nil {
		rdr := dw.Reader()
		for {
			entry, err := rdr.Next()
			if err != nil {
				glog.Errorf("rdr.Next failed! err: %s", err.Error())
				break
			}

			if entry == nil {
				break
			}

			if entry.Tag == dwarf.TagSubprogram {
				name, ok := entry.Val(dwarf.AttrName).(string)
				if !ok {
					continue
				}

				lowpc, ok := entry.Val(dwarf.AttrLowpc).(uint64)
				if !ok {
					continue
				}
				glog.Infof("[debug] %s: %#x", name, lowpc)
			}
		}
	}

	// glog.Info("dump SHT_DYNSYM")
	// symbols, err := f.DynamicSymbols()
	// if err != nil {
	// 	glog.Fatalf("f.DynamicSymbols failed! err: %s", err.Error())
	// }
	// for _, symbol := range symbols {
	// 	glog.Infof("\t%s: %#x, library: %s", symbol.Name, symbol.Value, symbol.Library)
	// }

	glog.Infof("dump '%s' address+symbols exit", _elf_file_path)
}

// https://www.hitzhangjie.pro/debugger101.io/7-headto-sym-debugger/6-gopkg-debug/3-dwarf.html
/*
⚡ root@localhost  /home/calmwu/program/cpp_space/x-monitor/bin   nm -a /usr/lib64/libpthread-2.28.so|grep open
U fopen@@GLIBC_2.2.5                 U __libc_dlopen_mode@@GLIBC_PRIVATE
00000000000121d0 t __libc_open
00000000000121d0 t __libc_open64
00000000000121d0 T __open
00000000000121d0 W open
00000000000121d0 T __open64
00000000000121d0 W open64
U __open64_nocancel@@GLIBC_PRIVATE
0000000000010560 T sem_open
⚡ root@localhost  /home/calmwu/program/cpp_space/x-monitor/bin  ./dump_elf_syms --path=/usr/lib64/libpthread-2.28.so --alsologtostderr -v=4
*/
