package main

/*
#include <stdlib.h>
*/
import "C"

import (
	"bytes"
	"fmt"
	"os"
	"unsafe"
)

const maxCaptureBytes = 128 << 20

func buildArgs(argc C.int, argv **C.char) []string {
	count := int(argc)
	if count <= 0 || argv == nil {
		return nil
	}
	slice := unsafe.Slice(argv, count)
	args := make([]string, count)
	for i, arg := range slice {
		args[i] = C.GoString(arg)
	}
	return args
}

//export hrtf2f_version
func hrtf2f_version() *C.char {
	return C.CString(buildVersion)
}

//export hrtf2f_free
func hrtf2f_free(ptr *C.char) {
	if ptr != nil {
		C.free(unsafe.Pointer(ptr))
	}
}

//export hrtf2f_run
func hrtf2f_run(argc C.int, argv **C.char) (code C.int) {
	defer func() {
		if recover() != nil {
			code = 1
		}
	}()
	return C.int(run(buildArgs(argc, argv), os.Stdout, os.Stderr))
}

//export hrtf2f_run_capture
func hrtf2f_run_capture(argc C.int, argv **C.char, out **C.char,
	outLen *C.size_t, errOut **C.char, errLen *C.size_t) (code C.int) {
	defer func() {
		if recovered := recover(); recovered != nil {
			*out = C.CString("")
			*outLen = 0
			message := fmt.Sprintf("F2F_INTERNAL: %v", recovered)
			*errOut = C.CString(message)
			*errLen = C.size_t(len(message))
			code = 1
		}
	}()

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	code = C.int(run(buildArgs(argc, argv), &stdout, &stderr))
	if stdout.Len() > maxCaptureBytes || stderr.Len() > maxCaptureBytes {
		message := "F2F_LIMIT: output exceeds 128 MiB"
		*out = C.CString("")
		*outLen = 0
		*errOut = C.CString(message)
		*errLen = C.size_t(len(message))
		return 1
	}
	*out = C.CString(stdout.String())
	*outLen = C.size_t(stdout.Len())
	*errOut = C.CString(stderr.String())
	*errLen = C.size_t(stderr.Len())
	return code
}
