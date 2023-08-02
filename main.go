package main

import (
	"fmt"
	"os"
	"unsafe"
)

/*
#cgo !windows LDFLAGS: -ldl
#include <stdio.h>
#include <stdlib.h>
#include "bridge.c"
*/
import "C"

func main() {
	fmt.Println(os.Args)
	a := C.CString(os.Args[1])
	b := C.CString(os.Args[2])
	result := C.FixWebmFile(a, b)
	C.free(unsafe.Pointer(a))
	C.free(unsafe.Pointer(b))
	fmt.Println(result) // prints 12
}
