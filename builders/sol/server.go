/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package main

import (
	"archive/tar"
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

// TODO: Once the behavior is settled, improve the errors messages.

func runServer() {
	addr := ":2222"
	fmt.Println("Listening at", addr)
	http.HandleFunc("/build/", handleBuild)
	http.HandleFunc("/list", handleList)
	log.Fatal(http.ListenAndServe(addr, nil))
}

func listPlatforms() []string {
	dir, _ := os.Open(initialDir)
	fis, _ := dir.Readdir(0)

	var platforms []string
	for _, fi := range fis {
		if fi.IsDir() && strings.HasPrefix(fi.Name(), "platform-") {
			platforms = append(platforms, fi.Name())
		}
	}

	dir.Close()
	return platforms
}

func handleList(w http.ResponseWriter, r *http.Request) {
	for _, p := range listPlatforms() {
		fmt.Fprintln(w, p)
	}
}

func platformExists(platform string) bool {
	for _, p := range listPlatforms() {
		if p == platform {
			return true
		}
	}
	return false
}

func clientError(w http.ResponseWriter, format string, a ...interface{}) {
	formatted := fmt.Sprintf(format, a...)
	fmt.Println("Request failed: " + formatted)
	fmt.Fprintln(w, "Request failed: "+formatted)
}

func internalError(w http.ResponseWriter, format string, a ...interface{}) {
	formatted := fmt.Sprintf(format, a...)
	fmt.Println("Internal error: " + formatted)
	fmt.Fprintln(w, "Internal error: "+formatted)
}

func compile(dir string, platform string) (*bytes.Buffer, error) {
	var out bytes.Buffer
	mw := io.MultiWriter(&out, os.Stdout)

	// TODO: Check permission denied error.

	cmd := exec.Command(initialDir + "/" + platform + "/compile")
	cmd.Dir = dir
	cmd.Stdout = mw
	cmd.Stderr = mw

	return &out, cmd.Run()
}

func handleBuild(w http.ResponseWriter, r *http.Request) {
	plat := r.URL.Path[7:]

	if !platformExists(plat) {
		clientError(w, "%s is not available", plat)
		return
	}

	dir, err := ioutil.TempDir("", "sb-")
	if err != nil {
		internalError(w, "couldn't create tempdir: %s", err)
		return
	}

	defer os.RemoveAll(dir)

	tr := tar.NewReader(r.Body)

	header, _ := tr.Next()
	for ; header != nil; header, _ = tr.Next() {
		filename := header.Name
		if !isValidName(filename) {
			clientError(w, "invalid filename "+header.Name+" provided as input")
			return
		}

		switch header.Typeflag {
		case tar.TypeDir:
			os.MkdirAll(dir+"/"+filename, 0755)

		case tar.TypeReg:
			f, err := os.Create(dir + "/" + filename)
			if err != nil {
				log.Fatal(err)
			}
			io.Copy(f, tr)
			f.Close()
		}
	}

	out, err := compile(dir, plat)

	if err != nil {
		if out.Bytes() != nil {
			clientError(w, "Compilation error\n")
			io.Copy(w, out)
		} else {
			internalError(w, "couldn't call compile: %s", err)
		}
	} else {
		bin, err := os.Open(dir + "/output.zip")
		if err != nil {
			internalError(w, err.Error())
			return
		}
		w.Header().Set("Content-Type", "application/octet-stream")
		stat, _ := bin.Stat()
		fmt.Println(strconv.FormatInt(stat.Size(), 10))
		io.Copy(os.Stdout, out)
		w.Header().Set("Content-Length", strconv.FormatInt(stat.Size(), 10))
		io.Copy(w, bin)
	}
}
